#pragma once

#include "Utilities.hpp"

#include <variant>
#include <optional>

struct ImplementationGenerator {
    std::string Templates;
    std::string FullTypeName;
    std::string SerializeFieldsSource;
    std::string DeserializeFieldsSource;

    bool operator==(ImplementationGenerator const& Other) const;
    bool operator!=(ImplementationGenerator const& Other) const;

    std::string Generate(GenMode Mode) const;
};

struct ImplementationGeneratorSet {
    std::map<std::string, ImplementationGenerator> Generators;
    std::set<std::string> NonTemplateTypes;

    // Adds all generators from other, and returns a list of mismatched generators
    // Mismatched generators can allow generation to continue but should ultimately cause a failure
    std::vector<std::string> Combine(ImplementationGeneratorSet const& Other);

    std::string GenDynamicReflectionImpl() const;
};

void SaveCachedGenerator(std::string const& Filepath, ImplementationGeneratorSet const& Generator);
std::optional<ImplementationGeneratorSet> GetCachedGenerator(std::filesystem::path const& FilePath);

struct KindOrType {
    std::string KindOrTypeName;
    std::string Name;
};

struct Template;
using TemplatePtr = std::shared_ptr<Template>;
using TemplateParam = std::variant<KindOrType, TemplatePtr>;

struct Template {
    std::vector<TemplateParam> Params;
    std::string Name;

    std::string Generate(bool IsOuter = true) const;
    std::string GenerateNames() const;
};
