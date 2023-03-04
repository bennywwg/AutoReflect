#include <string>
#include <vector>
#include <regex>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <variant>
#include <filesystem>
#include <chrono>
#include <map>
#include <sstream>

const std::regex ClassRegex("class ([a-zA-Z0-9_]+) definition");
const std::regex TemplateTypeParmRegex("'?([a-zA-Z0-9_]+)?'? depth ([0-9]+) index ([0-9]+)( \\.\\.\\.)?( [a-zA-Z0-9_]+)?$");
const std::regex FieldRegex("([a-zA-Z0-9_]+) '([a-zA-Z0-9_:<>, \\*\\&\\[\\]]+)'");

std::vector<std::string> SplitForTemplate(std::string const& Val) {
    std::vector<std::string> result;
    result.reserve(5);
    std::stringstream ss (Val);
    std::string item;

    while (getline (ss, item, ' ')) {
        result.push_back (item);
    }

    return result;
}

bool GetTemplateParams(std::string const& Line, std::string& Type, std::string& Name, bool& HasType) {
    auto Sections = SplitForTemplate(Line);
    int NumDots = 0;
    int NumVars = 0;
    if (Sections.back()[0] >= '0' && Sections.back()[0] <= '9') {
        Name = "";
    } else {
        NumVars = 1;
        Name = Sections.back();
    }
    if (Sections[Sections.size() - 1 - NumVars][0] == '.') {
        NumDots = 1;
    }
    
    Type = Sections[Sections.size() - NumVars - NumDots - 5];
    return true;
}

std::map<int, std::chrono::high_resolution_clock::duration> Times;

struct ScopeTimer;

std::vector<ScopeTimer*> TimerStack;

struct ScopeTimer {
    std::chrono::high_resolution_clock::time_point Begin;
    const int Line;
    std::chrono::high_resolution_clock::duration TotalTime;
    ScopeTimer(int Line)
    : Line(Line)
    , Begin(std::chrono::high_resolution_clock::now())
    , TotalTime(0)
    {
        if (!TimerStack.empty()) {
            TimerStack.back()->Pause();
        }
        TimerStack.push_back(this);
    }
    
    ~ScopeTimer() {
        Pause();
        Times[Line] += TotalTime;
        TimerStack.pop_back();
        if (!TimerStack.empty()) {
            TimerStack.back()->Resume();
        }
    }
    
    void Pause() {
        TotalTime += std::chrono::high_resolution_clock::now() - Begin;
    }
    
    void Resume() {
        Begin = std::chrono::high_resolution_clock::now();
    }
};

//#define SCOPE_TIMER ScopeTimer T##__LINE__(__LINE__);
#define SCOPE_TIMER

void PrintTabbed(std::string const& Str, int TabCount) {
    return;
    for (int i = 0; i < TabCount; ++i) {
        std::cout << "    ";
    }
    std::cout << Str;
}

template<typename T>
using Opt = std::optional<T>;

template<typename T>
void ClangASTLinesPiped(std::string ParsePath, T const& Func) {
    std::string ClangASTCommand =
    "clang -std=c++20 -Xclang -ast-dump -fsyntax-only -fno-color-diagnostics -I" + std::string(AR_INCLUDE_DIR) + " " + ParsePath + " 2>/dev/null";

    std::cout << "Clang command: " << ClangASTCommand << std::endl;

    std::unique_ptr<FILE, decltype(&pclose)> Pipe(popen(ClangASTCommand.c_str(), "r"), pclose);
    if (!Pipe) {
        throw std::runtime_error("popen() failed!");
    }

    char *LineData = nullptr;
    size_t LineSize = 0;

    while (getline(&LineData, &LineSize, Pipe.get()) > 0) {
        size_t Len = strlen(LineData);

        LineData[Len - 1] = 0;
        Func(LineData, Len);
    }
}

/*
template<typename T, size_t N>
void ClangASTLinesCached(std::string ParsePath, T const& Func) {
    {
        std::string ClangASTCommand = "clang -std=c++20 -Xclang -ast-dump -fsyntax-only -fno-color-diagnostics " + ParsePath + "> cast_cache.txt 2>/dev/null";
    
        std::unique_ptr<FILE, decltype(&pclose)> Pipe(popen(ClangASTCommand.c_str(), "r"), pclose);
        if (!Pipe) {
            throw std::runtime_error("popen() failed!");
        }
    }

    // Silly technique: Split the file into N chunks
    std::ifstream File("cast_cache.txt");
    size_t Begins[N] = { 0 };
    const size_t FileSize = std::filesystem::file_size("cast_cache.txt");
    size_t ChunkSize = FileSize / N;
    if (ChunkSize <= 40) {
        // Process the whole file in one go
    }
    ChunkSize -= 40; // 40 is approx size of a line, any value would work
    for (size_t i = 1; i < N; ++i) {
        Begins[i] = Begins[i - 1] + ChunkSize;
        // fseek to that position, then advance until we hit a newline
        File.seekg(Begins[i]);
        char c;
        while (File.get(c)) {
            if (c == '\n') {
                break;
            }
            ++Begins[i];
        }
    }

    if (Begins[N - 1] != FileSize) {
        throw std::runtime_error("Something went wrong");
    }

    size_t NumLines = 0;
    // Read all the lines of cast_cache.txt
    std::ifstream File("cast_cache.txt");
    std::string Line;
    while (std::getline(File, Line)) {
        Func(Line);

        ++NumLines;
        if (NumLines % 1000 == 0 || NumLines >= 314000) {
            std::cout << "Parsed " << NumLines << " lines" << std::endl;
        }
    }

    // Delete cached file
    std::filesystem::remove("cast_cache.txt");
}

template<typename T>
void ClangASTLines(std::string ParsePath, T const& Func) {
    ClangASTLinesCached<T, 8>(ParsePath, Func);
}*/

// Stuff for keeping track of templates
// Ok... this isn't perfectly structured, one of Name could be eliminated its such a minor issue its not worth restructuring
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

struct ASTNode;
using ASTPtr = std::shared_ptr<ASTNode>;
using NodeFunc = std::function<void(ASTPtr)>;

enum class TagType {
    INVALID,
    FieldDecl,
    CXXRecordDecl,
    NamespaceDecl,
    ClassTemplateDecl,
    TemplateTypeParmDecl,
    NonTypeTemplateParmDecl,
    TemplateTemplateParmDecl,
    Public,
    Private,
    TranslationUnitDecl
};

// A slightly higher level representation of the AST generated by clang
struct ASTNode {
    int Indent = -1;
    TagType Tag;
    std::string Line;

    std::shared_ptr<ASTNode> Parent;
    std::vector<std::shared_ptr<ASTNode>> Children;
};


// Remove the node from its parent's children and set its parent to nullptr
// Also recursively remove all children
void DeleteNode(ASTPtr Node) {
    for (auto const& Child : Node->Children) {
        DeleteNode(Child);
    }

    // Find the node in the parent's children
    auto const& Parent = Node->Parent;
    auto const& Children = Parent->Children;
    auto const& It = std::find(Children.begin(), Children.end(), Node);
    if (It == Children.end()) {
        throw std::runtime_error("Node not found in parent's children");
    }

    // Remove the node from the parent's children
    Parent->Children.erase(It);
    Node->Parent = nullptr;
}

bool StartsWith(uint32_t& OutSize, char const* Line, size_t LineSize, char const* Match, uint32_t const& MatchSize) {
    if (LineSize <= MatchSize) return false;
    for (size_t i = 0; i < MatchSize; ++i) if (Line[i] != Match[i]) return false;
    if (Line[MatchSize] == ' ') {
        OutSize = MatchSize + 1;
        return true;
    }
    return false;
}

TagType BeginsWithValidTag(char const* Line, size_t LineSize, uint32_t& End) {
    //(TranslationUnitDecl|NamespaceDecl|CXXRecordDecl|FieldDecl|public|private|ClassTemplateDecl|TemplateTypeParmDecl|NonTypeTemplateParmDecl|TemplateTemplateParmDecl)
    if (StartsWith(End, Line, LineSize, "FieldDecl", 9)) return TagType::FieldDecl;
    if (StartsWith(End, Line, LineSize, "CXXRecordDecl", 13)) return TagType::CXXRecordDecl;
    if (StartsWith(End, Line, LineSize, "NamespaceDecl", 13)) return TagType::NamespaceDecl;
    if (StartsWith(End, Line, LineSize, "ClassTemplateDecl", 17)) return TagType::ClassTemplateDecl;
    if (StartsWith(End, Line, LineSize, "TemplateTypeParmDecl", 20)) return TagType::TemplateTypeParmDecl;
    if (StartsWith(End, Line, LineSize, "NonTypeTemplateParmDecl", 23)) return TagType::NonTypeTemplateParmDecl;
    if (StartsWith(End, Line, LineSize, "TemplateTemplateParmDecl", 24)) return TagType::TemplateTemplateParmDecl;
    if (StartsWith(End, Line, LineSize, "public", 6)) return TagType::Public;
    if (StartsWith(End, Line, LineSize, "private", 7)) return TagType::Private;
    if (StartsWith(End, Line, LineSize, "TranslationUnitDecl", 19)) return TagType::TranslationUnitDecl;
    return TagType::INVALID;
}

ASTPtr LoadASTNodes(std::string const& ASTFile) {
    const ASTPtr Root = std::make_shared<ASTNode>();
    Root->Indent = 0;
    ASTPtr CurrentScope = Root;
    
    std::ofstream CacheFile = std::ofstream("cache.txt", std::ios::out | std::ios::trunc);

    ClangASTLinesPiped(ASTFile, [&](char const* CurrentLine, size_t LineSize) {
        CacheFile << CurrentLine << std::endl;
        
        size_t Indent = 0;
        char c = CurrentLine[0];
        if (c == '-' || c == '|' || c == ' ' || c == '`') {
            while (c == '-' || c == '|' || c == ' ' || c == '`') {
                c = CurrentLine[Indent++];
            }
            --Indent;
        }

        // Pop if we are at a lower or equal indent level
        while (Indent <= CurrentScope->Indent && CurrentScope != Root) {
            CurrentScope = CurrentScope->Parent;
        }

        ASTPtr ToAdd = std::make_shared<ASTNode>();
        ToAdd->Parent = CurrentScope;
        ToAdd->Indent = static_cast<int>(Indent);

        uint32_t OutSize;
        TagType Tag = BeginsWithValidTag(CurrentLine + Indent, LineSize - Indent, OutSize);
        if (Tag != TagType::INVALID) {
            ToAdd->Tag = Tag;
            ToAdd->Line = CurrentLine + Indent + OutSize;
            CurrentScope->Children.push_back(ToAdd);
        }

        CurrentScope = ToAdd;
    });

    if (Root->Children.size() < 1) return nullptr;
    Root->Children[0]->Parent = nullptr;
    return Root->Children[0];
}

class NodeScanContext {
public:
    ASTPtr Root;

    std::vector<Template> TemplateStack;
    std::vector<std::string> NameStack;

    std::string GeneratedSource;

    std::vector<std::string> Errors;

    std::string GetFullyQualifiedName() {
        if (TemplateStack.empty() && NameStack.empty()) return "";

        std::string FullyQualified;
        for (auto const& Name : NameStack) {
            FullyQualified += Name + "::";
        }
        if (!FullyQualified.empty()) FullyQualified.erase(FullyQualified.end() - 2, FullyQualified.end());

        return FullyQualified;
    }

    Template GetFlattenedTemplates() {
        if (TemplateStack.empty()) return Template { };

        Template Flattened;
        for (auto const& Template : TemplateStack) {
            for (auto const& Param : Template.Params) {
                Flattened.Params.push_back(Param);
            }
        }

        return Flattened;
    }

    struct ClassDefinition {
        std::string LocalClassName;
    };
    Opt<ClassDefinition> GetAsClassDefinition(ASTPtr Node) {
        SCOPE_TIMER

        if (Node->Tag == TagType::CXXRecordDecl && (Node->Line.find("implicit") == std::string::npos)) {
            
            std::smatch ClassMatch;
            if (std::regex_search(Node->Line, ClassMatch, ClassRegex)) {
                //std::cout << "Found class " << ClassMatch[1] << std::endl;
                return ClassDefinition{ ClassMatch[1] };
            }
        }

        return std::nullopt;
    }

    struct NamespaceDefinition {
        std::string LocalNamespaceName;
    };
    Opt<NamespaceDefinition> GetAsNamespaceDefinition(ASTPtr Node) {
        SCOPE_TIMER
        if (Node->Tag == TagType::NamespaceDecl) {
            // The namespace name is the last word in the line
            size_t i;
            for (i = Node->Line.size() - 1; i >= 0; --i) {
                if (Node->Line[i] == ' ') break;
            }
            
            return NamespaceDefinition { Node->Line.substr(i) };
        }

        return std::nullopt;
    }

    struct TemplateDefinition {
        Template T;
    };
    Opt<TemplateDefinition> GetAsTemplateDefinition(ASTPtr Node, TemplatePtr ParentTemplate = nullptr) {
        SCOPE_TIMER
        //`-ClassTemplateDecl 0x1309055e0 <line:15:1, line:18:1> line:16:7 B3
        //|-TemplateTemplateParmDecl 0x130905430 <line:15:10, col:63> col:63 depth 0 index 0 T2
        //| |-TemplateTemplateParmDecl 0x130905338 <col:19, col:45> col:45 depth 1 index 0
        //| | `-TemplateTypeParmDecl 0x130905268 <col:28, col:34> col:34 class depth 2 index 0 U
        //| `-NonTypeTemplateParmDecl 0x1309053b0 <col:47, col:51> col:51 'int' depth 1 index 1 E
        //|-TemplateTypeParmDecl 0x130905490 <col:67, col:76> col:76 typename depth 0 index 1 E2
        
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
    Opt<FieldDefinition> GetAsField(ASTPtr Node) {
        SCOPE_TIMER
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
        SCOPE_TIMER
        auto TemplateDef = GetAsTemplateDefinition(Node);
        if (!TemplateDef) {
            Errors.push_back("Internal error, could not find template definition");
            return;
        }
        PrintTabbed(TemplateDef->T.Generate() + "\n", Indent);

        TemplateStack.push_back(TemplateDef->T);
        ASTPtr ClassNode = Node->Children.back();
        if (GetAsClassDefinition(ClassNode)) {
            GenerateClass(ClassNode, Indent, Generating);
        }
        TemplateStack.pop_back();
    }

    void GenerateClass(ASTPtr Node, int Indent, bool Generating) {
        SCOPE_TIMER
        auto ClassDef = GetAsClassDefinition(Node);
        if (!ClassDef) {
            throw std::runtime_error("Could not find class definition");
        }

        PrintTabbed("class " + ClassDef->LocalClassName + " {\n", Indent);
        NameStack.push_back(ClassDef->LocalClassName);

        Template Flattened = GetFlattenedTemplates();
        std::string Templates = Flattened.Generate();
        std::string FullyQualified = GetFullyQualifiedName();
        std::string SerializeFieldsSource, DeserializeFieldsSource;

        bool FoundAutoReflect = false;
        for (size_t i = 0; i < Node->Children.size(); ++i) {
            auto Child = Node->Children[i];
            
            if (Opt<FieldDefinition> FieldDef = GetAsField(Child)) {
                FieldDefinition FD = *FieldDef;
                PrintTabbed(FD.TypeName + " " + FD.VarName + ";\n", Indent + 1);

                SerializeFieldsSource += "    Serialize(Ser, \"" + FD.VarName + "\", Val." + FD.VarName + ");\n";
                DeserializeFieldsSource += "    Deserialize(Ser, \"" + FD.VarName + "\", Val." + FD.VarName + ");\n";
            } else if ((Child->Tag == TagType::Private || Child->Tag == TagType::Public) && Child->Line == "'AutoReflect'") {
                FoundAutoReflect = true;
            }
        }

        GenerateScope(Node, Indent + 1, Generating);

        NameStack.pop_back();
        PrintTabbed("};\n", Indent);

        if (FoundAutoReflect && TemplateStack.size() > 1) {
            Errors.push_back("Nested templates are not supported");
            Generating = false;
        }

        if (FoundAutoReflect && Generating) {
            if (!Templates.empty()) GeneratedSource += Templates + "\n";
            GeneratedSource += "inline void SerializeFields(Serializer& Ser, " + FullyQualified + Flattened.GenerateNames() + " const& Val) {\n";
            GeneratedSource += SerializeFieldsSource;
            GeneratedSource += "}\n\n";

            if (!Templates.empty()) GeneratedSource += Templates + "\n";
            GeneratedSource += "inline void DeserializeFields(Deserializer& Ser, " + FullyQualified + Flattened.GenerateNames() + "& Val) {\n";
            GeneratedSource += DeserializeFieldsSource;
            GeneratedSource += "}\n\n";

            if (!Templates.empty()) GeneratedSource += Templates + "\n";
            GeneratedSource += "inline void Serialize(Serializer& Ser, char const* Name, " + FullyQualified + Flattened.GenerateNames() + " const& Val) {\n";
            GeneratedSource += "    BeginObject(Ser, Name);\n";
            GeneratedSource += "    SerializeFields(Ser, Val);\n";
            GeneratedSource += "    EndObject(Ser);\n";
            GeneratedSource += "}\n\n";

            if (!Templates.empty()) GeneratedSource += Templates + "\n";
            GeneratedSource += "inline void Deserialize(Deserializer& Ser, char const* Name, " + FullyQualified + Flattened.GenerateNames() + "& Val) {\n";
            GeneratedSource += "    BeginObject(Ser, Name);\n";
            GeneratedSource += "    DeserializeFields(Ser, Val);\n";
            GeneratedSource += "    EndObject(Ser);\n";
            GeneratedSource += "}\n\n";
        }
    }

    // Works for classes, namespaces, translation units, with special behavior handled by the specific function
    void GenerateScope(ASTPtr Node, int Indent, bool Generating) {
        SCOPE_TIMER

        // Assert if tag isn't a scope
        if (Node->Tag != TagType::TranslationUnitDecl && Node->Tag != TagType::NamespaceDecl && Node->Tag != TagType::ClassTemplateDecl && Node->Tag != TagType::CXXRecordDecl) {
            throw std::runtime_error("Tag is not a scope, is " + std::to_string(static_cast<int>(Node->Tag)));
        }

        for (size_t i = 0; i < Node->Children.size(); ++i) {
            auto Child = Node->Children[i];
            
            if (Opt<TemplateDefinition> TemplateDef = GetAsTemplateDefinition(Child)) {
                GenerateTemplate(Child, Indent, Generating);
            } else if (Opt<ClassDefinition> ClassDef = GetAsClassDefinition(Child)) {
                if (Child->Line.find("implicit") != std::string::npos) {
                    continue;
                }
                GenerateClass(Child, Indent + 1, Generating);
            } else if (Opt<NamespaceDefinition> NamespaceDef = GetAsNamespaceDefinition(Child)) {
                PrintTabbed("namespace " + NamespaceDef->LocalNamespaceName + " {\n", Indent);
                NameStack.push_back(NamespaceDef->LocalNamespaceName);
                GenerateScope(Child, Indent + 1, Generating);
                NameStack.pop_back();
                PrintTabbed("}\n", Indent);
            }
        }
    }
};

int main(int argc, char** argv) {
    if (false)
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }

    std::string Filename = argc < 2 ? std::string("/Users/bennywwg/Build/sertest/ReflectionTest/Source/ReflectionTest.cpp") : std::string(argv[1]);
    
    std::filesystem::path ResourcePath = AR_RESOURCES_DIR;

    std::string GeneratedFilename = Filename.substr(0, Filename.find_last_of('.')) + ".generated.inl";

    std::filesystem::copy_file(ResourcePath / "null_generated.inl", GeneratedFilename, std::filesystem::copy_options::overwrite_existing);

    std::cout << "Generating " << GeneratedFilename << "..." << std::endl;
    std::chrono::steady_clock::time_point ParseBegin = std::chrono::steady_clock::now();

    NodeScanContext Context;
    ASTPtr Root = LoadASTNodes(Filename);

    std::chrono::steady_clock::time_point ParseEnd = std::chrono::steady_clock::now();
    std::cout << "Parsed in " << std::chrono::duration_cast<std::chrono::milliseconds>(ParseEnd - ParseBegin).count() << "ms" << std::endl;

    if (!Root) {
        std::cerr << "Failed to load AST nodes (internal error)" << std::endl;
        return 1;
    }
    
    std::chrono::steady_clock::time_point Begin = std::chrono::steady_clock::now();
    
    try {
        Context.GenerateScope(Root, 0, true);
    } catch (std::runtime_error const& Er) {
        std::cerr << "Failed during generation: " << Er.what() << std::endl;
        return 1;
    }

    std::ofstream GeneratedFile(GeneratedFilename, std::ios::ate);

    GeneratedFile << "#pragma once" << std::endl << std::endl;
    GeneratedFile << "#include <SerializeBaseImpl.hpp>" << std::endl << std::endl;
    GeneratedFile << Context.GeneratedSource;

    for (std::string const& Error : Context.Errors) {
        std::cerr << Error << std::endl;
    }

    std::cout << "Generated source of " << Context.GeneratedSource.size() << " bytes" << std::endl;
    std::chrono::steady_clock::time_point End = std::chrono::steady_clock::now();
    std::cout << "Generated in " << std::chrono::duration_cast<std::chrono::milliseconds>(End - Begin).count() << "ms" << std::endl;
    
    std::cout << "Times:" << std::endl;
    for (auto kvp : Times) {
        std::cout << kvp.first << ": " << std::chrono::duration_cast<std::chrono::milliseconds>(kvp.second).count() << std::endl;
    }

    return 0;
}
