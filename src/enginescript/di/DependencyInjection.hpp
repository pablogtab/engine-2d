#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <optional>

namespace EngineScript {

enum class DIScopeLevel {
    Application,
    PlaySession,
    Scene,
    Entity,
    Behaviour
};

inline const char* di_scope_to_string(DIScopeLevel level) {
    switch (level) {
        case DIScopeLevel::Application: return "Application";
        case DIScopeLevel::PlaySession: return "PlaySession";
        case DIScopeLevel::Scene:       return "Scene";
        case DIScopeLevel::Entity:      return "Entity";
        case DIScopeLevel::Behaviour:   return "Behaviour";
    }
    return "Unknown";
}

struct ServiceDescriptor {
    std::string type_name;
    DIScopeLevel scope_level = DIScopeLevel::Application;
    std::vector<std::string> dependencies;
    std::function<void*()> factory;
};

struct EntityContext {
    uint32_t entity_id = 0;
    std::vector<std::string> attached_components;

    bool has_component(const std::string& comp_name) const {
        for (const auto& c : attached_components) {
            if (c == comp_name) return true;
        }
        return false;
    }
};

struct DIValidationResult {
    bool is_valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> missing_required_components;
};

class DIContainer {
public:
    DIContainer() = default;

    void register_service(ServiceDescriptor desc);
    void register_instance(const std::string& type_name, void* instance, DIScopeLevel scope_level = DIScopeLevel::Application);

    void* resolve_service(const std::string& type_name) const;

    DIValidationResult validate_behaviour_requirements(
        const std::string& behaviour_name,
        const std::vector<std::pair<std::string, std::string>>& services,       // (param_name, service_type)
        const std::vector<std::pair<std::string, std::string>>& components,     // (param_name, component_type)
        const std::vector<std::pair<std::string, std::string>>& optional_comps, // (param_name, component_type)
        const EntityContext& entity
    ) const;

    bool check_service_cycles(std::vector<std::string>* out_cycle = nullptr) const;

private:
    std::unordered_map<std::string, ServiceDescriptor> m_descriptors;
    std::unordered_map<std::string, void*> m_instances;
};

} // namespace EngineScript
