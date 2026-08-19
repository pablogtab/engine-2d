#pragma once

#include "EntitySubtreeArchive.hpp"
#include <string>

struct SDL_Renderer;

namespace Engine {

class PrefabSerializer {
public:
    // Saves a root entity and its recursive descendants to a .prefab JSON file.
    [[nodiscard]] static bool save_to_file(
        const EntityManager& em,
        EntityID root_entity,
        const std::string& filepath,
        SDL_Renderer* renderer = nullptr,
        const ComponentRegistry* registry = nullptr
    );

    // Reads the raw JSON text from a .prefab file.
    [[nodiscard]] static std::string read_json_from_file(const std::string& filepath);
};

} // namespace Engine
