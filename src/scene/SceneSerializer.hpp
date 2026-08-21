#pragma once

#include "Scene.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>

namespace Engine {

class ComponentRegistry;

struct SceneEditorMetadata {
    std::unordered_map<EntityID, std::vector<std::string>> component_orders;
};

class SceneSerializer {
public:
    explicit SceneSerializer(
        Scene* scene,
        SDL_Renderer* renderer = nullptr,
        ComponentRegistry* component_registry = nullptr,
        SceneEditorMetadata* editor_metadata = nullptr
    );

    // JSON file serialization
    bool serialize(const std::string& filepath);
    bool deserialize(const std::string& filepath);

    // In-memory string serialization (fast snapshots)
    std::string serialize_to_string();
    bool deserialize_from_string(const std::string& json_text);

    void set_renderer(SDL_Renderer* renderer) { m_renderer = renderer; }

private:
    Scene* m_scene{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    ComponentRegistry* m_component_registry{nullptr};
    SceneEditorMetadata* m_editor_metadata{nullptr};
};

} // namespace Engine
