#pragma once

#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../../ecs/EcsTypes.hpp"
#include <string>
#include <memory>
#include <functional>

struct SDL_Renderer;

namespace Engine {

struct EditorSceneSession; // defined in EditorLayer.hpp

/// Encapsulates the lifecycle of editing a .prefab asset in an isolated transient scene.
/// EditorLayer owns exactly one of these and routes relevant shortcuts / menu items through it.
class PrefabStageController {
public:
    PrefabStageController() = default;
    ~PrefabStageController() = default;

    PrefabStageController(const PrefabStageController&) = delete;
    PrefabStageController& operator=(const PrefabStageController&) = delete;

    /// Opens a .prefab file in an isolated stage scene.
    /// Loads the JSON, spawns entities into a fresh transient Scene, then activates it.
    /// Returns false (and changes nothing) if the file is missing, invalid, or another stage is already active.
    [[nodiscard]] bool open(
        const std::string& prefab_path,
        SceneManager* scene_manager,
        SDL_Renderer* renderer
    );

    /// Atomically saves the current prefab stage to disk.
    /// Writes to <prefab_path>.tmp, then renames to <prefab_path>.
    /// Marks command_history clean. Returns false on I/O failure.
    [[nodiscard]] bool save();

    /// If dirty, calls on_need_confirm() and returns false (caller must show a modal).
    /// If clean, calls close_discard() immediately and returns true.
    [[nodiscard]] bool request_close(std::function<void()> on_need_confirm);

    /// Unconditionally exits the transient scene and resets all stage state.
    /// Does NOT save. Called after "Discard" is confirmed.
    void close_discard();

    [[nodiscard]] bool is_active() const { return m_active; }
    [[nodiscard]] bool is_dirty() const;
    [[nodiscard]] const std::string& get_prefab_path() const { return m_prefab_path; }
    [[nodiscard]] const std::string& get_prefab_name() const { return m_prefab_name; }
    [[nodiscard]] EntityID get_root_entity() const { return m_root_entity; }
    [[nodiscard]] Scene* get_stage_scene() const { return m_stage_scene ? m_stage_scene.get() : nullptr; }

    /// Structural protection helpers: the prefab root cannot be deleted or reparented.
    [[nodiscard]] bool is_root_entity(EntityID id) const { return m_active && m_root_entity != 0 && m_root_entity == id; }
    [[nodiscard]] bool can_delete_entity(EntityID id) const { return !is_root_entity(id); }
    [[nodiscard]] bool can_reparent_entity(EntityID id) const { return !is_root_entity(id); }


    // Session accessors — used by EditorLayer::sync_active_scene_session()
    [[nodiscard]] std::shared_ptr<EditorCommandHistory> get_command_history() const { return m_command_history; }
    [[nodiscard]] std::shared_ptr<EditorEntityStateStore> get_entity_state_store() const { return m_entity_state_store; }
    [[nodiscard]] std::shared_ptr<EditorSelection> get_selection() const { return m_selection; }

private:
    bool m_active{false};
    std::string m_prefab_path;
    std::string m_prefab_name;

    SceneManager* m_scene_manager{nullptr};
    SDL_Renderer* m_renderer{nullptr};

    std::shared_ptr<Scene> m_stage_scene;
    EntityID m_root_entity{0};

    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store;
    std::shared_ptr<EditorSelection> m_selection;
};

} // namespace Engine
