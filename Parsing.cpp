#include "Parsing.hpp"

#include <sstream>

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

void ClangASTLinesPiped(std::string ParsePath, std::vector<std::filesystem::path> const& Includes, std::function<void(const char*, size_t)> const& Func, bool Silent) {
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
