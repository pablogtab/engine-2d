#include "DependencyInjection.hpp"
#include <queue>
#include <algorithm>
#include <format>

namespace EngineScript {

void DIContainer::register_service(ServiceDescriptor desc) {
    m_descriptors[desc.type_name] = std::move(desc);
}

void DIContainer::register_instance(const std::string& type_name, void* instance, DIScopeLevel scope_level) {
    m_instances[type_name] = instance;
    ServiceDescriptor desc;
    desc.type_name = type_name;
    desc.scope_level = scope_level;
    m_descriptors[type_name] = desc;
}

void* DIContainer::resolve_service(const std::string& type_name) const {
    auto it = m_instances.find(type_name);
    if (it != m_instances.end()) return it->second;

    auto desc_it = m_descriptors.find(type_name);
    if (desc_it != m_descriptors.end() && desc_it->second.factory) {
        return desc_it->second.factory();
    }
    return nullptr;
}

bool DIContainer::check_service_cycles(std::vector<std::string>* out_cycle) const {
    enum class State { Unvisited, Visiting, Visited };
    std::unordered_map<std::string, State> state;
    std::vector<std::string> stack;

    for (const auto& [name, desc] : m_descriptors) {
        state[name] = State::Unvisited;
    }

    auto dfs = [&](auto& self, const std::string& u) -> bool {
        state[u] = State::Visiting;
        stack.push_back(u);

        auto it = m_descriptors.find(u);
        if (it != m_descriptors.end()) {
            for (const auto& dep : it->second.dependencies) {
                if (state[dep] == State::Visiting) {
                    if (out_cycle) {
                        out_cycle->clear();
                        auto c_it = std::find(stack.begin(), stack.end(), dep);
                        if (c_it != stack.end()) {
                            out_cycle->assign(c_it, stack.end());
                            out_cycle->push_back(dep);
                        }
                    }
                    return true;
                }
                if (state[dep] == State::Unvisited) {
                    if (self(self, dep)) return true;
                }
            }
        }

        stack.pop_back();
        state[u] = State::Visited;
        return false;
    };

    for (const auto& [name, desc] : m_descriptors) {
        if (state[name] == State::Unvisited) {
            if (dfs(dfs, name)) return true;
        }
    }
    return false;
}

DIValidationResult DIContainer::validate_behaviour_requirements(
    const std::string& behaviour_name,
    const std::vector<std::pair<std::string, std::string>>& services,
    const std::vector<std::pair<std::string, std::string>>& components,
    const std::vector<std::pair<std::string, std::string>>& optional_comps,
    const EntityContext& entity
) const {
    DIValidationResult result;

    // Validate services
    for (const auto& [param, s_type] : services) {
        if (m_instances.find(s_type) == m_instances.end() && m_descriptors.find(s_type) == m_descriptors.end()) {
            result.is_valid = false;
            result.errors.push_back(std::format("Injected service '{}' for behaviour '{}' is not registered in DIContainer", s_type, behaviour_name));
        }
    }

    // Validate required components
    for (const auto& [param, comp_type] : components) {
        if (!entity.has_component(comp_type)) {
            result.is_valid = false;
            result.errors.push_back(std::format("Entity #{} is missing required component '{}' needed by behaviour '{}'", entity.entity_id, comp_type, behaviour_name));
            result.missing_required_components.push_back(comp_type);
        }
    }

    // Optional components do not cause failure if missing

    return result;
}

} // namespace EngineScript
