#include "HotReloadManager.hpp"
#include <algorithm>

namespace EngineScript {

bool HotReloadManager::load_initial_module(std::shared_ptr<GameModule> module) {
    if (!module || !module->is_compatible()) {
        return false;
    }
    m_active_module = module;
    return true;
}

void HotReloadManager::register_instance(uint32_t entity_id, const std::string& type_name, std::unique_ptr<NativeBehaviour> instance) {
    if (!instance) return;
    instance->entity_id = entity_id;
    if (instance->owner_entity.id == 0) instance->owner_entity = Entity{.id = entity_id};
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
    // Reload is deliberately fail-stop: broken new code never leaves the old
    // behaviour running, because that would hide the failure from the author.
    if (!new_module || !new_module->is_compatible()) {
        stop_active_instances(scheduler);
        m_active_module.reset();
        return false;
    }

    auto snapshots = snapshot_all_instances();
    std::vector<ActiveBehaviourSlot> candidate_instances;

    for (const auto& [entity_id, type_name] : m_active_descriptors) {
        if (!new_module->has_behaviour_type(type_name)) {
            stop_active_instances(scheduler);
            m_active_module.reset();
            return false;
        }

        const auto& registrations = new_module->descriptor().behaviour_types;
        const auto registration = std::find_if(registrations.begin(), registrations.end(), [&](const BehaviourTypeRegistration& item) {
            return item.type_name == type_name;
        });
        if (registration == registrations.end()) {
            stop_active_instances(scheduler);
            m_active_module.reset();
            return false;
        }

        std::vector<void*> constructor_args;
        constructor_args.reserve(registration->injected_services.size());
        for (const auto& service_type : registration->injected_services) {
            void* service = di.resolve_service(service_type);
            if (!service) {
                stop_active_instances(scheduler);
                m_active_module.reset();
                return false;
            }
            constructor_args.push_back(service);
        }

        NativeBehaviour* raw_inst = new_module->create_behaviour(
            type_name,
            constructor_args.empty() ? nullptr : constructor_args.data()
        );
        if (!raw_inst) {
            stop_active_instances(scheduler);
            m_active_module.reset();
            return false;
        }

        raw_inst->entity_id = entity_id;
        const auto previous = std::find_if(
            m_active_instances.begin(),
            m_active_instances.end(),
            [&](const ActiveBehaviourSlot& slot) {
                return slot.entity_id == entity_id && slot.type_name == type_name;
            }
        );
        raw_inst->owner_entity = previous != m_active_instances.end() && previous->instance
            ? previous->instance->owner_entity
            : Entity{.id = entity_id};
        raw_inst->task_scope_id = entity_id;
        raw_inst->async_scheduler = &scheduler;
        candidate_instances.push_back({entity_id, type_name, std::unique_ptr<NativeBehaviour>(raw_inst)});
    }

    stop_active_instances(scheduler);
    m_active_module = std::move(new_module);
    m_active_instances = std::move(candidate_instances);
    restore_all_instances(snapshots);

    for (auto& slot : m_active_instances) {
        if (slot.instance) {
            slot.instance->on_start();
        }
    }

    return true;
}

void HotReloadManager::stop_active_instances(AsyncScheduler& scheduler) {
    for (const auto& slot : m_active_instances) {
        if (slot.instance) {
            scheduler.cancel_scope(slot.instance->task_scope_id);
            slot.instance->on_destroy();
        }
    }
    m_active_instances.clear();
}

} // namespace EngineScript
