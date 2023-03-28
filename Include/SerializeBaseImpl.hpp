#include <string>
#include <stdexcept>
#include <vector>
#include <typeinfo>

#include <nlohmann/json.hpp>

#ifdef AR_SUPPORT_GLM
#include <glm/ext.hpp>
#endif

class Serializer {
public:
    nlohmann::json Data;
    std::vector<uint8_t> Binary;

    std::vector<nlohmann::json*> Scopes;
    std::vector<std::string> ScopeNames;

    inline nlohmann::json& GetCurrentScope() {
        return Scopes.empty() ? Data : *Scopes.back();
    }
    
    inline nlohmann::json& AtChecked(char const* Name) {
        if (GetCurrentScope().find(Name) != GetCurrentScope().end()) throw std::runtime_error("Name " + std::string(Name) + " already in use");
        return GetCurrentScope()[Name];
    }
};

class Deserializer {
public:
    nlohmann::json Data;
    std::vector<uint8_t> Binary;

    std::vector<nlohmann::json*> Scopes;
    std::vector<std::string> ScopeNames;

    Deserializer() = default;

    inline nlohmann::json& GetCurrentScope() {
        return Scopes.empty() ? Data : *Scopes.back();
    }
    
    inline nlohmann::json& AtChecked(char const* Name) {
        if (GetCurrentScope().find(Name) == GetCurrentScope().end()) {
            std::string Er = ScopeNames.empty() ? "" : ScopeNames[0];
            for (std::string const& ScopeName : ScopeNames) {
				Er += "::" + ScopeName;
			}
            throw std::runtime_error("Name " + std::string(Name) + " can't be found" + (Er.empty() ? std::string() : " in " + Er));

        }
        return GetCurrentScope()[Name];
    }
};
                                                                                              
static void BeginObject(Serializer& Ser, char const* Name) {
    nlohmann::json& Scope = Ser.AtChecked(Name);
    Ser.Scopes.push_back(&Scope);
    Ser.ScopeNames.push_back(Name);
}

static void BeginObject(Deserializer& Ser, char const* Name) {
    nlohmann::json& Scope = Ser.AtChecked(Name);
	Ser.Scopes.push_back(&Scope);
	Ser.ScopeNames.push_back(Name);
}

static void EndObject(Serializer& Ser) {
  Ser.Scopes.pop_back();
  Ser.ScopeNames.pop_back();
}

static void EndObject(Deserializer& Ser) {
    Ser.Scopes.pop_back();
    Ser.ScopeNames.pop_back();
}

static void Serialize(Serializer& Ser, char const* Name, std::any const& Val);
static void Deserialize(Deserializer& Ser, char const* Name, std::any& Val);
static void SerializeFields(Serializer& Ser, std::any const& Val);
static void DeserializeFields(Deserializer& Ser, std::any& Val);

static void Serialize(Serializer& Ser, char const* Name, bool const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, uint8_t const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, uint16_t const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, uint32_t const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, uint64_t const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, int8_t const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, int16_t const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, int32_t const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, int64_t const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, std::string const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, float const& Value) { Ser.AtChecked(Name) = Value; }
static void Serialize(Serializer& Ser, char const* Name, double const& Value) { Ser.AtChecked(Name) = Value; }

static void Deserialize(Deserializer& Ser, char const* Name, bool& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, uint8_t& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, uint16_t& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, uint32_t& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, uint64_t& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, int8_t& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, int16_t& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, int32_t& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, int64_t& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, std::string& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, float& Value) { Value = Ser.AtChecked(Name); }
static void Deserialize(Deserializer& Ser, char const* Name, double& Value) { Value = Ser.AtChecked(Name); }

static void SerializeFields(Serializer& Ser, bool const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, uint8_t const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, uint16_t const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, uint32_t const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, uint64_t const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, int8_t const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, int16_t const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, int32_t const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, int64_t const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, std::string const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, float const& Value) { Ser.GetCurrentScope() = Value; }
static void SerializeFields(Serializer& Ser, double const& Value) { Ser.GetCurrentScope() = Value; }

static void DeserializeFields(Deserializer& Ser, bool& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, uint8_t& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, uint16_t& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, uint32_t& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, uint64_t& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, int8_t& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, int16_t& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, int32_t& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, int64_t& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, std::string& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, float& Value) { Value = Ser.GetCurrentScope(); }
static void DeserializeFields(Deserializer& Ser, double& Value) { Value = Ser.GetCurrentScope(); }

#ifdef AR_SUPPORT_GLM
inline void Serialize(Serializer& Ser, char const* Name, glm::vec2 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y }); }
inline void Serialize(Serializer& Ser, char const* Name, glm::vec3 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y, Value.z }); }
inline void Serialize(Serializer& Ser, char const* Name, glm::vec4 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y, Value.z, Value.w }); }
inline void Serialize(Serializer& Ser, char const* Name, glm::ivec2 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y }); }
inline void Serialize(Serializer& Ser, char const* Name, glm::ivec3 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y, Value.z }); }
inline void Serialize(Serializer& Ser, char const* Name, glm::ivec4 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y, Value.z, Value.w }); }
inline void Serialize(Serializer& Ser, char const* Name, glm::uvec2 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y }); }
inline void Serialize(Serializer& Ser, char const* Name, glm::uvec3 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y, Value.z }); }
inline void Serialize(Serializer& Ser, char const* Name, glm::uvec4 const& Value) { Ser.AtChecked(Name) = nlohmann::json::array({ Value.x, Value.y, Value.z, Value.w }); }

inline void Deserialize(Deserializer& Ser, char const* Name, glm::vec2& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::vec2(Array[0], Array[1]); }
inline void Deserialize(Deserializer& Ser, char const* Name, glm::vec3& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::vec3(Array[0], Array[1], Array[2]); }
inline void Deserialize(Deserializer& Ser, char const* Name, glm::vec4& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::vec4(Array[0], Array[1], Array[2], Array[3]); }
inline void Deserialize(Deserializer& Ser, char const* Name, glm::ivec2& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::ivec2(Array[0], Array[1]); }
inline void Deserialize(Deserializer& Ser, char const* Name, glm::ivec3& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::ivec3(Array[0], Array[1], Array[2]); }
inline void Deserialize(Deserializer& Ser, char const* Name, glm::ivec4& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::ivec4(Array[0], Array[1], Array[2], Array[3]); }
inline void Deserialize(Deserializer& Ser, char const* Name, glm::uvec2& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::uvec2(Array[0], Array[1]); }
inline void Deserialize(Deserializer& Ser, char const* Name, glm::uvec3& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::uvec3(Array[0], Array[1], Array[2]); }
inline void Deserialize(Deserializer& Ser, char const* Name, glm::uvec4& Value)
{ auto const& Array = Ser.AtChecked(Name); Value = glm::uvec4(Array[0], Array[1], Array[2], Array[3]); }

inline void SerializeFields(Serializer& Ser, glm::vec2 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y }); }
inline void SerializeFields(Serializer& Ser, glm::vec3 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y, Value.z }); }
inline void SerializeFields(Serializer& Ser, glm::vec4 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y, Value.z, Value.w }); }
inline void SerializeFields(Serializer& Ser, glm::ivec2 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y }); }
inline void SerializeFields(Serializer& Ser, glm::ivec3 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y, Value.z }); }
inline void SerializeFields(Serializer& Ser, glm::ivec4 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y, Value.z, Value.w }); }
inline void SerializeFields(Serializer& Ser, glm::uvec2 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y }); }
inline void SerializeFields(Serializer& Ser, glm::uvec3 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y, Value.z }); }
inline void SerializeFields(Serializer& Ser, glm::uvec4 const& Value) { Ser.GetCurrentScope() = nlohmann::json::array({ Value.x, Value.y, Value.z, Value.w }); }

inline void DeserializeFields(Deserializer& Ser, glm::vec2& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::vec2(Array[0], Array[1]); }
inline void DeserializeFields(Deserializer& Ser, glm::vec3& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::vec3(Array[0], Array[1], Array[2]); }
inline void DeserializeFields(Deserializer& Ser, glm::vec4& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::vec4(Array[0], Array[1], Array[2], Array[3]); }
inline void DeserializeFields(Deserializer& Ser, glm::ivec2& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::ivec2(Array[0], Array[1]); }
inline void DeserializeFields(Deserializer& Ser, glm::ivec3& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::ivec3(Array[0], Array[1], Array[2]); }
inline void DeserializeFields(Deserializer& Ser, glm::ivec4& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::ivec4(Array[0], Array[1], Array[2], Array[3]); }
inline void DeserializeFields(Deserializer& Ser, glm::uvec2& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::uvec2(Array[0], Array[1]); }
inline void DeserializeFields(Deserializer& Ser, glm::uvec3& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::uvec3(Array[0], Array[1], Array[2]); }
inline void DeserializeFields(Deserializer& Ser, glm::uvec4& Value)
{ auto const& Array = Ser.GetCurrentScope(); Value = glm::uvec4(Array[0], Array[1], Array[2], Array[3]); }
#endif

template<typename T>
inline void SerializeFields(Serializer& Ser, std::vector<T> const& Value) {
    auto& Scope = Ser.GetCurrentScope();
    Scope = nlohmann::json::array();

    for (auto const& Item : Value) {
        Scope.push_back(nlohmann::json());
        Ser.Scopes.push_back(&Scope.back());
        SerializeFields(Ser, Item);
        Ser.Scopes.pop_back();
    }
}

template<typename T>
inline void Serialize(Serializer& Ser, char const* Name, std::vector<T> const& Value) {
    BeginObject(Ser, Name);
    SerializeFields(Ser, Value);
    EndObject(Ser);
}

template<typename T>
inline void DeserializeFields(Deserializer& Ser, std::vector<T>& Value) {
    auto& Scope = Ser.GetCurrentScope();

    for (auto& Item : Scope) {
        Value.push_back(T());
        Ser.Scopes.push_back(&Item);
        DeserializeFields(Ser, Value.back());
        Ser.Scopes.pop_back();
    }
}

template<typename T>
inline void Deserialize(Deserializer& Ser, char const* Name, std::vector<T>& Value) {
    BeginObject(Ser, Name);
    DeserializeFields(Ser, Value);
    EndObject(Ser);
}

template<typename T>
inline void SerializeFields(Serializer& Ser, std::optional<T> const& Value) {
    if (Value.has_value()) {
        SerializeFields(Ser, Value.value());
    } else {
        Ser.GetCurrentScope() = nullptr;
    }
}

template<typename T>
inline void Serialize(Serializer& Ser, char const* Name, std::optional<T> const& Value) {
    BeginObject(Ser, Name);
    SerializeFields(Ser, Value);
    EndObject(Ser);
}