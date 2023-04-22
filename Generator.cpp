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

#define GeneratedSuffix ".gen.inl"

const std::regex ClassRegex("class ([a-zA-Z0-9_]+) definition");
const std::regex FieldRegex("([a-zA-Z0-9_]+) '([a-zA-Z0-9_:<>, \\*\\&\\[\\]]+)'");

struct Template;
struct KindOrType {
    std::string KindOrTypeName;
    std::string Name;
};

using TemplatePtr = std::shared_ptr<Template>;
using TemplateParam = std::variant<KindOrType, TemplatePtr>;

struct Template {
    std::vector<TemplateParam> Params;
    std::string Name;

    std::string Generate(bool IsOuter = true) const {
        if (Params.empty()) return "";
        std::string Result = "template<";
        for (auto const& Param : Params) {
            if (std::holds_alternative<KindOrType>(Param)) {
                KindOrType Info = std::get<KindOrType>(Param);
                Result += Info.KindOrTypeName + (Info.Name.empty() ? "" : " ") + Info.Name + ", ";
            } else {
                auto const& Template = std::get<TemplatePtr>(Param);
                Result += Template->Generate(false) + ", ";
            }
        }
        Result.erase(Result.size() - 2);
        Result += ">" + std::string(IsOuter ? std::string() : (std::string(" typename") + (Name.empty() ? "" : " ") + Name));

        return Result;
    }

    std::string GenerateNames() const {
        if (Params.empty()) return "";
        std::string Result = "<";
        for (auto const& Param : Params) {
            if (std::holds_alternative<KindOrType>(Param)) {
                Result += std::get<KindOrType>(Param).Name;
            } else {
                Result += std::get<TemplatePtr>(Param)->Name;
            }
            Result += ", ";
        }
        Result.erase(Result.size() - 2);
        Result += ">";
        return Result;
    }
};

class GeneratorContext {
private:
    std::vector<Template> TemplateStack;
    std::vector<std::string> NameStack;
    std::vector<std::string> Errors;
    std::map<std::string, std::string> EnumTypeMaps;
    int NumAutoReflectNamespaces = 0;
public:
    enum GenMode {
        ForwardDeclMode,
        RegularMode,
        InlineMode
    };

    using CodeBlockGenerator = std::function<std::string(GenMode)>;

    // Output Variables:
    std::set<std::string> NonTemplateTypes;
    std::map<std::string, CodeBlockGenerator> GeneratedBlocks;

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

    void GenerateTemplate(ASTPtr Node, int Indent, bool Generating) {
        auto TemplateDef = GetAsTemplateDefinition(Node);
        if (!TemplateDef) {
            Errors.push_back("Internal error, could not find template definition");
            return;
        }

        TemplateStack.push_back(TemplateDef->T);
        ASTPtr ClassNode = Node->Children.back();
        if (GetAsClassDefinition(ClassNode)) {
            GenerateClass(ClassNode, Indent, Generating);
        }
        TemplateStack.pop_back();
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
        if (GeneratedBlocks.find(FullTypeName) != GeneratedBlocks.end()) {
            return;
        }

        if ((NumAutoReflectNamespaces > 0 || FoundAutoReflect) && TemplateStack.size() > 1) {
            Errors.push_back("Nested templates are not supported");
            Generating = false;
        }

        if ((NumAutoReflectNamespaces > 0 || FoundAutoReflect) && Generating) {
            if (TemplateStack.empty()) // Dynamic types are not supported for templates
            {
                NonTemplateTypes.insert(FullyQualified);
            }

            GeneratedBlocks[FullTypeName] = [Templates, FullTypeName, SerializeFieldsSource, DeserializeFieldsSource](GenMode Mode) {
                std::string Qualifier = (!Templates.empty() || Mode == InlineMode) ? "inline " : "";

                if (!Templates.empty()) {
                    Qualifier = Templates + "\n" + Qualifier;
                }

                std::string GeneratedSource;

                if (Mode == ForwardDeclMode) {
                    GeneratedSource += Qualifier + "void Serialize(Serializer& Ser, char const* Name, " + FullTypeName + " const& Val);\n";
                    GeneratedSource += Qualifier + "void Deserialize(Deserializer& Ser, char const* Name, " + FullTypeName + "& Val);\n";
                    GeneratedSource += Qualifier + "void SerializeFields(Serializer& Ser, " + FullTypeName + " const& Val);\n";
                    GeneratedSource += Qualifier + "void DeserializeFields(Deserializer& Ser, " + FullTypeName + "& Val);\n";
                } else {
                    GeneratedSource += Qualifier + "void SerializeFields(Serializer& Ser, " + FullTypeName + " const& Val) {\n";
                    GeneratedSource += SerializeFieldsSource;
                    GeneratedSource += "}\n\n";

                    GeneratedSource += Qualifier + "void DeserializeFields(Deserializer& Ser, " + FullTypeName + "& Val) {\n";
                    GeneratedSource += DeserializeFieldsSource;
                    GeneratedSource += "}\n\n";

                    GeneratedSource += Qualifier + "void Serialize(Serializer& Ser, char const* Name, " + FullTypeName + " const& Val) {\n";
                    GeneratedSource += "    Ser.BeginObject(Name);\n";
                    GeneratedSource += "    SerializeFields(Ser, Val);\n";
                    GeneratedSource += "    Ser.EndObject();\n";
                    GeneratedSource += "}\n\n";

                    GeneratedSource += Qualifier + "void Deserialize(Deserializer& Ser, char const* Name, " + FullTypeName + "& Val) {\n";
                    GeneratedSource += "    Ser.BeginObject(Name);\n";
                    GeneratedSource += "    DeserializeFields(Ser, Val);\n";
                    GeneratedSource += "    Ser.EndObject();\n";
                    GeneratedSource += "}\n\n";
                }

                return GeneratedSource;
            };
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
                GenerateTemplate(Child, Indent, Generating);
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

    int GenerateFile(ASTPtr Root) {
        CallOnDtor OnDtor([&]() {
            TemplateStack.clear();
            NameStack.clear();
            Errors.clear();
            NumAutoReflectNamespaces = 0;
        });

        if (!Root) {
            std::cerr << "Failed to load AST nodes (internal error)" << std::endl;
            return 1;
        }

        GenerateScope(Root, 0, true);

        for (std::string const& Error : Errors) {
            std::cerr << Error << std::endl;
        }

        return 0;
    }
};

int GenerateSingleFile(GeneratorContext& Context, std::filesystem::path const& Path, std::vector<std::filesystem::path> const& IncludePaths, bool Silent) {
    std::chrono::steady_clock::time_point ParseBegin = std::chrono::steady_clock::now();

    ASTPtr Root = LoadASTNodes(Path, IncludePaths, Silent);

    CallOnDtor OnDtor([Root]() {
        // Why does this crash?
        //DeleteNode(Root);
    });

    std::chrono::steady_clock::time_point ParseEnd = std::chrono::steady_clock::now();
    if (!Silent) Log(Path, "Parsed in " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(ParseEnd - ParseBegin).count()) + "ms");
    
    std::chrono::steady_clock::time_point Begin = std::chrono::steady_clock::now();
    
    try {
        Context.GenerateFile(Root);
    } catch (std::runtime_error const& Er) {
        std::cerr << "Failed during generation (internal error): " << Er.what() << std::endl;
        return 1;
    }

    std::chrono::steady_clock::time_point End = std::chrono::steady_clock::now();
    if (!Silent) Log(Path, "Generated in " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(End - Begin).count()) + "ms");

    return 0;
}

// TODO: Use an acceleration structure instead of just if statements
std::string GenDynamicReflectionImpl(std::set<std::string> const& NonTemplateTypes) {
    std::stringstream GeneratedFile;

    GeneratedFile << "void DeserializeFields(Deserializer& Ser, SubclassOfBase& Val) {" << std::endl;
    GeneratedFile << "    if (Ser.GetCurrentScope() == nullptr) {" << std::endl;
    GeneratedFile << "        Val.Reset();" << std::endl;
    GeneratedFile << "        return;" << std::endl;
    GeneratedFile << "    }" << std::endl;
    GeneratedFile << "    std::string const Type = Ser.AtChecked(\"Type\");" << std::endl;
    for (std::string const& TypeName : NonTemplateTypes) {
        GeneratedFile << "    if (Type == \"" << TypeName << "\") {" << std::endl;
        GeneratedFile << "        Ser.BeginObject(\"Value\");" << std::endl;
        GeneratedFile << "        " << TypeName << " Temp;" << std::endl;
        GeneratedFile << "        DeserializeFields(Ser, Temp);" << std::endl;
        GeneratedFile << "        Val = SubclassOf<" << TypeName << ">(Temp);" << std::endl;
        GeneratedFile << "        Ser.EndObject();" << std::endl;
        GeneratedFile << "        return;" << std::endl;
        GeneratedFile << "    }" << std::endl;
    }
    GeneratedFile << "    throw std::runtime_error(\"Unknown type \" + Type);" << std::endl;
    GeneratedFile << "}" << std::endl << std::endl;
    
    GeneratedFile << "void Deserialize(Deserializer& Ser, char const* Name, SubclassOfBase& Val) {" << std::endl;
    GeneratedFile << "    Ser.BeginObject(Name);" << std::endl;
    GeneratedFile << "    DeserializeFields(Ser, Val);" << std::endl;
    GeneratedFile << "    Ser.EndObject();" << std::endl;
    GeneratedFile << "}" << std::endl << std::endl;

    GeneratedFile << "void SerializeFields(Serializer& Ser, SubclassOfBase const& Val) {" << std::endl;
    GeneratedFile << "    if (!Val.GetAny().has_value()) {" << std::endl;
    GeneratedFile << "        Ser.GetCurrentScope() = nullptr;" << std::endl;
    GeneratedFile << "        return;" << std::endl;
    GeneratedFile << "    }" << std::endl;
    for (std::string const& TypeName : NonTemplateTypes) {
        GeneratedFile << "    if (Val.GetAny().type() == typeid(" << TypeName << ")) {" << std::endl;
        GeneratedFile << "        Ser.AtChecked(\"Type\") = \"" << TypeName << "\";" << std::endl;
        GeneratedFile << "        Ser.BeginObject(\"Value\");" << std::endl;
        GeneratedFile << "        SerializeFields(Ser, std::any_cast<" << TypeName << ">(Val.GetAny()));" << std::endl;
        GeneratedFile << "        Ser.EndObject();" << std::endl;
        GeneratedFile << "        return;" << std::endl;
        GeneratedFile << "    }" << std::endl;
    }
    GeneratedFile << "    throw std::runtime_error(\"Unsupported type \" + std::string(Val.GetAny().type().name()));" << std::endl;
    GeneratedFile << "}" << std::endl << std::endl;
    
    GeneratedFile << "void Serialize(Serializer& Ser, char const* Name, SubclassOfBase const& Val) {" << std::endl;
    GeneratedFile << "    Ser.BeginObject(Name);" << std::endl;
    GeneratedFile << "    SerializeFields(Ser, Val);" << std::endl;
    GeneratedFile << "    Ser.EndObject();" << std::endl;
    GeneratedFile << "}" << std::endl << std::endl;

    return GeneratedFile.str();
}

std::filesystem::path GetGeneratedPath(std::filesystem::path const& Path) {
    return Path.parent_path() / Path.filename().replace_extension(".gen.inl");
}

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

    std::set<std::string> AllNonTemplateTypes;
    std::map<std::string, GeneratorContext::CodeBlockGenerator> AllGeneratedBlocks;

    std::mutex SharedContextMut;

    // Does not generate a main file, just tries to place everything in inline functions
    // Does not support SubclassOf reflection when using this mode
    const bool InlineMode = Params.MainImpl.empty();

    // TODO: Support this feature
    if (InlineMode) {
        std::cerr << "Inline mode is not supported yet, you need to specify a main file with -M" << std::endl;
        return 1;
    }

    ParallelFor([&Params, &AllNonTemplateTypes, &AllGeneratedBlocks, &SharedContextMut, InlineMode](std::filesystem::path const& Path) {
        GeneratorContext Context;
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
                    std::cerr << "\033[31m" << "Header " << header << " does not exist" << "\033[0m" << std::endl;
                    continue;
                }
                if (OutputPath != header && std::filesystem::last_write_time(header) > OutputWriteTime) {
                    AnyNewer = true;
                    if (!Params.Silent) Log(Path, "Header " + header + " is newer than output");
                    break;
                }
            }
        }

        if (!AnyNewer && !Params.Silent) Log(Path, "No changes detected");

        if (AnyNewer) {
            std::filesystem::copy_file(
                std::filesystem::path(AR_RESOURCES_DIR) / "null_generated.h",
                OutputPath,
                std::filesystem::copy_options::overwrite_existing
            );
        }

        if (int Result = GenerateSingleFile(Context, Path, Params.IncludePaths, Params.Silent)) {
            std::cerr << "Failed to generate " << Path << std::endl;
            ++NumErrors;
            return;
        }

        if (AnyNewer) {
            std::ofstream GeneratedFile(OutputPath, std::ios::ate);

            GeneratedFile << "#pragma once" << std::endl << std::endl;
            GeneratedFile << "#include <AutoReflectDecls.hpp>" << std::endl << std::endl;

            // First, do all forward decls in the header
            for (auto const& kvp : Context.GeneratedBlocks) {
                if (Context.NonTemplateTypes.find(kvp.first) == Context.NonTemplateTypes.end()) {
                    continue; // Skip templates
                }
                GeneratedFile << kvp.second(InlineMode ? GeneratorContext::InlineMode : GeneratorContext::ForwardDeclMode) << std::endl;
            }

            // Then template impls only
            for (auto const& kvp : Context.GeneratedBlocks) {
                if (Context.NonTemplateTypes.find(kvp.first) != Context.NonTemplateTypes.end()) {
                    continue; // Skip non-templates
                }
                GeneratedFile << kvp.second(GeneratorContext::RegularMode) << std::endl;
            }

            GeneratedFile << std::ifstream(std::filesystem::path(AR_RESOURCES_DIR) / "BaseTemplateImpls.txt").rdbuf() << std::endl << std::endl;
        }

        if (!InlineMode) {
            std::lock_guard<std::mutex> Lock(SharedContextMut);
            // Merge all non-template types and generated blocks
            for (auto const& Type : Context.NonTemplateTypes) {
                auto found = AllNonTemplateTypes.find(Type);
                if (found != AllNonTemplateTypes.end() && *found != Type) {
                    std::cerr << "Type " << Type << " is already defined as " << *found << std::endl;
                    ++NumErrors;
                    continue;
                }
                
                AllNonTemplateTypes.insert(Type);
            }
            
            for (auto const& kvp : Context.GeneratedBlocks) {
                auto found = AllGeneratedBlocks.find(kvp.first);
                if (found != AllGeneratedBlocks.end() && found->second(GeneratorContext::RegularMode) != kvp.second(GeneratorContext::RegularMode)) {
                    std::cerr << "Type " << kvp.first << " has different generated code" << std::endl;
                    ++NumErrors;
                    continue;
                }

                AllGeneratedBlocks[kvp.first] = kvp.second;
            }
        }
    }, Params.FilesToParse);

    if (!InlineMode) {
        std::ofstream MainImplFile = std::ofstream(Params.MainImplOutput, std::ios::ate);
        
        MainImplFile << "// Base forward declarations" << std::endl;
        MainImplFile << "#include <AutoReflectDecls.hpp>" << std::endl << std::endl;

        MainImplFile << "// Type forward declarations" << std::endl;
        for (auto const& kvp : AllGeneratedBlocks) {
            MainImplFile << "// " << kvp.first << std::endl;
            MainImplFile << kvp.second(GeneratorContext::ForwardDeclMode) << std::endl;
        }

        MainImplFile << "// Base implementations" << std::endl;
        MainImplFile << std::ifstream(std::filesystem::path(AR_RESOURCES_DIR) / "BaseImpls.txt").rdbuf() << std::endl << std::endl;
        MainImplFile << "// Base template implementations" << std::endl;
        MainImplFile << std::ifstream(std::filesystem::path(AR_RESOURCES_DIR) / "BaseTemplateImpls.txt").rdbuf() << std::endl << std::endl;

        MainImplFile << "// std::any implementations" << std::endl;
        MainImplFile << GenDynamicReflectionImpl(AllNonTemplateTypes) << std::endl;

        MainImplFile << "// Type implementations" << std::endl;
        for (auto const& kvp : AllGeneratedBlocks) {
            MainImplFile << "// " << kvp.first << std::endl;
            MainImplFile << kvp.second(GeneratorContext::RegularMode) << std::endl;
        }
    }
    
    return 0;
}
