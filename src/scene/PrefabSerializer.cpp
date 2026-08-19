#include "PrefabSerializer.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Log.hpp"
#include "../core/AssetManager.hpp"
#include <filesystem>

namespace Engine {

bool PrefabSerializer::save_to_file(
    const EntityManager& em,
    EntityID root_entity,
    const std::string& filepath,
    SDL_Renderer* renderer,
    const ComponentRegistry* registry
) {
    if (filepath.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] Cannot save prefab: empty filepath");
        return false;
    }

    ComponentSerializationContext context{
        .renderer = renderer,
        .asset_manager = &AssetManager::get()
    };

    std::string json_text = EntitySubtreeArchive::capture_to_json(em, root_entity, context, registry);
    if (json_text.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] Failed capturing subtree for root entity #{}", root_entity);
        return false;
    }

    try {
        std::filesystem::path p(filepath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[PREFAB] Failed creating directory for '{}': {}", filepath, e.what());
        return false;
    }

    if (!FileSystem::write_text_atomically(filepath, json_text)) {
        ENGINE_LOG_ERROR("[PREFAB] Failed writing prefab JSON atomically to '{}'", filepath);
        return false;
    }

    ENGINE_LOG_INFO("[PREFAB] Successfully saved prefab to '{}'", filepath);
    return true;

}

std::string PrefabSerializer::read_json_from_file(const std::string& filepath) {
    return FileSystem::read_text(filepath);
}

} // namespace Engine
