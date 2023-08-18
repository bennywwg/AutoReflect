#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <variant>

#include <regex>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>

#include <chrono>
#include <functional>
#include <thread>

#include "Parsing.hpp"
#include "Generating.hpp"

#define GeneratedSuffix ".gen.inl"

const std::regex ClassRegex("class ([a-zA-Z0-9_]+) definition");
const std::regex FieldRegex("([a-zA-Z0-9_]+) '([a-zA-Z0-9_:<>, \\*\\&\\[\\]]+)'");

class GeneratorContext {
private:
    std::vector<Template> TemplateStack;
    std::vector<std::string> NameStack;
    std::vector<std::string> Errors;
    std::map<std::string, std::string> EnumTypeMaps;
    int NumAutoReflectNamespaces = 0;
public:
    // Output Variables:
    ImplementationGeneratorSet Generators;

    std::string GetFullyQualifiedName() const {
        if (TemplateStack.empty() && NameStack.empty()) return "";

        std::string FullyQualified;
        for (auto const& Name : NameStack) {
            FullyQualified += Name + "::";
        }
        if (!FullyQualified.empty()) FullyQualified.erase(FullyQualified.end() - 2, FullyQualified.end());

        return FullyQualified;
    }

    Template GetFlattenedTemplates() const {
        if (TemplateStack.empty()) return Template { };

        Template Flattened;
        for (auto const& Template : TemplateStack) {
            for (auto const& Param : Template.Params) {
                Flattened.Params.push_back(Param);
            }
        }

        return Flattened;
    }

    struct EnumDefinition {
        std::string LocalEnumName;
        std::string UnderlyingType;
    };
    std::optional<EnumDefinition> GetAsEnumDefinition(ASTPtr Node) {
        if (Node->Tag != TagType::EnumDecl) return std::nullopt;
        if (Node->Line.back() != '\'') return std::nullopt;

        auto ClassIndex = Node->Line.find("class ");
        if (ClassIndex == std::string::npos) return std::nullopt;

        std::string Line = Node->Line.substr(ClassIndex + 6);
        auto NextSpaceIndex = Line.find(' ');
        if (NextSpaceIndex == std::string::npos) return std::nullopt;

        Line.pop_back(); // Remove the '
        auto LastSpaceIndex = Line.find_last_of('\'');
        if (LastSpaceIndex == std::string::npos) return std::nullopt;

        return EnumDefinition { Line.substr(0, NextSpaceIndex), Line.substr(LastSpaceIndex + 1) };
    }

    struct ClassDefinition {
        std::string LocalClassName;
    };
    std::optional<ClassDefinition> GetAsClassDefinition(ASTPtr Node) {
        if (Node->Tag == TagType::CXXRecordDecl && (Node->Line.find("implicit") == std::string::npos)) {
            
            std::smatch ClassMatch;
            if (std::regex_search(Node->Line, ClassMatch, ClassRegex)) {
                return ClassDefinition{ ClassMatch[1] };
            }
        }

        return std::nullopt;
    }

    struct NamespaceDefinition {
        std::string LocalNamespaceName;
    };
    std::optional<NamespaceDefinition> GetAsNamespaceDefinition(ASTPtr Node) {
        if (Node->Tag == TagType::NamespaceDecl) {
            // The namespace name is the last word in the line
            size_t i;
            for (i = Node->Line.size() - 1; i > 0; --i) {
                if (Node->Line[i] == ' ') break;
            }

            return NamespaceDefinition { Node->Line.substr(i + 1) };
        }

        return std::nullopt;
    }

    struct TemplateDefinition {
        Template T;
    };
    std::optional<TemplateDefinition> GetAsTemplateDefinition(ASTPtr Node, TemplatePtr ParentTemplate = nullptr) {
        if (
            Node->Tag == TagType::ClassTemplateDecl ||
            Node->Tag == TagType::TemplateTypeParmDecl ||
            Node->Tag == TagType::NonTypeTemplateParmDecl ||
            Node->Tag == TagType::TemplateTemplateParmDecl
        ) {
            TemplatePtr T = ParentTemplate ? ParentTemplate : std::make_shared<Template>();

            for (size_t i = 0; i < Node->Children.size(); ++i) {
                auto Child = Node->Children[i];
                if (
                    Child->Tag == TagType::TemplateTypeParmDecl ||
                    Child->Tag == TagType::NonTypeTemplateParmDecl ||
                    Child->Tag == TagType::TemplateTemplateParmDecl
                ) {
                    std::string TypeName;
                    std::string VarName;
                    bool HasType;
                    
                    if (GetTemplateParams(Child->Line, TypeName, VarName, HasType)) {
                        TemplateParam TParam;
                        if (Child->Tag == TagType::TemplateTemplateParmDecl) {
                            TemplatePtr TemplateChild = std::make_shared<Template>();
                            GetAsTemplateDefinition(Child, TemplateChild);
                            TParam = TemplateChild;
                            TemplateChild->Name = VarName;
                        } else {
                            TParam = KindOrType { TypeName, VarName };
                        }
                        T->Params.push_back(TParam);
                    }
                }
            }

            return TemplateDefinition{ *T };
        }

        return std::nullopt;
    }

    struct FieldDefinition {
        std::string TypeName;
        std::string VarName;
    };
    std::optional<FieldDefinition> GetAsField(ASTPtr Node) {
        // Sample line:
        // 0x13c10cb60 <line:16:9, col:13> col:13 h 'int'
        if (Node->Tag != TagType::FieldDecl) {
            return std::nullopt;
        }

        // Allow :,<,>, for qualified names and templates
        std::smatch FieldMatch;
        if (std::regex_search(Node->Line, FieldMatch, FieldRegex)) {
            return FieldDefinition{ FieldMatch[2], FieldMatch[1] };
        } else {
            return std::nullopt;
        }
    }

    void GenerateClass(ASTPtr Node, int Indent, bool Generating) {
        auto ClassDef = GetAsClassDefinition(Node);
        if (!ClassDef) {
            throw std::runtime_error("Could not find class definition");
        }

        NameStack.push_back(ClassDef->LocalClassName);

        Template Flattened = GetFlattenedTemplates();
        std::string Templates = Flattened.Generate();
        std::string FullyQualified = GetFullyQualifiedName();
        std::string SerializeFieldsSource, DeserializeFieldsSource;

        bool FoundAutoReflect = false;
        for (size_t i = 0; i < Node->Children.size(); ++i) {
            auto Child = Node->Children[i];
            
            if (std::optional<FieldDefinition> FieldDef = GetAsField(Child)) {
                FieldDefinition FD = *FieldDef;

                std::string SerializeName = "Val." + FD.VarName;
                if (EnumTypeMaps.find(FD.TypeName) != EnumTypeMaps.end()) {
                    SerializeName = "static_cast<" + EnumTypeMaps[FD.TypeName] + ">(" + SerializeName + ")";
                }

                std::string DeserializeName = "Val." + FD.VarName;
                if (EnumTypeMaps.find(FD.TypeName) != EnumTypeMaps.end()) {
                    DeserializeName = "*reinterpret_cast<" + EnumTypeMaps[FD.TypeName] + "*>(&" + DeserializeName + ")";
                }

                SerializeFieldsSource += "    Serialize(Ser, \"" + FD.VarName + "\", " + SerializeName + ");\n";
                DeserializeFieldsSource += "    Deserialize(Ser, \"" + FD.VarName + "\", " + DeserializeName + ");\n";
            } else if ((Child->Tag == TagType::Private || Child->Tag == TagType::Public) && Child->Line == "'AutoReflect'") {
                FoundAutoReflect = true;
            }
        }

        GenerateScope(Node, Indent + 1, Generating);

        NameStack.pop_back();   

        std::string const FullTypeName = FullyQualified + Flattened.GenerateNames();
        if (Generators.Generators.find(FullTypeName) != Generators.Generators.end()) {
            return;
        }

        if ((NumAutoReflectNamespaces > 0 || FoundAutoReflect) && TemplateStack.size() > 1) {
            Errors.push_back("Nested templates are not supported");
            Generating = false;
        }

        if ((NumAutoReflectNamespaces > 0 || FoundAutoReflect) && Generating) {
            if (TemplateStack.empty()) // Dynamic types are not supported for templates
            {
                Generators.NonTemplateTypes.insert(FullyQualified);
            }

            Generators.Generators[FullTypeName] = ImplementationGenerator { Templates, FullTypeName, SerializeFieldsSource, DeserializeFieldsSource };
        }
    }

    void GenerateScope(ASTPtr Node, int Indent, bool Generating) {
        // Assert if tag isn't a scope
        if (Node->Tag != TagType::TranslationUnitDecl && Node->Tag != TagType::NamespaceDecl && Node->Tag != TagType::ClassTemplateDecl && Node->Tag != TagType::CXXRecordDecl) {
            throw std::runtime_error("Tag is not a scope, is " + std::to_string(static_cast<int>(Node->Tag)));
        }

        for (size_t i = 0; i < Node->Children.size(); ++i) {
            auto Child = Node->Children[i];
            
            if (std::optional<TemplateDefinition> TemplateDef = GetAsTemplateDefinition(Child)) {
                TemplateStack.push_back(TemplateDef->T);
                ASTPtr ClassNode = Node->Children.back();
                if (GetAsClassDefinition(ClassNode)) {
                    GenerateClass(ClassNode, Indent, Generating);
                }
                TemplateStack.pop_back();
            } else if (std::optional<ClassDefinition> ClassDef = GetAsClassDefinition(Child)) {
                if (Child->Line.find("implicit") != std::string::npos) {
                    continue;
                }
                GenerateClass(Child, Indent + 1, Generating);
            } else if (std::optional<NamespaceDefinition> NamespaceDef = GetAsNamespaceDefinition(Child)) {
                if (NamespaceDef->LocalNamespaceName == "AutoReflect") ++NumAutoReflectNamespaces;
                NameStack.push_back(NamespaceDef->LocalNamespaceName);
                GenerateScope(Child, Indent + 1, Generating);
                NameStack.pop_back();
                if (NamespaceDef->LocalNamespaceName == "AutoReflect") --NumAutoReflectNamespaces;
            } else if (std::optional<EnumDefinition> EnumDef = GetAsEnumDefinition(Child)) {
                // Get fully qualified version of enum name, may not always work
                std::string FullyQualified;
                for (std::string const& Name : NameStack) {
                    FullyQualified += Name + "::";
                }
                FullyQualified += EnumDef->LocalEnumName;
                EnumTypeMaps[FullyQualified] = EnumDef->UnderlyingType;
            }
        }
    }

    GeneratorContext(std::filesystem::path const& Path, std::vector<std::filesystem::path> const& IncludePaths, bool Silent) {
        CallOnDtor OnDtor([&]() {
            TemplateStack.clear();
            NameStack.clear();
            Errors.clear();
            NumAutoReflectNamespaces = 0;
        });

        ASTPtr Root = LoadASTNodes(Path, IncludePaths, Silent);

        try {
            GenerateScope(Root, 0, true);
        } catch (std::runtime_error const& Er) {
            std::cerr << "Failed during generation (internal error): " << Er.what() << std::endl;
            return;
        }

        for (std::string const& Error : Errors) {
            std::cerr << Error << std::endl;
        }
    }
};

struct InputParams {
    std::vector<std::filesystem::path> IncludePaths;
    std::vector<std::filesystem::path> FilesToParse;
    std::filesystem::path MainImpl, MainImplOutput;
    bool Silent = false;

    InputParams(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string Arg = argv[i];
            if (Arg == "-S") {
                Silent = true;
            } else if (Arg == "-M") {
                MainImpl = (argc > i + 1) ? argv[++i] : "";
                MainImplOutput = MainImpl;
                MainImplOutput += GeneratedSuffix;
            } else if (Arg == "-I" && i + 1 < argc) {
                IncludePaths.push_back(argv[++i]);
            } else {
                // Make sure the generated file is included, otherwise its pointless
                bool FoundGenerated = false;
                if (std::ifstream File = std::ifstream(Arg)) {
                    std::string Line;
                    while (std::getline(File, Line)) {
                        if (Line.find(GeneratedSuffix) != std::string::npos) {
                            FoundGenerated = true;
                            break;
                        }
                    }
                }
                if (FoundGenerated) {
                    FilesToParse.push_back(Arg);
                }
            }
        }
    }
};

int main(int argc, char** argv) {
    InputParams Params(argc, argv);

    ImplementationGeneratorSet GlobalGenerators;

    std::mutex SharedContextMut;

    // Does not generate a main file, just tries to place everything in inline functions
    // Does not support SubclassOf reflection when using this mode
    const bool InlineMode = Params.MainImpl.empty();

    // TODO: Support this feature
    if (InlineMode) {
        std::cerr << "Inline mode is not supported yet, you need to specify a main file with -M" << std::endl;
        return 1;
    }
    
    std::atomic_bool GlobalAnyNewer = false;

    ParallelFor([&Params, &GlobalGenerators, &SharedContextMut, InlineMode, &GlobalAnyNewer](std::filesystem::path const& Path) {
        int NumErrors = 0;

        std::filesystem::path OutputPath = Path;
        OutputPath += GeneratedSuffix;

        if (OutputPath == Params.MainImplOutput) return;

        // Get write date of output file
        auto const OutputWriteTime = std::filesystem::exists(OutputPath) ? std::filesystem::last_write_time(OutputPath) : std::filesystem::file_time_type::min();
        auto const InputWriteTime = std::filesystem::last_write_time(Path);
        
        bool AnyNewer = false;
        if (InputWriteTime > OutputWriteTime) {
            AnyNewer = true;
            if (!Params.Silent) Log(Path, "Input is newer than output");
        } else {
            for (auto header : GetAllHeaders(Path, Params.IncludePaths, Params.Silent)) {
                if (!std::filesystem::exists(header)) {
                    // Print in red
                    //std::cerr << "\033[31m" << "Header " << header << " does not exist" << "\033[0m" << std::endl;
                    continue;
                }
                if (OutputPath != header && std::filesystem::last_write_time(header) > OutputWriteTime) {
                    AnyNewer = true;
                    if (!Params.Silent) Log(Path, "Header " + header + " is newer than output");
                    break;
                }
            }
        }

        if (AnyNewer) {
            GlobalAnyNewer = true;
        }

        if (!AnyNewer && !Params.Silent) Log(Path, "No changes detected");

        if (AnyNewer) {
            std::filesystem::copy_file(
                std::filesystem::path(AR_RESOURCES_DIR) / "null_generated.h",
                OutputPath,
                std::filesystem::copy_options::overwrite_existing
            );
        }

        // If nothing newer, try to load the cached generator
        std::optional<ImplementationGeneratorSet> Generators = AnyNewer ? std::nullopt : GetCachedGenerator(Path);

        if (!Generators) {
            GeneratorContext Context(Path, Params.IncludePaths, Params.Silent);

            Generators = Context.Generators;

            SaveCachedGenerator(Path, *Generators);
        }

        if (AnyNewer) {
            std::ofstream GeneratedFile(OutputPath, std::ios::ate);

            GeneratedFile << "#pragma once" << std::endl << std::endl;
            GeneratedFile << "#include <AutoReflectDecls.hpp>" << std::endl << std::endl;

            // First, do all forward decls in the header
            for (auto const& kvp : Generators->Generators) {
                if (Generators->NonTemplateTypes.find(kvp.first) == Generators->NonTemplateTypes.end()) {
                    continue; // Skip templates
                }
                GeneratedFile << kvp.second.Generate(InlineMode ? GenMode::InlineMode : GenMode::ForwardDeclMode) << std::endl;
            }

            // Then template impls only
            for (auto const& kvp : Generators->Generators) {
                if (Generators->NonTemplateTypes.find(kvp.first) != Generators->NonTemplateTypes.end()) {
                    continue; // Skip non-templates
                }
                GeneratedFile << kvp.second.Generate(GenMode::RegularMode) << std::endl;
            }

            GeneratedFile << std::ifstream(std::filesystem::path(AR_RESOURCES_DIR) / "BaseTemplateImpls.txt").rdbuf() << std::endl << std::endl;
        }

        if (!InlineMode) {
            std::lock_guard<std::mutex> Lock(SharedContextMut);

            // TODO: Handle errors vector returned from this operation
            GlobalGenerators.Combine(*Generators);
        }
    }, Params.FilesToParse);

    if (!InlineMode && GlobalAnyNewer) {
        std::ofstream MainImplFile = std::ofstream(Params.MainImplOutput, std::ios::ate);
        
        MainImplFile << "// Base forward declarations" << std::endl;
        MainImplFile << "#include <AutoReflectDecls.hpp>" << std::endl << std::endl;

        MainImplFile << "// Type forward declarations" << std::endl;
        for (auto const& kvp : GlobalGenerators.Generators) {
            MainImplFile << "// " << kvp.first << std::endl;
            MainImplFile << kvp.second.Generate(GenMode::ForwardDeclMode) << std::endl;
        }

        MainImplFile << "// Base implementations" << std::endl;
        MainImplFile << std::ifstream(std::filesystem::path(AR_RESOURCES_DIR) / "BaseImpls.txt").rdbuf() << std::endl << std::endl;
        MainImplFile << "// Base template implementations" << std::endl;
        MainImplFile << std::ifstream(std::filesystem::path(AR_RESOURCES_DIR) / "BaseTemplateImpls.txt").rdbuf() << std::endl << std::endl;

        MainImplFile << "// std::any implementations" << std::endl;
        MainImplFile << GlobalGenerators.GenDynamicReflectionImpl() << std::endl;

        MainImplFile << "// Type implementations" << std::endl;
        for (auto const& kvp : GlobalGenerators.Generators) {
            MainImplFile << "// " << kvp.first << std::endl;
            MainImplFile << kvp.second.Generate(GenMode::RegularMode) << std::endl;
        }
    }
    
    return 0;
}
