#pragma once

#include "../../core/AssetHandle.hpp"
#include "../EcsTypes.hpp"

#include <string>
#include <variant>
#include <vector>

namespace Engine {

enum class ScriptFieldType {
    Boolean,
    Number,
    String,
    Vector2,
    Entity,
    Asset
};

struct ScriptVector2 {
    float x{0.0f};
    float y{0.0f};

    constexpr bool operator==(const ScriptVector2&) const = default;
};

using ScriptFieldValue = std::variant<
    bool,
    double,
    std::string,
    ScriptVector2,
    EntityID,
    AssetHandle
>;

struct ScriptPublicField {
    std::string name;
    ScriptFieldType type{ScriptFieldType::Number};
    ScriptFieldValue value{0.0};

    bool operator==(const ScriptPublicField&) const = default;
};

struct ScriptComponent {
    AssetHandle script_handle{};
    bool enabled{true};
    std::vector<ScriptPublicField> public_fields;

    bool operator==(const ScriptComponent&) const = default;
};

[[nodiscard]] inline bool script_field_value_matches_type(const ScriptPublicField& field) {
    switch (field.type) {
        case ScriptFieldType::Boolean: return std::holds_alternative<bool>(field.value);
        case ScriptFieldType::Number: return std::holds_alternative<double>(field.value);
        case ScriptFieldType::String: return std::holds_alternative<std::string>(field.value);
        case ScriptFieldType::Vector2: return std::holds_alternative<ScriptVector2>(field.value);
        case ScriptFieldType::Entity: return std::holds_alternative<EntityID>(field.value);
        case ScriptFieldType::Asset: return std::holds_alternative<AssetHandle>(field.value);
    }
    return false;
}

} // namespace Engine
