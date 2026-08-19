#include "EditorComponentRegistry.hpp"
#include <unordered_set>
#include <utility>

namespace Engine {

bool EditorComponentRegistry::register_component(
    EditorComponentDescriptor descriptor,
    std::string* error
) {
    const auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (descriptor.component_key.empty()) return fail("Editor component key cannot be empty");
    if (descriptor.display_name.empty()) return fail("Editor component display name cannot be empty");
    if (m_by_component_key.contains(descriptor.component_key)) {
        return fail("Duplicate editor component key: " + descriptor.component_key);
    }
    if (descriptor.show_in_inspector) {
        if (descriptor.drawer_key.empty()) return fail("Visible editor component requires a drawer key");
        if (!descriptor.draw) return fail("Visible editor component requires a drawer callback");
        if (m_by_drawer_key.contains(descriptor.drawer_key)) {
            return fail("Duplicate component drawer key: " + descriptor.drawer_key);
        }
    }
    if (descriptor.addable && !descriptor.create_default) {
        return fail("Addable editor component requires a default factory");
    }

    const std::size_t index = m_descriptors.size();
    m_by_component_key.emplace(descriptor.component_key, index);
    if (!descriptor.drawer_key.empty()) m_by_drawer_key.emplace(descriptor.drawer_key, index);
    m_descriptors.push_back(std::move(descriptor));
    return true;
}

const EditorComponentDescriptor* EditorComponentRegistry::find(std::string_view component_key) const {
    const auto found = m_by_component_key.find(std::string(component_key));
    return found == m_by_component_key.end() ? nullptr : &m_descriptors[found->second];
}

bool EditorComponentRegistry::validate(
    const ComponentRegistry& runtime_registry,
    std::string& error
) const {
    for (const ComponentDescriptor& runtime_descriptor : runtime_registry.descriptors()) {
        if (!find(runtime_descriptor.serialized_key)) {
            error = "Runtime component '" + runtime_descriptor.serialized_key +
                "' is missing editor metadata";
            return false;
        }
    }
    for (const EditorComponentDescriptor& descriptor : m_descriptors) {
        if (!runtime_registry.find_by_key(descriptor.component_key)) {
            error = "Editor metadata references unknown runtime component '" + descriptor.component_key + "'";
            return false;
        }
        std::unordered_set<std::string> constraints;
        for (const std::string& dependency : descriptor.dependencies) {
            if (dependency == descriptor.component_key) {
                error = "Editor component '" + descriptor.component_key + "' depends on itself";
                return false;
            }
            if (!runtime_registry.find_by_key(dependency)) {
                error = "Editor component '" + descriptor.component_key +
                    "' depends on unknown component '" + dependency + "'";
                return false;
            }
            if (!constraints.insert(dependency).second) {
                error = "Editor component '" + descriptor.component_key +
                    "' repeats constraint '" + dependency + "'";
                return false;
            }
        }
        for (const std::string& incompatible : descriptor.incompatible_components) {
            if (incompatible == descriptor.component_key) {
                error = "Editor component '" + descriptor.component_key + "' is incompatible with itself";
                return false;
            }
            if (!runtime_registry.find_by_key(incompatible)) {
                error = "Editor component '" + descriptor.component_key +
                    "' conflicts with unknown component '" + incompatible + "'";
                return false;
            }
            if (!constraints.insert(incompatible).second) {
                error = "Editor component '" + descriptor.component_key +
                    "' repeats constraint '" + incompatible + "'";
                return false;
            }
        }
    }
    return true;
}

EditorComponentAvailability EditorComponentRegistry::availability(
    const EditorComponentDescriptor& descriptor,
    const ComponentRegistry& runtime_registry,
    const EntityManager& em,
    EntityID entity
) const {
    if (!em.is_alive(entity)) return {false, "Entity no longer exists"};
    const ComponentDescriptor* runtime = runtime_registry.find_by_key(descriptor.component_key);
    if (!runtime) return {false, "Runtime component is not registered"};
    if (!descriptor.addable) return {false, "Component cannot be added manually"};
    if (runtime->has(em, entity)) return {false, "Entity already has this component"};

    for (const std::string& dependency : descriptor.dependencies) {
        const ComponentDescriptor* required = runtime_registry.find_by_key(dependency);
        if (!required || !required->has(em, entity)) {
            return {false, "Requires " + dependency};
        }
    }
    for (const std::string& incompatible : descriptor.incompatible_components) {
        const ComponentDescriptor* conflict = runtime_registry.find_by_key(incompatible);
        if (conflict && conflict->has(em, entity)) {
            return {false, "Incompatible with " + incompatible};
        }
    }
    return {true, {}};
}

const char* editor_component_category_label(EditorComponentCategory category) {
    switch (category) {
        case EditorComponentCategory::Core: return "Core";
        case EditorComponentCategory::Rendering: return "Rendering";
        case EditorComponentCategory::Physics: return "Physics";
        case EditorComponentCategory::Animation: return "Animation";
        case EditorComponentCategory::World: return "World";
    }
    return "Other";
}

} // namespace Engine
