#pragma once

#include "../../core/AssetHandle.hpp"
#include "../EcsTypes.hpp"

#include <string>
#include <atomic>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace Engine {

enum class ScriptFieldType {
    Boolean,
    Number,
    String,
    Enum,
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
    // Non-empty only for Entity fields backing ComponentRef<T>.
    std::string component_type;
    // Non-empty only for Asset fields constrained to an asset type (for example Prefab).
    std::string asset_type;
    // Authored enum name and current members. The serialized value remains the member name.
    std::string enum_type;
    std::vector<std::string> enum_values;
    // Definition metadata comes from @expose and is not authored per scene/prefab.
    bool is_exposed{false};
    std::optional<double> minimum;
    std::optional<double> maximum;

    bool operator==(const ScriptPublicField& other) const {
        // Exposure/range/enum members are derived from the script definition.
        // They must not make an authored scene dirty when the schema is refreshed.
        return name == other.name && type == other.type && value == other.value &&
            component_type == other.component_type && asset_type == other.asset_type &&
            enum_type == other.enum_type;
    }
};

using ScriptSlotID = std::uint64_t;

[[nodiscard]] inline std::atomic<ScriptSlotID>& script_slot_id_counter() {
    static std::atomic<ScriptSlotID> next_id{1};
    return next_id;
}

[[nodiscard]] inline ScriptSlotID make_script_slot_id() {
    return script_slot_id_counter().fetch_add(1, std::memory_order_relaxed);
}

inline void observe_script_slot_id(ScriptSlotID id) {
    auto& next_id = script_slot_id_counter();
    ScriptSlotID expected = next_id.load(std::memory_order_relaxed);
    while (expected <= id &&
           !next_id.compare_exchange_weak(expected, id + 1, std::memory_order_relaxed)) {}
}

struct ScriptBehaviourSlot {
    ScriptSlotID id{make_script_slot_id()};
    AssetHandle script_handle{};
    bool enabled{true};
    bool runtime_only{false};
    std::vector<ScriptPublicField> public_fields;

    bool operator==(const ScriptBehaviourSlot&) const = default;
};

struct ScriptComponent {
    std::vector<ScriptBehaviourSlot> instances{ScriptBehaviourSlot{}};

    bool operator==(const ScriptComponent&) const = default;
};

[[nodiscard]] inline bool script_field_value_matches_type(const ScriptPublicField& field) {
    switch (field.type) {
        case ScriptFieldType::Boolean: return std::holds_alternative<bool>(field.value);
        case ScriptFieldType::Number: return std::holds_alternative<double>(field.value);
        case ScriptFieldType::String: return std::holds_alternative<std::string>(field.value);
        case ScriptFieldType::Enum: return std::holds_alternative<std::string>(field.value);
        case ScriptFieldType::Vector2: return std::holds_alternative<ScriptVector2>(field.value);
        case ScriptFieldType::Entity: return std::holds_alternative<EntityID>(field.value);
        case ScriptFieldType::Asset: return std::holds_alternative<AssetHandle>(field.value);
    }
    return false;
}

} // namespace Engine
