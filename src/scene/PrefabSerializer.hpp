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

    // Saves a root entity as a Prefab Variant deriving from base_prefab_path.
    [[nodiscard]] static bool save_variant_to_file(
        const EntityManager& em,
        EntityID root_entity,
        const std::string& filepath,
        const std::string& base_prefab_path,
        SDL_Renderer* renderer = nullptr,
        const ComponentRegistry* registry = nullptr
    );

    // Returns true if the file on disk is a Prefab Variant, and optionally populates out_base_path.
    [[nodiscard]] static bool is_variant_file(const std::string& filepath, std::string* out_base_path = nullptr);

    // Stores a resolved prefab document as a variant delta when `filepath` is a
    // variant asset. Normal prefabs receive the full document unchanged.
    [[nodiscard]] static bool write_resolved_to_file(const std::string& filepath, const std::string& resolved_json);

    // Reads the raw JSON text from a .prefab file.
    [[nodiscard]] static std::string read_json_from_file(const std::string& filepath);
};


} // namespace Engine
