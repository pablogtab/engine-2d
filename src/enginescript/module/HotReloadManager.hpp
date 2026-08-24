#pragma once

#include "GameModule.hpp"
#include "../async/AsyncScheduler.hpp"
#include "../di/DependencyInjection.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace EngineScript {

struct PropertySnapshot {
    std::string behaviour_type;
    uint32_t entity_id = 0;
    std::unordered_map<std::string, std::string> property_values;
};

struct ActiveBehaviourSlot {
    uint32_t entity_id = 0;
    std::string type_name;
    std::unique_ptr<NativeBehaviour> instance;
};

class HotReloadManager {
public:
    HotReloadManager() = default;

    bool load_initial_module(std::shared_ptr<GameModule> module);
    bool hot_reload(std::shared_ptr<GameModule> new_module, AsyncScheduler& scheduler, const DIContainer& di);

    void register_instance(uint32_t entity_id, const std::string& type_name, std::unique_ptr<NativeBehaviour> instance);
    void unregister_instance(uint32_t entity_id, const std::string& type_name);

    size_t active_instances_count() const { return m_active_instances.size(); }
    const std::vector<ActiveBehaviourSlot>& active_instances() const { return m_active_instances; }

    std::shared_ptr<GameModule> active_module() const { return m_active_module; }

    // Mock property snapshot hook for testing
    std::function<std::unordered_map<std::string, std::string>(const NativeBehaviour*)> on_snapshot_properties;
    std::function<void(NativeBehaviour*, const std::unordered_map<std::string, std::string>&)> on_restore_properties;

private:
    std::vector<PropertySnapshot> snapshot_all_instances();
    void restore_all_instances(const std::vector<PropertySnapshot>& snapshots);
    void stop_active_instances(AsyncScheduler& scheduler);

    std::shared_ptr<GameModule> m_active_module;
    std::vector<ActiveBehaviourSlot> m_active_instances;
    std::vector<std::pair<uint32_t, std::string>> m_active_descriptors;
};

} // namespace EngineScript
