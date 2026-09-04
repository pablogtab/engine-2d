#include "ScriptInstanceRegistry.hpp"

#include "../enginescript/module/GameModule.hpp"

#include <algorithm>

namespace Engine {

std::size_t ScriptInstanceRegistry::KeyHash::operator()(const ScriptInstanceKey& key) const noexcept {
    std::size_t value = std::hash<EntityID>{}(key.entity);
    value ^= std::hash<ScriptSlotID>{}(key.slot) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
    value ^= std::hash<std::uint32_t>{}(key.generation) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
    return value;
}

std::size_t ScriptInstanceRegistry::SlotKeyHash::operator()(const SlotKey& key) const noexcept {
    std::size_t value = std::hash<EntityID>{}(key.entity);
    value ^= std::hash<ScriptSlotID>{}(key.slot) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
    return value;
}

std::uint32_t ScriptInstanceRegistry::register_instance(
    EntityID entity,
    ScriptSlotID slot,
    std::string type_name,
    EngineScript::NativeBehaviour* behaviour
) {
    std::uint32_t& generation = m_generations[{entity, slot}];
    ++generation;
    if (generation == 0) ++generation;
    m_entries[{entity, slot, generation}] = {std::move(type_name), behaviour, ScriptInstanceState::Constructed};
    return generation;
}

void ScriptInstanceRegistry::unregister_instance(const ScriptInstanceKey& key) {
    if (const auto found = m_entries.find(key); found != m_entries.end()) {
        found->second.state = ScriptInstanceState::Destroyed;
        found->second.behaviour = nullptr;
        m_entries.erase(found);
    }
}

void ScriptInstanceRegistry::set_state(const ScriptInstanceKey& key, ScriptInstanceState state_value) {
    if (const auto found = m_entries.find(key); found != m_entries.end()) found->second.state = state_value;
}

ScriptInstanceState ScriptInstanceRegistry::state(const ScriptInstanceKey& key) const {
    const auto found = m_entries.find(key);
    return found == m_entries.end() ? ScriptInstanceState::Destroyed : found->second.state;
}

EngineScript::NativeBehaviour* ScriptInstanceRegistry::resolve(
    const ScriptInstanceKey& key,
    const std::string& type_name
) const {
    const auto found = m_entries.find(key);
    if (found == m_entries.end() || found->second.type_name != type_name ||
        found->second.state == ScriptInstanceState::Destroyed || found->second.state == ScriptInstanceState::Error) {
        return nullptr;
    }
    return found->second.behaviour;
}

std::vector<ScriptInstanceKey> ScriptInstanceRegistry::find(
    EntityID entity,
    const std::string& type_name
) const {
    std::vector<ScriptInstanceKey> result;
    for (const auto& [key, entry] : m_entries) {
        if (key.entity == entity && entry.type_name == type_name && entry.behaviour &&
            entry.state != ScriptInstanceState::Destroyed && entry.state != ScriptInstanceState::Error) {
            result.push_back(key);
        }
    }
    std::ranges::sort(result, {}, &ScriptInstanceKey::slot);
    return result;
}

void ScriptInstanceRegistry::clear() {
    m_entries.clear();
}

} // namespace Engine
