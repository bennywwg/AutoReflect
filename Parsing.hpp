#pragma once

#include "Utilities.hpp"

#include <regex>
#include <vector>
#include <filesystem>

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

struct ASTNode;
using ASTPtr = std::shared_ptr<ASTNode>;

struct ASTNode {
    int Indent = -1;
    TagType Tag = TagType::INVALID;
    std::string Line;

    std::shared_ptr<ASTNode> Parent;
    std::vector<std::shared_ptr<ASTNode>> Children;
};

std::vector<std::string> SplitForTemplate(std::string const& Val);

bool GetTemplateParams(std::string const& Line, std::string& Type, std::string& Name, bool& HasType);

std::vector<std::string> GetAllHeaders(std::filesystem::path const& Path, std::vector<std::filesystem::path> const& Includes, bool Silent);

void DeleteNode(ASTPtr Node);

bool StartsWith(uint32_t& OutSize, char const* Line, size_t LineSize, char const* Match, uint32_t const& MatchSize);

TagType BeginsWithValidTag(char const* Line, size_t LineSize, uint32_t& End);

ASTPtr LoadASTNodes(std::filesystem::path const& ASTFile, std::vector<std::filesystem::path> const& Includes, bool Silent);

void ClangASTLinesPiped(std::filesystem::path const& ParsePath, std::vector<std::filesystem::path> const& Includes, std::function<void(const char*, size_t)> const& Func, bool Silent);
