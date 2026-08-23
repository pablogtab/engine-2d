#include "HotReloadManager.hpp"
#include <algorithm>

namespace EngineScript {

bool HotReloadManager::load_initial_module(std::shared_ptr<GameModule> module) {
    if (!module || !module->is_compatible()) {
        return false;
    }
    m_active_module = module;
    m_last_known_good_module = module;
    return true;
}

void HotReloadManager::register_instance(uint32_t entity_id, const std::string& type_name, std::unique_ptr<NativeBehaviour> instance) {
    if (!instance) return;
    instance->entity_id = entity_id;
    m_active_instances.push_back({entity_id, type_name, std::move(instance)});
    m_active_descriptors.push_back({entity_id, type_name});
}

void HotReloadManager::unregister_instance(uint32_t entity_id, const std::string& type_name) {
    m_active_instances.erase(
        std::remove_if(m_active_instances.begin(), m_active_instances.end(), [&](const ActiveBehaviourSlot& slot) {
            return slot.entity_id == entity_id && slot.type_name == type_name;
        }),
        m_active_instances.end()
    );
    m_active_descriptors.erase(
        std::remove_if(m_active_descriptors.begin(), m_active_descriptors.end(), [&](const std::pair<uint32_t, std::string>& p) {
            return p.first == entity_id && p.second == type_name;
        }),
        m_active_descriptors.end()
    );
}

std::vector<PropertySnapshot> HotReloadManager::snapshot_all_instances() {
    std::vector<PropertySnapshot> snapshots;
    for (const auto& slot : m_active_instances) {
        PropertySnapshot snap;
        snap.entity_id = slot.entity_id;
        snap.behaviour_type = slot.type_name;
        if (on_snapshot_properties && slot.instance) {
            snap.property_values = on_snapshot_properties(slot.instance.get());
        }
        snapshots.push_back(std::move(snap));
    }
    return snapshots;
}

void HotReloadManager::restore_all_instances(const std::vector<PropertySnapshot>& snapshots) {
    for (const auto& snap : snapshots) {
        for (auto& slot : m_active_instances) {
            if (slot.entity_id == snap.entity_id && slot.type_name == snap.behaviour_type) {
                if (on_restore_properties && slot.instance) {
                    on_restore_properties(slot.instance.get(), snap.property_values);
                }
            }
        }
    }
}

bool HotReloadManager::hot_reload(std::shared_ptr<GameModule> new_module, AsyncScheduler& scheduler, const DIContainer& di) {
    // 1. Validate new module ABI & compatibility
    if (!new_module || !new_module->is_compatible()) {
        return false; // Incompatible module rejected safely!
    }

    // 2. Snapshot compatible exposed properties
    auto snapshots = snapshot_all_instances();

    // 3. Cancel owned async tasks for all instances
    for (const auto& slot : m_active_instances) {
        if (slot.instance) {
            scheduler.cancel_scope(slot.instance->task_scope_id);
            slot.instance->on_destroy();
        }
    }

    // 4. Destroy old instances
    m_active_instances.clear();

    // 5. Switch active module
    m_active_module = new_module;

    // 6. Recreate instances with DI injection
    bool recreation_success = true;
    for (const auto& [entity_id, type_name] : m_active_descriptors) {
        if (!new_module->has_behaviour_type(type_name)) {
            recreation_success = false;
            break;
        }

        NativeBehaviour* raw_inst = new_module->create_behaviour(type_name);
        if (!raw_inst) {
            recreation_success = false;
            break;
        }

        raw_inst->entity_id = entity_id;
        raw_inst->task_scope_id = entity_id; // Scope tied to entity
        m_active_instances.push_back({entity_id, type_name, std::unique_ptr<NativeBehaviour>(raw_inst)});
    }

    if (!recreation_success) {
        // Rollback to last known good!
        rollback(scheduler, di);
        return false;
    }

    // 7. Restore properties
    restore_all_instances(snapshots);

    // 8. Lifecycle start
    for (auto& slot : m_active_instances) {
        if (slot.instance) {
            slot.instance->on_start();
        }
    }

    // 9. Update last known good
    m_last_known_good_module = new_module;
    return true;
}

bool HotReloadManager::rollback(AsyncScheduler& scheduler, const DIContainer& di) {
    if (!m_last_known_good_module) return false;

    auto snapshots = snapshot_all_instances();

    for (const auto& slot : m_active_instances) {
        if (slot.instance) {
            scheduler.cancel_scope(slot.instance->task_scope_id);
            slot.instance->on_destroy();
        }
    }
    m_active_instances.clear();

    m_active_module = m_last_known_good_module;

    for (const auto& [entity_id, type_name] : m_active_descriptors) {
        NativeBehaviour* raw_inst = m_last_known_good_module->create_behaviour(type_name);
        if (raw_inst) {
            raw_inst->entity_id = entity_id;
            raw_inst->task_scope_id = entity_id;
            m_active_instances.push_back({entity_id, type_name, std::unique_ptr<NativeBehaviour>(raw_inst)});
        }
    }

    restore_all_instances(snapshots);

    for (auto& slot : m_active_instances) {
        if (slot.instance) {
            slot.instance->on_start();
        }
    }

    return true;
}

} // namespace EngineScript
