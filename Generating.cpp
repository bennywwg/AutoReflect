#include "Generating.hpp"

#include <fstream>
#include <sstream>

bool ImplementationGenerator::operator==(ImplementationGenerator const& Other) const {
    return
        Templates == Other.Templates &&
        FullTypeName == Other.FullTypeName &&
        SerializeFieldsSource == Other.SerializeFieldsSource &&
        DeserializeFieldsSource == Other.DeserializeFieldsSource;
}

bool ImplementationGenerator::operator!=(ImplementationGenerator const& Other) const {
    return !(*this == Other);
}

std::string ImplementationGenerator::Generate(GenMode Mode) const {
    std::string Qualifier = (!Templates.empty() || Mode == GenMode::InlineMode) ? "inline " : "";

    if (!Templates.empty()) {
        Qualifier = Templates + "\n" + Qualifier;
    }

    std::string GeneratedSource;

    if (Mode == GenMode::ForwardDeclMode) {
        GeneratedSource += Qualifier + "void Serialize(Serializer& Ser, char const* Name, " + FullTypeName + " const& Val);\n";
        GeneratedSource += Qualifier + "void Deserialize(Deserializer& Ser, char const* Name, " + FullTypeName + "& Val);\n";
        GeneratedSource += Qualifier + "void SerializeFields(Serializer& Ser, " + FullTypeName + " const& Val);\n";
        GeneratedSource += Qualifier + "void DeserializeFields(Deserializer& Ser, " + FullTypeName + "& Val);\n";
    } else {
        // Convert to a macro to avoid multiple definitions, replace :,<,>,, with _
        std::string MacroName = FullTypeName;
        std::replace(MacroName.begin(), MacroName.end(), ':', '_');
        std::replace(MacroName.begin(), MacroName.end(), '<', '_');
        std::replace(MacroName.begin(), MacroName.end(), '>', '_');
        std::replace(MacroName.begin(), MacroName.end(), ',', '_');

        GeneratedSource += "#ifndef " + MacroName + "_IMPL\n";
        GeneratedSource += "#define " + MacroName + "_IMPL\n\n";

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

        GeneratedSource += "#endif // " + MacroName + "\n";
    }

    return GeneratedSource;
}

std::vector<std::string> ImplementationGeneratorSet::Combine(ImplementationGeneratorSet const& Other) {
    std::vector<std::string> Errors;

    for (auto const& Type : Other.NonTemplateTypes) {
        NonTemplateTypes.insert(Type);
    }
    
    for (auto const& kvp : Other.Generators) {
        auto found = Generators.find(kvp.first);
        if (found != Generators.end()) {
            if (found->second != kvp.second) {
                Errors.push_back(kvp.first);
            }
            continue;
        }

        Generators[kvp.first] = kvp.second;
    }

    return Errors;
}

// TODO: Use an acceleration structure instead of just if statements
std::string ImplementationGeneratorSet::GenDynamicReflectionImpl() const {
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

void SaveCachedGenerator(std::string const& Filepath, ImplementationGeneratorSet const& Generator) {
    nlohmann::json J;

    nlohmann::json& GeneratorsJSON = J["Generators"];
    for (auto kvp : Generator.Generators) {
        GeneratorsJSON[kvp.first]["Templates"] = kvp.second.Templates;
        GeneratorsJSON[kvp.first]["FullTypeName"] = kvp.second.FullTypeName;
        GeneratorsJSON[kvp.first]["SerializeFieldsSource"] = kvp.second.SerializeFieldsSource;
        GeneratorsJSON[kvp.first]["DeserializeFieldsSource"] = kvp.second.DeserializeFieldsSource;
    }

    J["NonTemplateTypes"] = Generator.NonTemplateTypes;

    std::filesystem::create_directory(".AutoSerialize");

    std::ofstream Serialized(std::filesystem::path(".AutoSerialize") / Filepath, std::ios::ate);
    Serialized << J.dump();
}

std::optional<ImplementationGeneratorSet> GetCachedGenerator(std::filesystem::path const& FilePath) {
    const std::filesystem::path CachedPath = std::filesystem::path(".AutoSerialize") / FilePath;

    if (!std::filesystem::exists(CachedPath))
        return std::nullopt;

    std::ifstream Cached(CachedPath);
    std::string CachedStr((std::istreambuf_iterator<char>(Cached)), std::istreambuf_iterator<char>());

    ImplementationGeneratorSet Res;

    nlohmann::json J = nlohmann::json::parse(CachedStr);
    
    for (const auto& [key, value] : J["Generators"].items()) {
        ImplementationGenerator IG;
        IG.Templates = value["Templates"].get<std::string>();
        IG.FullTypeName = value["FullTypeName"].get<std::string>();
        IG.SerializeFieldsSource = value["SerializeFieldsSource"].get<std::string>();
        IG.DeserializeFieldsSource = value["DeserializeFieldsSource"].get<std::string>();

        Res.Generators[key] = IG;
    }

    auto NonTempalteTypesVec = J["NonTemplateTypes"].get<std::vector<std::string>>();
    Res.NonTemplateTypes = std::set<std::string>(NonTempalteTypesVec.begin(), NonTempalteTypesVec.end());
    
    return Res;
}

std::string Template::Generate(bool IsOuter) const {
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

std::string Template::GenerateNames() const {
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