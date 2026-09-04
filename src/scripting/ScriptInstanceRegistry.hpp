#pragma once

#include "../ecs/EcsTypes.hpp"
#include "../ecs/components/ScriptingComponents.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace EngineScript { class NativeBehaviour; }

namespace Engine {

enum class ScriptInstanceState {
    Constructed,
    Initializing,
    Initialized,
    Starting,
    Running,
    Error,
    Destroyed
};

struct ScriptInstanceKey {
    EntityID entity{0};
    ScriptSlotID slot{0};
    std::uint32_t generation{0};

    bool operator==(const ScriptInstanceKey&) const = default;
};

class ScriptInstanceRegistry {
public:
    std::uint32_t register_instance(
        EntityID entity,
        ScriptSlotID slot,
        std::string type_name,
        EngineScript::NativeBehaviour* behaviour
    );
    void unregister_instance(const ScriptInstanceKey& key);
    void set_state(const ScriptInstanceKey& key, ScriptInstanceState state);
    [[nodiscard]] ScriptInstanceState state(const ScriptInstanceKey& key) const;
    [[nodiscard]] EngineScript::NativeBehaviour* resolve(
        const ScriptInstanceKey& key,
        const std::string& type_name
    ) const;
    [[nodiscard]] std::vector<ScriptInstanceKey> find(
        EntityID entity,
        const std::string& type_name
    ) const;
    void clear();

private:
    struct Entry {
        std::string type_name;
        EngineScript::NativeBehaviour* behaviour{nullptr};
        ScriptInstanceState state{ScriptInstanceState::Constructed};
    };
    struct KeyHash {
        std::size_t operator()(const ScriptInstanceKey& key) const noexcept;
    };
    struct SlotKey {
        EntityID entity{0};
        ScriptSlotID slot{0};
        bool operator==(const SlotKey&) const = default;
    };
    struct SlotKeyHash {
        std::size_t operator()(const SlotKey& key) const noexcept;
    };

    std::unordered_map<ScriptInstanceKey, Entry, KeyHash> m_entries;
    std::unordered_map<SlotKey, std::uint32_t, SlotKeyHash> m_generations;
};

} // namespace Engine
