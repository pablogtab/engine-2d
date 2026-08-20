#include "PrefabStageController.hpp"
#include "../../scene/PrefabSerializer.hpp"
#include "../../scene/EntitySubtreeArchive.hpp"
#include "../../scene/PrefabPropagation.hpp"
#include "../../scene/ComponentRegistry.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../EditorSelection.hpp"
#include <filesystem>



namespace Engine {

bool PrefabStageController::open(
    const std::string& prefab_path,
    SceneManager* scene_manager,
    SDL_Renderer* renderer
) {
    if (m_active) {
        ENGINE_LOG_WARN("[PREFAB STAGE] open() called while stage is already active — ignoring.");
        return false;
    }
    if (!scene_manager) {
        ENGINE_LOG_ERROR("[PREFAB STAGE] open() requires a valid SceneManager.");
        return false;
    }
    if (prefab_path.empty() || !std::filesystem::exists(prefab_path)) {
        ENGINE_LOG_ERROR("[PREFAB STAGE] Prefab file not found: '{}'", prefab_path);
        return false;
    }

    // Read JSON — if this fails we abort before touching the SceneManager
    std::string json_text = PrefabSerializer::read_json_from_file(prefab_path);
    if (json_text.empty()) {
        ENGINE_LOG_ERROR("[PREFAB STAGE] Failed to read prefab JSON from '{}'", prefab_path);
        return false;
    }

    // Derive display name
    std::filesystem::path p(prefab_path);
    const std::string prefab_name = p.stem().string();
    const std::string stage_name = "Prefab: " + prefab_name;

    // Create the isolated stage scene
    auto stage_scene = std::make_shared<GenericScene>(stage_name);
    EntityManager& stage_em = stage_scene->get_entity_manager();

    ComponentSerializationContext context{
        .renderer = renderer,
        .asset_manager = &AssetManager::get()
    };

    // Instantiate the prefab at world origin, no parent
    SubtreeInstantiationResult result = EntitySubtreeArchive::instantiate_from_json(
        stage_em,
        json_text,
        std::optional<TransformComponent>(TransformComponent{glm::vec3(0.f)}),
        0,
        context,
        &default_component_registry()
    );

    if (!result.success || result.root_entity == 0) {
        ENGINE_LOG_ERROR("[PREFAB STAGE] Failed to instantiate prefab '{}': {}", prefab_path, result.error_message);
        return false;
    }

    // Create isolated session
    auto command_history = std::make_shared<EditorCommandHistory>();
    command_history->reset_clean_baseline(0);
    auto entity_state_store = std::make_shared<EditorEntityStateStore>();
    auto selection = std::make_shared<EditorSelection>();
    selection->set_selection({result.root_entity});

    // Everything succeeded — commit to state
    m_scene_manager     = scene_manager;
    m_renderer          = renderer;
    m_prefab_path       = prefab_path;
    m_prefab_name       = prefab_name;
    m_stage_scene       = std::move(stage_scene);
    m_root_entity       = result.root_entity;
    m_command_history   = std::move(command_history);
    m_entity_state_store = std::move(entity_state_store);
    m_selection         = std::move(selection);
    m_active            = true;

    m_scene_manager->enter_transient_scene(m_stage_scene);

    ENGINE_LOG_INFO("[PREFAB STAGE] Opened '{}' as prefab stage (root entity #{})", prefab_path, m_root_entity);
    return true;
}

bool PrefabStageController::save() {
    if (!m_active || !m_stage_scene) {
        ENGINE_LOG_WARN("[PREFAB STAGE] save() called when stage is not active.");
        return false;
    }

    EntityManager& em = m_stage_scene->get_entity_manager();

    // Ensure transforms are fresh
    TransformSystem::update(em);

    ComponentSerializationContext context{
        .renderer = m_renderer,
        .asset_manager = &AssetManager::get()
    };

    std::string json_text = EntitySubtreeArchive::capture_to_json(
        em, m_root_entity, context, &default_component_registry()
    );

    if (json_text.empty()) {
        ENGINE_LOG_ERROR("[PREFAB STAGE] capture_to_json returned empty — aborting save.");
        return false;
    }

    // Write atomically via FileSystem
    if (!FileSystem::write_text_atomically(m_prefab_path, json_text)) {
        ENGINE_LOG_ERROR("[PREFAB STAGE] Failed writing prefab JSON atomically to '{}'", m_prefab_path);
        return false;
    }

    if (m_command_history) {
        m_command_history->mark_saved();
    }

    // Propagate updated master prefab to authoring scenes
    if (m_scene_manager) {
        for (const auto& scene : m_scene_manager->get_scenes()) {
            if (scene) {
                PrefabPropagation::propagate_to_instances(
                    scene->get_entity_manager(),
                    m_prefab_path,
                    0,
                    m_renderer
                );
            }
        }
    }

    ENGINE_LOG_INFO("[PREFAB STAGE] Saved prefab '{}' atomically.", m_prefab_path);
    return true;
}


bool PrefabStageController::request_close(std::function<void()> on_need_confirm) {
    if (!m_active) return true;
    if (is_dirty()) {
        if (on_need_confirm) on_need_confirm();
        return false; // caller should show modal
    }
    close_discard();
    return true;
}

void PrefabStageController::close_discard() {
    if (!m_active) return;

    if (m_scene_manager && m_scene_manager->is_transient_scene_active()) {
        m_scene_manager->exit_transient_scene();
    }

    m_stage_scene.reset();
    m_command_history.reset();
    m_entity_state_store.reset();
    m_selection.reset();
    m_root_entity = 0;
    m_prefab_path.clear();
    m_prefab_name.clear();
    m_scene_manager = nullptr;
    m_renderer      = nullptr;
    m_active        = false;

    ENGINE_LOG_INFO("[PREFAB STAGE] Stage closed (discarded).");
}

bool PrefabStageController::is_dirty() const {
    return m_command_history && m_command_history->is_dirty();
}

} // namespace Engine
