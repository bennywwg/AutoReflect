#pragma once

#include <string>
#include <stdexcept>
#include <vector>
#include <optional>
#include <typeinfo>

#include <nlohmann/json.hpp>
#include <glm/ext.hpp>

class SubclassOfBase {
public:
    std::function<void*(SubclassOfBase const&)> BaseFunc; // Function that gets the base ptr
    std::any Value;
public:
    std::any const& GetAny() const { return Value; }
    
    void Reset() {
        Value = nullptr;
        BaseFunc = nullptr;
    }
};

// Version of any that can be cast to a base class T
template<typename T>
class SubclassOf : public SubclassOfBase {
public:
    SubclassOfBase Erase() const {
        SubclassOfBase Ret;
        Ret.Value = Value;
        Ret.BaseFunc = BaseFunc;
        return Ret;
    }

    SubclassOf() { Reset(); }

    template<typename U>
    SubclassOf(U const& InValue) {
        Value = InValue;
        BaseFunc = [](SubclassOfBase const& Self) -> void* {
            return static_cast<T*>(const_cast<U*>(&std::any_cast<U const&>(Self.Value)));
        };
    }
    
    SubclassOf& operator= (SubclassOf<T> const& Other) {
        Value = Other.Value;
        BaseFunc = Other.BaseFunc;
        return *this;
    }
    SubclassOf(SubclassOf<T> const& Other) { *this = Other; }

    SubclassOf& operator= (SubclassOf<T>&& Other) {
        Value = std::move(Other.Value);
        BaseFunc = std::move(Other.BaseFunc);
        Other.Reset();
        return *this;
    }
    SubclassOf(SubclassOf<T>&& Other) { *this = std::move(Other); }

    SubclassOf& operator= (std::nullptr_t) { Reset(); return *this; }

    T& GetBase() const { return *reinterpret_cast<T*>(BaseFunc(*this)); }
    template<typename U> U& GetAs() const { return const_cast<U&>(std::any_cast<U const&>(Value)); }
};

class SerdeData {
public:
    nlohmann::json Data;
    std::vector<uint8_t> Binary;

    std::vector<nlohmann::json*> Scopes;
    std::vector<std::string> ScopeNames;

    void BeginObject(char const* Name);
    void EndObject();

    nlohmann::json& GetCurrentScope();
    virtual nlohmann::json& AtChecked(char const* Name) = 0;
};

class Serializer : public SerdeData {
public:
    nlohmann::json& AtChecked(char const* Name) override;
};

class Deserializer : public SerdeData {
public:
    nlohmann::json& AtChecked(char const* Name) override;
};

void Serialize(Serializer& Ser, char const* Name, SubclassOfBase const& Val);
void Deserialize(Deserializer& Ser, char const* Name, SubclassOfBase& Val);
void SerializeFields(Serializer& Ser, SubclassOfBase const& Val);
void DeserializeFields(Deserializer& Ser, SubclassOfBase& Val);

void Serialize(Serializer& Ser, char const* Name, bool const& Value);
void Serialize(Serializer& Ser, char const* Name, uint8_t const& Value);
void Serialize(Serializer& Ser, char const* Name, uint16_t const& Value);
void Serialize(Serializer& Ser, char const* Name, uint32_t const& Value);
void Serialize(Serializer& Ser, char const* Name, uint64_t const& Value);
void Serialize(Serializer& Ser, char const* Name, int8_t const& Value);
void Serialize(Serializer& Ser, char const* Name, int16_t const& Value);
void Serialize(Serializer& Ser, char const* Name, int32_t const& Value);
void Serialize(Serializer& Ser, char const* Name, int64_t const& Value);
void Serialize(Serializer& Ser, char const* Name, std::string const& Value);
void Serialize(Serializer& Ser, char const* Name, float const& Value);
void Serialize(Serializer& Ser, char const* Name, double const& Value);

void Deserialize(Deserializer& Ser, char const* Name, bool& Value);
void Deserialize(Deserializer& Ser, char const* Name, uint8_t& Value);
void Deserialize(Deserializer& Ser, char const* Name, uint16_t& Value);
void Deserialize(Deserializer& Ser, char const* Name, uint32_t& Value);
void Deserialize(Deserializer& Ser, char const* Name, uint64_t& Value);
void Deserialize(Deserializer& Ser, char const* Name, int8_t& Value);
void Deserialize(Deserializer& Ser, char const* Name, int16_t& Value);
void Deserialize(Deserializer& Ser, char const* Name, int32_t& Value);
void Deserialize(Deserializer& Ser, char const* Name, int64_t& Value);
void Deserialize(Deserializer& Ser, char const* Name, std::string& Value);
void Deserialize(Deserializer& Ser, char const* Name, float& Value);
void Deserialize(Deserializer& Ser, char const* Name, double& Value);

void SerializeFields(Serializer& Ser, bool const& Value);
void SerializeFields(Serializer& Ser, uint8_t const& Value);
void SerializeFields(Serializer& Ser, uint16_t const& Value);
void SerializeFields(Serializer& Ser, uint32_t const& Value);
void SerializeFields(Serializer& Ser, uint64_t const& Value);
void SerializeFields(Serializer& Ser, int8_t const& Value);
void SerializeFields(Serializer& Ser, int16_t const& Value);
void SerializeFields(Serializer& Ser, int32_t const& Value);
void SerializeFields(Serializer& Ser, int64_t const& Value);
void SerializeFields(Serializer& Ser, std::string const& Value);
void SerializeFields(Serializer& Ser, float const& Value);
void SerializeFields(Serializer& Ser, double const& Value);

void DeserializeFields(Deserializer& Ser, bool& Value);
void DeserializeFields(Deserializer& Ser, uint8_t& Value);
void DeserializeFields(Deserializer& Ser, uint16_t& Value);
void DeserializeFields(Deserializer& Ser, uint32_t& Value);
void DeserializeFields(Deserializer& Ser, uint64_t& Value);
void DeserializeFields(Deserializer& Ser, int8_t& Value);
void DeserializeFields(Deserializer& Ser, int16_t& Value);
void DeserializeFields(Deserializer& Ser, int32_t& Value);
void DeserializeFields(Deserializer& Ser, int64_t& Value);
void DeserializeFields(Deserializer& Ser, std::string& Value);
void DeserializeFields(Deserializer& Ser, float& Value);
void DeserializeFields(Deserializer& Ser, double& Value);

void Serialize(Serializer& Ser, char const* Name, glm::vec2 const& Value);
void Serialize(Serializer& Ser, char const* Name, glm::vec3 const& Value);
void Serialize(Serializer& Ser, char const* Name, glm::vec4 const& Value);
void Serialize(Serializer& Ser, char const* Name, glm::ivec2 const& Value);
void Serialize(Serializer& Ser, char const* Name, glm::ivec3 const& Value);
void Serialize(Serializer& Ser, char const* Name, glm::ivec4 const& Value);
void Serialize(Serializer& Ser, char const* Name, glm::uvec2 const& Value);
void Serialize(Serializer& Ser, char const* Name, glm::uvec3 const& Value);
void Serialize(Serializer& Ser, char const* Name, glm::uvec4 const& Value);

void Deserialize(Deserializer& Ser, char const* Name, glm::vec2& Value);
void Deserialize(Deserializer& Ser, char const* Name, glm::vec3& Value);
void Deserialize(Deserializer& Ser, char const* Name, glm::vec4& Value);
void Deserialize(Deserializer& Ser, char const* Name, glm::ivec2& Value);
void Deserialize(Deserializer& Ser, char const* Name, glm::ivec3& Value);
void Deserialize(Deserializer& Ser, char const* Name, glm::ivec4& Value);
void Deserialize(Deserializer& Ser, char const* Name, glm::uvec2& Value);
void Deserialize(Deserializer& Ser, char const* Name, glm::uvec3& Value);
void Deserialize(Deserializer& Ser, char const* Name, glm::uvec4& Value);

void SerializeFields(Serializer& Ser, glm::vec2 const& Value);
void SerializeFields(Serializer& Ser, glm::vec3 const& Value);
void SerializeFields(Serializer& Ser, glm::vec4 const& Value);
void SerializeFields(Serializer& Ser, glm::ivec2 const& Value);
void SerializeFields(Serializer& Ser, glm::ivec3 const& Value);
void SerializeFields(Serializer& Ser, glm::ivec4 const& Value);
void SerializeFields(Serializer& Ser, glm::uvec2 const& Value);
void SerializeFields(Serializer& Ser, glm::uvec3 const& Value);
void SerializeFields(Serializer& Ser, glm::uvec4 const& Value);

void DeserializeFields(Deserializer& Ser, glm::vec2& Value);
void DeserializeFields(Deserializer& Ser, glm::vec3& Value);
void DeserializeFields(Deserializer& Ser, glm::vec4& Value);
void DeserializeFields(Deserializer& Ser, glm::ivec2& Value);
void DeserializeFields(Deserializer& Ser, glm::ivec3& Value);
void DeserializeFields(Deserializer& Ser, glm::ivec4& Value);
void DeserializeFields(Deserializer& Ser, glm::uvec2& Value);
void DeserializeFields(Deserializer& Ser, glm::uvec3& Value);
void DeserializeFields(Deserializer& Ser, glm::uvec4& Value);

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
    Ser.BeginObject(Name);
    SerializeFields(Ser, Value);
    Ser.EndObject();
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
    Ser.BeginObject(Name);
    DeserializeFields(Ser, Value);
    Ser.EndObject();
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
    Ser.BeginObject(Name);
    SerializeFields(Ser, Value);
    Ser.EndObject();
}

template<typename T>
inline void DeserializeFields(Deserializer& Ser, std::optional<T>& Value) {
    auto& Scope = Ser.GetCurrentScope();

    if (Scope.is_null()) {
        Value = std::nullopt;
    } else {
        Value = T();
        DeserializeFields(Ser, Value.value());
    }
}

template<typename T>
inline void Deserialize(Deserializer& Ser, char const* Name, std::optional<T>& Value) {
    Ser.BeginObject(Name);
    DeserializeFields(Ser, Value);
    Ser.EndObject();
}