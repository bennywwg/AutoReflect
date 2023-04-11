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

#ifndef _WIN32 
#define POPEN popen
#define PCLOSE pclose
#else
#define POPEN _popen
#define PCLOSE _pclose
#endif

#define GeneratedSuffix ".gen.inl"

const std::regex ClassRegex("class ([a-zA-Z0-9_]+) definition");
const std::regex FieldRegex("([a-zA-Z0-9_]+) '([a-zA-Z0-9_:<>, \\*\\&\\[\\]]+)'");

std::mutex Mutex;

void Log(std::filesystem::path const& Task, std::string const& Str) {
    std::lock_guard<std::mutex> Lock(Mutex);
    std::cout << "AutoReflect " << Task << ": " << Str << std::endl;
}

struct CallOnDtor {
    std::function<void()> Func;
    CallOnDtor(std::function<void()> Func)
    : Func(Func)
    {}
    ~CallOnDtor() {
        Func();
    }
    CallOnDtor(CallOnDtor const&) = delete;
    CallOnDtor(CallOnDtor&&) = delete;
    CallOnDtor& operator=(CallOnDtor const&) = delete;
    CallOnDtor& operator=(CallOnDtor&&) = delete;
};

template<typename F, typename T>
void ParallelFor(F Func, std::vector<T> const& Inputs) {
    std::vector<std::thread> Threads;
    Threads.resize(std::thread::hardware_concurrency());
    std::vector<std::atomic<int>*> ThreadStates; // 0 = fresh, 1 = done, 2 = running
    for (size_t i = 0; i < Threads.size(); ++i) ThreadStates.push_back(new std::atomic<int>(0));

    for (size_t i = 0; i < Inputs.size(); ++i) {
        bool Found = false;
        for (size_t ThreadIndex = 0; ThreadIndex < Threads.size(); ++ThreadIndex) {
            if (ThreadStates[ThreadIndex]->load() <= 1) {
                if (ThreadStates[ThreadIndex]->load() == 1) Threads[ThreadIndex].join();
                ThreadStates[ThreadIndex]->store(2);
                Threads[ThreadIndex] = std::thread([Func, &ThreadStates, ThreadIndex, &Inputs, i](){
                    CallOnDtor OnDtor([&ThreadStates, ThreadIndex](){
                        ThreadStates[ThreadIndex]->store(1);
                    });

                    Func(Inputs[i]);
                });
                Found = true;
                break;
            }
        }
        if (!Found) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Join any remaining threads
    for (size_t ThreadIndex = 0; ThreadIndex < Threads.size(); ++ThreadIndex) {
        if (ThreadStates[ThreadIndex]->load() != 0) {
            Threads[ThreadIndex].join();
        }
        delete ThreadStates[ThreadIndex];
    }
}

std::vector<std::string> SplitForTemplate(std::string const& Val) {
    std::vector<std::string> result;
    result.reserve(5);
    std::stringstream ss (Val);
    std::string item;

    while (std::getline (ss, item, ' ')) {
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

//std::map<int, std::chrono::high_resolution_clock::duration> Times;

//struct ScopeTimer;

//std::vector<ScopeTimer*> TimerStack;

/*
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
};*/

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
void ClangASTLinesPiped(std::string ParsePath, std::vector<std::filesystem::path> const& Includes, T const& Func, bool Silent) {
    std::string ClangASTCommand
#ifdef _WIN32
    = "cmd /c \"clang -std=c++20 -Xclang -ast-dump -fsyntax-only -fno-color-diagnostics -I"
    + std::string(AR_INCLUDE_DIR);
    
    for (auto const& Include : Includes) {
        ClangASTCommand += " -I\"" + Include.string() + "\"";
    }
    
    ClangASTCommand += " " + ParsePath
    + " 2>NUL\"";
#else
    = "clang -std=c++20 -Xclang -ast-dump -fsyntax-only -fno-color-diagnostics -I"
    + std::string(AR_INCLUDE_DIR);
    
    for (auto const& Include : Includes) {
        ClangASTCommand += " -I\"" + Include.string() + "\"";
    }

    ClangASTCommand += " " + ParsePath
    + " 2>/dev/null";
#endif

    if (!Silent) Log(ParsePath, "Clang command: " + ClangASTCommand);

    std::unique_ptr<FILE, decltype(&PCLOSE)> Pipe(POPEN(ClangASTCommand.c_str(), "r"), PCLOSE);
    if (!Pipe) {
        throw std::runtime_error("popen() failed!");
    }

    constexpr size_t LineSize = 1024 * 16;
    char* LineData = new char[LineSize];
    memset(LineData, 0, LineSize);
    while (fgets(LineData, LineSize, Pipe.get())) {
        size_t Len = strlen(LineData);

        LineData[Len - 1] = 0;
        Func(LineData, Len - 1);
    }
    delete[] LineData;
}

std::vector<std::string> GetAllHeaders(std::filesystem::path const& Path, std::vector<std::filesystem::path> const& Includes, bool Silent) {
    std::string GetHeadersCommand
#ifdef _WIN32
    = "cmd /c \"clang -std=c++20 -M";
    
    for (auto const& Include : Includes) {
        GetHeadersCommand += " -I\"" + Include.string() + "\"";
    }
    
    ClangASTCommand += " " + Path.string()
    + " 2>NUL\"";
#else
    = "clang -std=c++20 -M";
    
    for (auto const& Include : Includes) {
        GetHeadersCommand += " -I\"" + Include.string() + "\"";
    }

    GetHeadersCommand += " " + Path.string()
    + " 2>/dev/null";
#endif

    if (!Silent) Log(Path, "Clang command: " + GetHeadersCommand);

    std::unique_ptr<FILE, decltype(&PCLOSE)> Pipe(POPEN(GetHeadersCommand.c_str(), "r"), PCLOSE);
    if (!Pipe) {
        throw std::runtime_error("popen() failed!");
    }

    std::vector<std::string> Headers;

    constexpr size_t LineSize = 1024 * 16;
    char* LineData = new char[LineSize];
    memset(LineData, 0, LineSize);
    bool First = true;
    while (fgets(LineData, LineSize, Pipe.get())) {
        if (First || LineSize < 2) {
            First = false;
            continue;
        }
        std::string Line = LineData + 2;

        // May end with trailing space trailing backslash and newline
        Line.pop_back();
        if (Line.back() == '\\') {
            Line.pop_back();
            Line.pop_back();
        }

        // And apparently, spaces can also be delimiter not just newlines
        while (true) {
            // Find next space, take substring up to that, or the whole thing if no space
            size_t SpacePos = Line.find(' ');
            if (SpacePos == std::string::npos) {
                Headers.push_back(Line);
                break;
            } else {
                Headers.push_back(Line.substr(0, SpacePos));
                Line = Line.substr(SpacePos + 1);
            }
        }

        //Log(Path, "Header: " + std::string(LineData));
        Headers.push_back(Line);
    }
    delete[] LineData;

    return Headers;
}

// Stuff for keeping track of templates
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
    EnumDecl,
    TranslationUnitDecl
};

// A slightly higher level representation of the AST generated by clang
struct ASTNode {
    int Indent = -1;
    TagType Tag = TagType::INVALID;
    std::string Line;

    std::shared_ptr<ASTNode> Parent;
    std::vector<std::shared_ptr<ASTNode>> Children;
};


// Remove the node from its parent's children and set its parent to nullptr
// Also recursively remove all children
void DeleteNode(ASTPtr Node) {
    if (!Node) return;

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
    if (StartsWith(End, Line, LineSize, "FieldDecl", 9)) return TagType::FieldDecl;
    if (StartsWith(End, Line, LineSize, "CXXRecordDecl", 13)) return TagType::CXXRecordDecl;
    if (StartsWith(End, Line, LineSize, "NamespaceDecl", 13)) return TagType::NamespaceDecl;
    if (StartsWith(End, Line, LineSize, "ClassTemplateDecl", 17)) return TagType::ClassTemplateDecl;
    if (StartsWith(End, Line, LineSize, "TemplateTypeParmDecl", 20)) return TagType::TemplateTypeParmDecl;
    if (StartsWith(End, Line, LineSize, "NonTypeTemplateParmDecl", 23)) return TagType::NonTypeTemplateParmDecl;
    if (StartsWith(End, Line, LineSize, "TemplateTemplateParmDecl", 24)) return TagType::TemplateTemplateParmDecl;
    if (StartsWith(End, Line, LineSize, "public", 6)) return TagType::Public;
    if (StartsWith(End, Line, LineSize, "private", 7)) return TagType::Private;
    if (StartsWith(End, Line, LineSize, "EnumDecl", 8)) return TagType::EnumDecl;
    if (StartsWith(End, Line, LineSize, "TranslationUnitDecl", 19)) return TagType::TranslationUnitDecl;
    return TagType::INVALID;
}

ASTPtr LoadASTNodes(std::string const& ASTFile, std::vector<std::filesystem::path> const& Includes, bool Silent) {
    const ASTPtr Root = std::make_shared<ASTNode>();
    Root->Indent = 0;
    ASTPtr CurrentScope = Root;

    ClangASTLinesPiped(ASTFile, Includes, [&](char const* CurrentLine, size_t LineSize) {
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
        ToAdd->Tag = Tag;
        if (Tag != TagType::INVALID) {
            ToAdd->Line = CurrentLine + Indent + OutSize;
            CurrentScope->Children.push_back(ToAdd);
        }

        CurrentScope = ToAdd;
    }, Silent);

    if (Root->Children.size() < 1) return nullptr;
    Root->Children[0]->Parent = nullptr;
    return Root->Children[0];
}

class NodeScanContext {
private:
    std::vector<Template> TemplateStack;
    std::vector<std::string> NameStack;
    std::vector<std::string> Errors;
    std::map<std::string, std::string> EnumTypeMaps;
    int NumAutoReflectNamespaces = 0;
public:
    // The param is whether or not to generate the block as a forward declaration
    using CodeBlockGenerator = std::function<std::string(bool)>;

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
    Opt<EnumDefinition> GetAsEnumDefinition(ASTPtr Node) {
        // | |-EnumDecl 0x1388c5eb0 <line:8:5, line:13:5> line:8:16 class TheBlooper 'uint8_t':'unsigned char'
        SCOPE_TIMER
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
    Opt<ClassDefinition> GetAsClassDefinition(ASTPtr Node) {
        SCOPE_TIMER

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
    Opt<NamespaceDefinition> GetAsNamespaceDefinition(ASTPtr Node) {
        SCOPE_TIMER
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
        PrintTabbed("};\n", Indent);

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

            GeneratedBlocks[FullTypeName] = [Templates, FullTypeName, SerializeFieldsSource, DeserializeFieldsSource](bool IsForwardDecl) {
                std::string const Qualifier = !Templates.empty() ? "inline " : "";

                std::string GeneratedSource;

                if (IsForwardDecl) {
                    if (!Templates.empty()) GeneratedSource += Templates + "\n";
                    GeneratedSource += Qualifier + "void Serialize(Serializer& Ser, char const* Name, " + FullTypeName + " const& Val);\n";
                    GeneratedSource += Qualifier + "void Deserialize(Deserializer& Ser, char const* Name, " + FullTypeName + "& Val);\n";
                    GeneratedSource += Qualifier + "void SerializeFields(Serializer& Ser, " + FullTypeName + " const& Val);\n";
                    GeneratedSource += Qualifier + "void DeserializeFields(Deserializer& Ser, " + FullTypeName + "& Val);\n";
                } else {
                    if (!Templates.empty()) GeneratedSource += Templates + "\n";
                    GeneratedSource += Qualifier + "void SerializeFields(Serializer& Ser, " + FullTypeName + " const& Val) {\n";
                    GeneratedSource += SerializeFieldsSource;
                    GeneratedSource += "}\n\n";

                    if (!Templates.empty()) GeneratedSource += Templates + "\n";
                    GeneratedSource += Qualifier + "void DeserializeFields(Deserializer& Ser, " + FullTypeName + "& Val) {\n";
                    GeneratedSource += DeserializeFieldsSource;
                    GeneratedSource += "}\n\n";

                    if (!Templates.empty()) GeneratedSource += Templates + "\n";
                    GeneratedSource += Qualifier + "void Serialize(Serializer& Ser, char const* Name, " + FullTypeName + " const& Val) {\n";
                    GeneratedSource += "    Ser.BeginObject(Name);\n";
                    GeneratedSource += "    SerializeFields(Ser, Val);\n";
                    GeneratedSource += "    Ser.EndObject();\n";
                    GeneratedSource += "}\n\n";

                    if (!Templates.empty()) GeneratedSource += Templates + "\n";
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
                if (NamespaceDef->LocalNamespaceName == "AutoReflect") ++NumAutoReflectNamespaces;
                NameStack.push_back(NamespaceDef->LocalNamespaceName);
                GenerateScope(Child, Indent + 1, Generating);
                NameStack.pop_back();
                if (NamespaceDef->LocalNamespaceName == "AutoReflect") --NumAutoReflectNamespaces;
                PrintTabbed("}\n", Indent);
            } else if (Opt<EnumDefinition> EnumDef = GetAsEnumDefinition(Child)) {
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

int GenerateSingleFile(NodeScanContext& Context, std::filesystem::path const& Path, std::vector<std::filesystem::path> const& IncludePaths, bool Silent) {
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

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> IncludePaths; // TODO: Use this
    std::vector<std::filesystem::path> FilesToParse;
    std::filesystem::path MainImpl, MainImplOutput;

    bool Silent = false;

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
                    if (Line.find("#include") != std::string::npos && Line.find(GeneratedSuffix) != std::string::npos) {
                        FoundGenerated = true;
                        break;
                    }
                }
            }
            if (FoundGenerated) FilesToParse.push_back(Arg);
            if (FoundGenerated) {
                std::filesystem::path OutputPath = Arg;
                OutputPath += GeneratedSuffix;
            }
            else std::filesystem::remove(GetGeneratedPath(Arg));
        }
    }

    if (MainImpl.empty()) {
        std::cerr << "A main implementation file must be specified with -M" << std::endl;
        return 1;
    }

    if (FilesToParse.empty()) {
        FilesToParse.push_back("../Source/AutoReflectTest.cpp");
        FilesToParse.push_back("../Source/AdditionalCpp.cpp");
    }

    std::set<std::string> AllNonTemplateTypes;
    std::map<std::string, NodeScanContext::CodeBlockGenerator> AllGeneratedBlocks;

    std::mutex SharedContextMut;

    //for (std::filesystem::path const& Path : FilesToParse) {
    ParallelFor([Silent, IncludePaths, MainImplOutput, &AllNonTemplateTypes, &AllGeneratedBlocks, &SharedContextMut](std::filesystem::path const& Path) {
        NodeScanContext Context;
        int NumErrors = 0;

        // Convert input name to ouput, ie Foo.cpp -> Foo.generated
        std::filesystem::path OutputPath = Path;
        OutputPath += GeneratedSuffix;

        if (OutputPath == MainImplOutput) return;

        // Get write date of output file
        auto const OutputWriteTime = std::filesystem::exists(OutputPath) ? std::filesystem::last_write_time(OutputPath) : std::filesystem::file_time_type::min();
        auto const InputWriteTime = std::filesystem::last_write_time(Path);
        
        bool AnyNewer = false;
        if (InputWriteTime > OutputWriteTime) {
            AnyNewer = true;
            if (!Silent) Log(Path, "Input is newer than output");
        } else {
            for (auto header : GetAllHeaders(Path, IncludePaths, Silent)) {
                if (!std::filesystem::exists(header)) {
                    // Print in red
                    std::cerr << "\033[31m" << "Header " << header << " does not exist" << "\033[0m" << std::endl;
                    continue;
                }
                if (OutputPath != header && std::filesystem::last_write_time(header) > OutputWriteTime) {
                    AnyNewer = true;
                    if (!Silent) Log(Path, "Header " + header + " is newer than output");
                    break;
                }
            }
        }

        if (!AnyNewer && !Silent) Log(Path, "No changes detected");

        if (AnyNewer) {
            std::filesystem::copy_file(
                std::filesystem::path(AR_RESOURCES_DIR) / "null_generated.h",
                OutputPath,
                std::filesystem::copy_options::overwrite_existing
            );
        }

        if (int Result = GenerateSingleFile(Context, Path, IncludePaths, Silent)) {
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
                GeneratedFile << kvp.second(true) << std::endl;
            }
        }

        {
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
                if (found != AllGeneratedBlocks.end() && found->second(false) != kvp.second(false)) {
                    std::cerr << "Type " << kvp.first << " has different generated code" << std::endl;
                    ++NumErrors;
                    continue;
                }

                AllGeneratedBlocks[kvp.first] = kvp.second;
            }
        }
    }, FilesToParse);
    //}

    std::filesystem::path AllIncludesOutput = MainImpl;
    {
        AllIncludesOutput += ".gen.hpp";
        std::ofstream AllIncludesFile = std::ofstream(AllIncludesOutput, std::ios::ate);
        AllIncludesFile << "#pragma once" << std::endl << std::endl;
        for (std::filesystem::path const& ToParse : FilesToParse) {
            std::filesystem:: path OutputPath = ToParse;
            OutputPath += GeneratedSuffix;
            if (OutputPath == MainImplOutput) continue;

            // Get relative to main impl
            OutputPath = std::filesystem::relative(OutputPath, std::filesystem::path(AllIncludesOutput).parent_path());

            AllIncludesFile << "#include \"" << OutputPath.string() << "\"" << std::endl;
        }
    }

    std::ofstream MainImplFile = std::ofstream(MainImplOutput, std::ios::ate);
    
    MainImplFile << "// Forward declarations" << std::endl;
    MainImplFile << "#include \"" << AllIncludesOutput.filename().string() << "\"" << std::endl << std::endl;

    MainImplFile << "// Base implementations" << std::endl;
    MainImplFile << std::ifstream(std::filesystem::path(AR_RESOURCES_DIR) / "BaseImpls.txt").rdbuf() << std::endl << std::endl;

    MainImplFile << "// std::any implementations" << std::endl;
    MainImplFile << GenDynamicReflectionImpl(AllNonTemplateTypes) << std::endl;

    MainImplFile << "// Type implementations" << std::endl;
    for (auto const& kvp : AllGeneratedBlocks) {
        MainImplFile << "// " << kvp.first << std::endl;
        MainImplFile << kvp.second(false) << std::endl;
    }
    
    return 0;
}
