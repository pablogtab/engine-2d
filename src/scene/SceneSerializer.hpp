#pragma once

#include "Scene.hpp"
#include <string>
#include <SDL3/SDL.h>

namespace Engine {

class SceneSerializer {
public:
    explicit SceneSerializer(Scene* scene, SDL_Renderer* renderer = nullptr);

    // JSON serialization
    bool serialize(const std::string& filepath);
    bool deserialize(const std::string& filepath);

    void set_renderer(SDL_Renderer* renderer) { m_renderer = renderer; }

private:
    Scene* m_scene{nullptr};
    SDL_Renderer* m_renderer{nullptr};
};

} // namespace Engine
