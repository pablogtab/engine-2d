#include "EditorLayer.hpp"
#include "EditorTheme.hpp"
#include "ui/EditorUi.hpp"
#include "EditorSelection.hpp"
#include "state/EditorEntityStateStore.hpp"
#include "commands/EditorCommandHistory.hpp"
#include "commands/EntityCommands.hpp"
#include "commands/HierarchyCommands.hpp"
#include "shortcuts/ShortcutManager.hpp"
#include "inspector/InspectorWidgets.hpp"
#include "tools/EditorIcons.hpp"
#include "stage/PrefabStageController.hpp"
#include "../core/Log.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Window.hpp"
#include "../scene/SceneManager.hpp"
#include "../scene/Scene.hpp"
#include "../scene/Scene.hpp"
#include "../scene/SceneSerializer.hpp"
#include "../renderer/LightingBakeService.hpp"
#include "panels/ViewportPanel.hpp"
#include "panels/SceneHierarchyPanel.hpp"
#include "panels/InspectorPanel.hpp"
#include "panels/SceneDiagnosticsPanel.hpp"
#include "panels/SpriteAnimationStudioPanel.hpp"
#include "panels/PostProcessTunerPanel.hpp"
#include "panels/ParticleEffectEditorPanel.hpp"
#include "panels/ProjectSettingsPanel.hpp"
#include "panels/AiAssistantPanel.hpp"
#include "panels/AssetBrowserPanel.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../ecs/CameraSystem.hpp"
#include "../tilemap/Tilemap.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <nlohmann/json.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

namespace Engine {

namespace {

constexpr int MIN_AUTOSAVE_INTERVAL_SECONDS = 15;
constexpr int MAX_AUTOSAVE_INTERVAL_SECONDS = 3600;

std::string workspace_panel_state_key(
    const WorkspaceManager& workspaces,
    EditorPanelId panel
) {
    return std::string("workspace.") + workspaces.active_descriptor().key + "." +
        editor_panel_id_key(panel);
}

std::string normalized_scene_identity(const std::string& path) {
    if (path.empty()) return {};
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? std::filesystem::path(path) : absolute)
        .lexically_normal().generic_string();
}

SceneEditorMetadata capture_scene_editor_metadata(const EditorEntityStateStore* store) {
    SceneEditorMetadata metadata;
    if (!store) return metadata;
    for (const auto& [entity, order] : store->component_orders()) {
        metadata.component_orders.emplace(entity, order);
    }
    return metadata;
}

void apply_scene_editor_metadata(
    const SceneEditorMetadata& metadata,
    EditorEntityStateStore* store
) {
    if (!store) return;
    store->clear_component_orders();
    for (const auto& [entity, order] : metadata.component_orders) {
        store->set_component_order(entity, order);
    }
}

} // namespace

EditorLayer::~EditorLayer() {
    // PrefabStageController must be torn down before EditorLayer members.
    if (m_prefab_stage && m_prefab_stage->is_active()) {
        m_prefab_stage->close_discard();
    }
    m_prefab_stage.reset();
    shutdown();
}

void EditorLayer::register_editor_actions() {
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.new_scene",
        .label = "New Scene",
        .category = "File",
        .execute = [this]() {
            request_destructive_action({
                .type = PendingActionType::NewScene,
                .target = "New Scene",
                .action = [this]() { create_new_scene("New Scene"); }
            });
        }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.save_scene",
        .label = "Save Scene",
        .category = "File",
        .execute = [this]() { static_cast<void>(save_active_scene()); },
        .enabled = [this]() {
            return m_scene_manager && m_scene_manager->get_active_scene() &&
                can_persist_authoring_scenes();
        }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.save_all",
        .label = "Save All Scenes",
        .category = "File",
        .shortcut_id = "editor.save_all_scenes",
        .execute = [this]() { static_cast<void>(save_all_scenes()); },
        .enabled = [this]() {
            return m_scene_manager && !m_scene_manager->get_scenes().empty() &&
                can_persist_authoring_scenes();
        }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.undo",
        .label = "Undo",
        .category = "Edit",
        .execute = [this]() {
            if (m_animation_studio_panel &&
                m_animation_studio_panel->owns_draft_history_focus()) {
                m_animation_studio_panel->undo_from_editor_action();
                return;
            }
            if (m_particle_editor_panel &&
                m_particle_editor_panel->owns_draft_history_focus()) {
                m_particle_editor_panel->undo_from_editor_action();
                return;
            }
            if (m_post_process_tuner_panel &&
                m_post_process_tuner_panel->owns_draft_history_focus()) {
                m_post_process_tuner_panel->undo_from_editor_action();
                return;
            }
            if (m_command_history) m_command_history->undo();
            update_window_title();
        },
        .enabled = [this]() {
            if (m_animation_studio_panel &&
                m_animation_studio_panel->owns_draft_history_focus()) {
                return m_animation_studio_panel->can_undo_draft();
            }
            if (m_particle_editor_panel &&
                m_particle_editor_panel->owns_draft_history_focus()) {
                return m_particle_editor_panel->can_undo_draft();
            }
            if (m_post_process_tuner_panel &&
                m_post_process_tuner_panel->owns_draft_history_focus()) {
                return m_post_process_tuner_panel->can_undo_draft();
            }
            return m_command_history && m_command_history->can_undo();
        }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.redo",
        .label = "Redo",
        .category = "Edit",
        .execute = [this]() {
            if (m_animation_studio_panel &&
                m_animation_studio_panel->owns_draft_history_focus()) {
                m_animation_studio_panel->redo_from_editor_action();
                return;
            }
            if (m_particle_editor_panel &&
                m_particle_editor_panel->owns_draft_history_focus()) {
                m_particle_editor_panel->redo_from_editor_action();
                return;
            }
            if (m_post_process_tuner_panel &&
                m_post_process_tuner_panel->owns_draft_history_focus()) {
                m_post_process_tuner_panel->redo_from_editor_action();
                return;
            }
            if (m_command_history) m_command_history->redo();
            update_window_title();
        },
        .enabled = [this]() {
            if (m_animation_studio_panel &&
                m_animation_studio_panel->owns_draft_history_focus()) {
                return m_animation_studio_panel->can_redo_draft();
            }
            if (m_particle_editor_panel &&
                m_particle_editor_panel->owns_draft_history_focus()) {
                return m_particle_editor_panel->can_redo_draft();
            }
            if (m_post_process_tuner_panel &&
                m_post_process_tuner_panel->owns_draft_history_focus()) {
                return m_post_process_tuner_panel->can_redo_draft();
            }
            return m_command_history && m_command_history->can_redo();
        }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.reset_layout",
        .label = "Reset Current Workspace Layout",
        .category = "Window",
        .execute = [this]() {
            m_workspace_layouts.request_default(m_shell.workspaces().active());
        }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.reset_all_layouts",
        .label = "Reset All Workspace Layouts",
        .category = "Window",
        .execute = [this]() { m_show_reset_all_layouts_modal = true; }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.open_project_settings",
        .label = "Open Project Settings",
        .category = "Window",
        .execute = [this]() {
            if (const auto panel = get_panel(EditorPanelId::ProjectSettings)) panel->is_open = true;
        }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.open_shortcuts",
        .label = "Open Shortcuts",
        .category = "Window",
        .execute = [this]() {
            if (const auto panel = get_panel(EditorPanelId::Shortcuts)) panel->is_open = true;
        }
    }));
    static_cast<void>(m_shell.actions().register_action({
        .id = "editor.open_ui_gallery",
        .label = "Open UI Gallery",
        .category = "Development",
        .execute = [this]() {
            if (const auto panel = get_panel(EditorPanelId::UiGallery)) panel->is_open = true;
        }
    }));
    for (const auto& descriptor : m_shell.workspaces().descriptors()) {
        const EditorWorkspaceId id = descriptor.id;
        const std::string action_id = std::string("workspace.") + descriptor.key;
        const std::string label = std::string("Switch to ") + descriptor.label + " Workspace";
        static_cast<void>(m_shell.actions().register_action({
            .id = action_id,
            .label = label,
            .category = "Workspace",
            .execute = [this, id]() { activate_workspace(id); },
            .checked = [this, id]() { return m_shell.workspaces().active() == id; }
        }));
    }
}

void EditorLayer::init(SDL_Window* window, SDL_Renderer* renderer, const std::filesystem::path& project_root) {
    if (m_initialized) return;

    m_window = window;
    m_renderer = renderer;
    m_project_root = std::filesystem::absolute(project_root).lexically_normal();
    m_recent_scenes_path = m_project_root / "ProjectSettings/EditorRecentScenes.json";
    m_autosave_preferences_path = m_project_root / "ProjectSettings/EditorAutosave.json";
    m_entity_state_preferences_path = m_project_root / "Library/Editor/EntityStates.json";
    m_panel_state_path = m_project_root / "Library/Editor/PanelStates.json";
    m_hot_exit_session_path = m_project_root / "Library/Editor/SessionState.json";
    static_cast<void>(m_panel_states.load(m_panel_state_path.string()));
    m_shell.initialize(m_project_root / "Library/Editor/WorkspaceState.json");
    m_workspace_layouts.initialize(m_project_root, m_shell.workspaces());
    m_saved_panel_state_revision = m_panel_states.revision();
    const std::string autosave_directory = (m_project_root / "Library/Autosaves").string();
    std::snprintf(m_autosave_directory, sizeof(m_autosave_directory), "%s", autosave_directory.c_str());
    m_autosave_recovery.set_directory(autosave_directory);
    static_cast<void>(m_recent_scenes.load(m_recent_scenes_path.string()));
    load_autosave_preferences();
    if (m_recent_scenes.remove_missing_files()) {
        static_cast<void>(m_recent_scenes.save(m_recent_scenes_path.string()));
    }

    // 1. Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    // Own persistence explicitly. dev.sh may terminate the process before
    // Dear ImGui's delayed ini timer or normal shutdown gets a chance to run.
    io.IniFilename = nullptr;
    static_cast<void>(m_workspace_layouts.load(
        m_shell.workspaces().active(), m_shell.workspaces()
    ));
    // 2. Setup the editor type system before merging icon glyphs into the
    // active atlas. The scale remains logical; platform backends handle the
    // framebuffer density separately.
    static_cast<void>(EditorTheme::initialize_fonts());
    static_cast<void>(initialize_editor_icons());
    EditorTheme::apply_dark_theme();

    // 3. Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // 4. Register editor actions once; menus, shortcuts and the command
    // palette invoke the same operations.
    register_editor_actions();

    // 5. Register default bindings in the backend-facing shortcut manager.
    auto& sm = ShortcutManager::get();
    sm.register_shortcut({
        .id = "editor.undo",
        .display_name = "Undo Action",
        .category = "Editing",
        .primary_key = EditorKey::Z,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = true,
        .callback = [this]() { static_cast<void>(m_shell.actions().execute("editor.undo")); }
    });

    sm.register_shortcut({
        .id = "editor.redo",
        .display_name = "Redo Action",
        .category = "Editing",
        .primary_key = EditorKey::Z,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = true,
        .mod_alt = false,
        .enable_repeat = true,
        .callback = [this]() { static_cast<void>(m_shell.actions().execute("editor.redo")); }
    });

    sm.register_shortcut({
        .id = "editor.save_scene",
        .display_name = "Save Scene",
        .category = "File",
        .primary_key = EditorKey::S,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() { static_cast<void>(m_shell.actions().execute("editor.save_scene")); }
    });

    sm.register_shortcut({
        .id = "editor.save_all_scenes",
        .display_name = "Save All Scenes",
        .category = "File",
        .primary_key = EditorKey::S,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = true,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() { static_cast<void>(m_shell.actions().execute("editor.save_all")); }
    });

    sm.register_shortcut({
        .id = "editor.new_scene",
        .display_name = "New Scene",
        .category = "File",
        .primary_key = EditorKey::N,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() { static_cast<void>(m_shell.actions().execute("editor.new_scene")); }
    });

    sm.register_shortcut({
        .id = "editor.duplicate",
        .display_name = "Duplicate Entities",
        .category = "Editing",
        .primary_key = EditorKey::D,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = nullptr
    });

    sm.register_shortcut({
        .id = "editor.delete",
        .display_name = "Delete Selected Entities",
        .category = "Editing",
        .primary_key = EditorKey::Backspace,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = nullptr
    });

    sm.register_shortcut({
        .id = "editor.group_selection",
        .display_name = "Group Selection",
        .category = "Hierarchy",
        .primary_key = EditorKey::G,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() {
            Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
            if (active_scene && m_selection && !m_selection->empty() && m_command_history) {
                EntityManager& em = active_scene->get_entity_manager();
                std::vector<EntityID> selected_vec(m_selection->get_selected().begin(), m_selection->get_selected().end());
                m_command_history->execute_command(std::make_unique<GroupSelectedEntitiesCommand>(&em, m_selection, selected_vec));
                update_window_title();
            }
        }
    });

    sm.register_shortcut({
        .id = "editor.command_palette",
        .display_name = "Open Command Palette",
        .category = "General",
        .primary_key = EditorKey::P,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = true,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() { m_shell.request_command_palette(); }
    });

    sm.register_shortcut({
        .id = "editor.ungroup_selection",
        .display_name = "Ungroup Selection",
        .category = "Hierarchy",
        .primary_key = EditorKey::G,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = true,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() {
            Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
            if (active_scene && m_selection && !m_selection->empty() && m_command_history) {
                EntityManager& em = active_scene->get_entity_manager();
                std::vector<EntityID> selected_vec(m_selection->get_selected().begin(), m_selection->get_selected().end());
                m_command_history->execute_command(std::make_unique<UngroupEntitiesCommand>(&em, m_selection, selected_vec));
                update_window_title();
            }
        }
    });

    sm.register_shortcut({
        .id = "editor.focus_camera",
        .display_name = "Frame Selection",
        .category = "Camera",
        .primary_key = EditorKey::F,
        .mod_cmd = false,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = nullptr
    });

    sm.register_shortcut({
        .id = "editor.frame_all",
        .display_name = "Frame All Visible Entities",
        .category = "Camera",
        .primary_key = EditorKey::F,
        .mod_cmd = false,
        .mod_ctrl = false,
        .mod_shift = true,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = nullptr
    });

    sm.register_shortcut({
        .id = "editor.reset_view",
        .display_name = "Reset Viewport Camera",
        .category = "Camera",
        .primary_key = EditorKey::Home,
        .mod_cmd = false,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = nullptr
    });

    sm.register_shortcut({
        .id = "editor.ai_attach_selection",
        .display_name = "Attach Selection to AI Assistant",
        .category = "AI Assistant",
        .primary_key = EditorKey::L,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() {
            const auto assistant = std::dynamic_pointer_cast<AiAssistantPanel>(
                get_panel("AI Assistant")
            );
            if (!assistant) return;
            const auto asset_browser = std::dynamic_pointer_cast<AssetBrowserPanel>(
                get_panel("Asset Browser")
            );
            if (asset_browser && asset_browser->is_focused()) {
                assistant->request_attach_assets(
                    asset_browser->selected_assets_for_context()
                );
                return;
            }
            assistant->request_attach_selection();
        }
    });

    sm.load_from_file((m_project_root / "ProjectSettings/EditorShortcuts.json").string());

    m_prefab_stage = std::make_unique<PrefabStageController>();

    m_initialized = true;
    ENGINE_LOG_INFO("[EDITOR] Dear ImGui initialized successfully (Docking Enabled).");
}

void EditorLayer::shutdown() {
    if (!m_initialized) return;

    if (m_scene_manager && m_scene_manager->is_play_session_active()) {
        m_scene_manager->stop_play();
    }
    save_hot_exit_session();
    save_entity_state_preferences();
    persist_panel_states();
    m_workspace_layouts.persist();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    m_initialized = false;
    ENGINE_LOG_INFO("[EDITOR] Dear ImGui shutdown cleanly.");
}

void EditorLayer::load_autosave_preferences() {
    if (!std::filesystem::exists(m_autosave_preferences_path)) return;
    try {
        const nlohmann::json preferences = nlohmann::json::parse(FileSystem::read_text(m_autosave_preferences_path.string()));
        m_autosave_interval_seconds = std::clamp(
            preferences.value("interval_seconds", m_autosave_interval_seconds),
            MIN_AUTOSAVE_INTERVAL_SECONDS,
            MAX_AUTOSAVE_INTERVAL_SECONDS
        );
        const std::string directory = preferences.value("directory", std::string{m_autosave_directory});
        if (!directory.empty() && directory.size() < sizeof(m_autosave_directory)) {
            std::snprintf(m_autosave_directory, sizeof(m_autosave_directory), "%s", directory.c_str());
            m_autosave_recovery.set_directory(directory);
        }
    } catch (const std::exception& exception) {
        ENGINE_LOG_WARN("[AUTOSAVE] Ignoring invalid editor preferences: {}", exception.what());
    }
}

void EditorLayer::save_autosave_preferences() const {
    const nlohmann::json preferences{
        {"interval_seconds", m_autosave_interval_seconds},
        {"directory", m_autosave_directory}
    };
    if (!FileSystem::write_text_atomically(m_autosave_preferences_path.string(), preferences.dump(2))) {
        ENGINE_LOG_ERROR("[AUTOSAVE] Failed saving editor preferences");
    }
}

void EditorLayer::load_entity_state_preferences(
    const Scene* scene,
    EditorSceneSession& session
) const {
    if (!scene || !session.entity_state_store || session.autosave_identity.empty() ||
        !std::filesystem::exists(m_entity_state_preferences_path)) {
        return;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(
            FileSystem::read_text(m_entity_state_preferences_path.string())
        );
        const auto scenes_it = root.find("scenes");
        if (scenes_it == root.end() || !scenes_it->is_object()) return;
        const auto scene_it = scenes_it->find(session.autosave_identity);
        if (scene_it == scenes_it->end() || !scene_it->is_array()) return;

        // Component ordering is scene metadata. Only the ephemeral visibility
        // and lock fields are replaced from the local editor preference file.
        for (const auto& [entity, previous] : session.entity_state_store->states()) {
            EditorEntityState reset = previous;
            reset.locked = false;
            reset.visible_in_editor = true;
            reset.component_open_states.clear();
            session.entity_state_store->set(entity, std::move(reset));
        }
        const EntityManager& em = scene->get_entity_manager();
        for (const nlohmann::json& entry : *scene_it) {
            if (!entry.is_object()) continue;
            const EntityID entity = entry.value("entity", EntityID{0});
            if (entity == 0 || !em.is_alive(entity)) continue;
            EditorEntityState state = session.entity_state_store->get(entity);
            state.locked = entry.value("locked", false);
            state.visible_in_editor = entry.value("visible", true);
            if (const auto component_open = entry.find("component_open");
                component_open != entry.end() && component_open->is_object()) {
                for (const auto& [component_key, open_value] : component_open->items()) {
                    if (open_value.is_boolean()) {
                        state.component_open_states[component_key] = open_value.get<bool>();
                    }
                }
            }
            session.entity_state_store->set(entity, std::move(state));
        }
    } catch (const std::exception& exception) {
        ENGINE_LOG_WARN("[EDITOR] Ignoring invalid entity-state preferences: {}", exception.what());
    }
}

void EditorLayer::save_entity_state_preferences() {
    nlohmann::json root{{"version", 2}, {"scenes", nlohmann::json::object()}};
    if (std::filesystem::exists(m_entity_state_preferences_path)) {
        try {
            nlohmann::json existing = nlohmann::json::parse(
                FileSystem::read_text(m_entity_state_preferences_path.string())
            );
            if (existing.is_object()) root = std::move(existing);
        } catch (const std::exception&) {
            // Replace malformed local state; it is cache-like editor data.
        }
    }
    root["version"] = 2;
    if (!root.contains("scenes") || !root["scenes"].is_object()) {
        root["scenes"] = nlohmann::json::object();
    }

    for (const auto& [scene, session] : m_sessions.sessions()) {
        if (!scene || !session.entity_state_store || session.autosave_identity.empty()) continue;
        nlohmann::json entries = nlohmann::json::array();
        const EntityManager& em = scene->get_entity_manager();
        for (const auto& [entity, state] : session.entity_state_store->states()) {
            if (!em.is_alive(entity) || (!state.locked && state.visible_in_editor &&
                state.component_open_states.empty())) continue;
            nlohmann::json entry{
                {"entity", entity},
                {"locked", state.locked},
                {"visible", state.visible_in_editor}
            };
            if (!state.component_open_states.empty()) {
                entry["component_open"] = state.component_open_states;
            }
            entries.push_back(std::move(entry));
        }
        if (entries.empty()) {
            root["scenes"].erase(session.autosave_identity);
        } else {
            root["scenes"][session.autosave_identity] = std::move(entries);
        }
    }

    std::error_code error;
    std::filesystem::create_directories(m_entity_state_preferences_path.parent_path(), error);
    if (error || !FileSystem::write_text_atomically(
            m_entity_state_preferences_path.string(), root.dump(2))) {
        ENGINE_LOG_ERROR("[EDITOR] Failed saving entity-state preferences");
        return;
    }
    for (auto& [_, session] : m_sessions.sessions()) {
        if (session.entity_state_store) {
            session.persisted_entity_state_revision =
                session.entity_state_store->persistence_revision();
        }
    }
}

void EditorLayer::process_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            InspectorWidgets::request_cancel_active_property();
        }
        if (event.key.scancode == SDL_SCANCODE_F1 || event.key.scancode == SDL_SCANCODE_GRAVE) {
            toggle_visibility();
            ENGINE_LOG_INFO("[EDITOR] Toggled visibility: {}", m_visible ? "ON" : "OFF");
        }
    }

    if (m_visible) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}

bool EditorLayer::want_capture_mouse() const {
    if (!m_initialized || !m_visible) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool EditorLayer::want_capture_keyboard() const {
    if (!m_initialized || !m_visible) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool EditorLayer::want_text_input() const {
    if (!m_initialized || !m_visible) return false;
    return ImGui::GetIO().WantTextInput;
}

EditorSceneSession& EditorLayer::get_or_create_session(const Scene* scene, const std::string& default_name) {
    if (!scene) {
        static EditorSceneSession fallback_session;
        if (!fallback_session.command_history) {
            fallback_session.command_history = std::make_shared<EditorCommandHistory>();
        }
        if (!fallback_session.entity_state_store) {
            fallback_session.entity_state_store = std::make_shared<EditorEntityStateStore>();
        }
        return fallback_session;
    }

    if (EditorSceneSession* existing = m_sessions.find(scene)) return *existing;

    EditorSceneSession& created = m_sessions.ensure(scene, [this, scene, &default_name]() {
        EditorSceneSession session;
        session.command_history = std::make_shared<EditorCommandHistory>();
        session.command_history->reset_clean_baseline(0);
        session.entity_state_store = std::make_shared<EditorEntityStateStore>();
        session.selection = std::make_shared<EditorSelection>();

        std::string scene_name = scene->get_name();
        if (scene_name.empty()) scene_name = default_name;
        const std::filesystem::path expected_path = scenes_directory() / (scene_name + ".scene");
        if (std::filesystem::exists(expected_path)) {
            session.file_path = expected_path.string();
        } else {
            // Save initial snapshot for rollback on discard
            SceneEditorMetadata editor_metadata = capture_scene_editor_metadata(session.entity_state_store.get());
            SceneSerializer serializer(
                const_cast<Scene*>(scene), m_renderer, nullptr, &editor_metadata
            );
            session.initial_snapshot_json = serializer.serialize_to_string();
        }
        session.autosave_identity = !session.file_path.empty()
            ? normalized_scene_identity(session.file_path)
            : "untitled:" + scene_name + ":" + std::to_string(reinterpret_cast<std::uintptr_t>(scene));

        return session;
    });
    refresh_scene_source_stamp(created);
    load_entity_state_preferences(scene, created);
    created.persisted_entity_state_revision =
        created.entity_state_store->persistence_revision();
    return created;
}

void EditorLayer::bind_scene_path(const Scene* scene, const std::string& path) {
    if (!scene || path.empty()) return;
    EditorSceneSession& session = get_or_create_session(scene, scene->get_name());
    session.file_path = path;
    session.autosave_identity = normalized_scene_identity(path);
    session.external_source_conflicted = false;
    refresh_scene_source_stamp(session);
    load_entity_state_preferences(scene, session);
    if (session.entity_state_store) {
        session.persisted_entity_state_revision =
            session.entity_state_store->persistence_revision();
    }
}

void EditorLayer::update_panel_sessions(std::shared_ptr<EditorCommandHistory> ch, std::shared_ptr<EditorEntityStateStore> store, std::shared_ptr<EditorSelection> sel) {
    const EditorPanelContext context{
        .command_history = std::move(ch),
        .entity_state_store = std::move(store),
        .selection = sel ? std::move(sel) : m_selection
    };
    m_panel_registry.broadcast_context(context);
}

void EditorLayer::update_panel_command_histories(std::shared_ptr<EditorCommandHistory> ch) {
    update_panel_sessions(ch, m_entity_state_store);
}

void EditorLayer::cleanup_stale_sessions() {
    if (!m_scene_manager) return;
    const auto& scenes = m_scene_manager->get_scenes();
    m_sessions.prune([&scenes](const Scene* scene) {
        return std::any_of(scenes.begin(), scenes.end(), [scene](const std::shared_ptr<Scene>& candidate) {
            return candidate.get() == scene;
        });
    });
}

void EditorLayer::sync_active_scene_session() {
    if (!m_scene_manager) return;

    // --- Prefab Stage takes priority ---
    if (m_prefab_stage && m_prefab_stage->is_active()) {
        // Bind stage session to all panels
        auto ch    = m_prefab_stage->get_command_history();
        auto store = m_prefab_stage->get_entity_state_store();
        auto sel   = m_prefab_stage->get_selection();
        if (ch != m_command_history || store != m_entity_state_store || sel != m_selection) {
            m_command_history    = ch;
            m_entity_state_store = store;
            m_selection          = sel;
            update_panel_sessions(ch, store, sel);
            update_window_title();
        }
        return;
    }

    // --- Normal scene session ---
    Scene* active_scene = m_scene_manager->get_active_scene();
    if (!active_scene) return;

    cleanup_stale_sessions();
    if (m_sessions.tabs_empty()) open_scene_tab(active_scene);

    if (active_scene != m_last_active_scene || m_command_history == nullptr || m_entity_state_store == nullptr) {
        m_last_active_scene = active_scene;
        auto& session = get_or_create_session(active_scene);
        m_command_history    = session.command_history;
        m_entity_state_store = session.entity_state_store;
        m_selection          = session.selection;
        if (m_entity_state_store) {
            m_entity_state_store->prune(active_scene->get_entity_manager());
        }
        update_panel_sessions(m_command_history, m_entity_state_store, m_selection);
        update_window_title();
    }
}

void EditorLayer::update_window_title() {
    if (!m_window_wrapper) return;

    // Prefab Stage title takes precedence
    if (m_prefab_stage && m_prefab_stage->is_active()) {
        std::string title = "Engine2D [Prefab: " + m_prefab_stage->get_prefab_name() + "]";
        if (m_prefab_stage->is_dirty()) title += " *";
        m_window_wrapper->set_title(title);
        return;
    }

    std::string title = "Engine2D";
    if (m_scene_manager && m_scene_manager->get_active_scene()) {
        Scene* scene = m_scene_manager->get_active_scene();
        title += " - " + scene->get_name();
        auto& session = get_or_create_session(scene);
        if (session.command_history && session.command_history->is_dirty()) {
            title += " *";
        }
    }
    m_window_wrapper->set_title(title);
}

bool EditorLayer::request_destructive_action(PendingDestructiveAction action) {
    // If a dirty prefab stage is active, block and show the stage exit modal.
    if (m_prefab_stage && m_prefab_stage->is_active() && m_prefab_stage->is_dirty()) {
        m_pending_action = std::move(action);
        m_show_prefab_stage_exit_modal = true;
        return false;
    }
    // If a prefab stage is active but clean, silently close it first.
    if (m_prefab_stage && m_prefab_stage->is_active()) {
        m_prefab_stage->close_discard();
        m_last_active_scene = nullptr; // force session resync
        sync_active_scene_session();
    }

    if (!m_scene_manager) {
        if (action.action) action.action();
        return true;
    }

    auto active_scene = m_scene_manager->get_active_scene();
    if (!active_scene) {
        if (action.action) action.action();
        return true;
    }

    auto& session = get_or_create_session(active_scene);
    if (session.command_history && session.command_history->is_dirty()) {
        m_pending_action = std::move(action);
        m_show_unsaved_changes_modal = true;
        return false;
    }

    if (action.action) {
        action.action();
    }
    return true;
}

bool EditorLayer::request_exit() {
    if (m_prefab_stage && m_prefab_stage->is_active() && m_prefab_stage->is_dirty()) {
        return request_destructive_action({
            .type = PendingActionType::ExitApp,
            .target = "",
            .action = [this]() {
                if (m_scene_manager && m_scene_manager->is_play_session_active()) {
                    m_scene_manager->stop_play();
                }
                save_hot_exit_session();
                if (m_window_wrapper) m_window_wrapper->force_close();
            }
        });
    }
    if (m_scene_manager && m_scene_manager->is_play_session_active()) {
        m_scene_manager->stop_play();
    }
    save_hot_exit_session();
    if (m_window_wrapper) m_window_wrapper->force_close();
    return true;
}

bool EditorLayer::request_project_hub() {
    return request_destructive_action({
        .type = PendingActionType::ExitApp,
        .target = "Project Hub",
        .action = [this]() {
            if (m_project_hub_request_callback) m_project_hub_request_callback();
        }
    });
}

void EditorLayer::begin_frame() {
    if (!m_initialized) return;
    update_lighting_bakes();
    if (!m_visible) return;

    sync_active_scene_session();
    restore_hot_exit_session();
    sync_active_scene_session();
    update_window_title();

    // While the running game owns the focused viewport, arrow keys and other
    // gameplay input must not also move Dear ImGui's keyboard-navigation cursor.
    ImGuiIO& io = ImGui::GetIO();
    const bool gameplay_owns_keyboard = m_scene_manager && !m_scene_manager->is_editing() &&
        m_viewport_panel && m_viewport_panel->is_viewport_focused();
    if (gameplay_owns_keyboard) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Process all registered actions with smart acceleration curves
    float dt = ImGui::GetIO().DeltaTime;
    ShortcutManager::get().update(dt, want_text_input());
}

void EditorLayer::apply_workspace_panel_visibility(bool restore_saved) {
    for (const auto& panel : m_panel_registry.panels()) {
        if (!panel || panel->get_id() == EditorPanelId::ProjectSettings ||
            panel->get_id() == EditorPanelId::Shortcuts ||
            panel->get_id() == EditorPanelId::UiGallery) {
            continue;
        }
        const bool default_open = m_shell.workspaces().contains(panel->get_id());
        panel->is_open = restore_saved
            ? m_panel_states.is_open(
                workspace_panel_state_key(m_shell.workspaces(), panel->get_id()),
                default_open
            )
            : default_open;
    }
}

void EditorLayer::persist_panel_states() {
    for (const auto& panel : m_panel_registry.panels()) {
        if (panel && panel->get_id() != EditorPanelId::ProjectSettings &&
            panel->get_id() != EditorPanelId::Shortcuts) {
            m_panel_states.set_open(
                workspace_panel_state_key(m_shell.workspaces(), panel->get_id()),
                panel->is_open
            );
        }
    }
    if (m_panel_states.revision() == m_saved_panel_state_revision) return;
    if (m_panel_states.save(m_panel_state_path.string())) {
        m_saved_panel_state_revision = m_panel_states.revision();
    }
}

bool EditorLayer::can_persist_authoring_scenes() const {
    return !m_scene_manager || !m_scene_manager->is_play_session_active();
}

bool EditorLayer::save_hot_exit_session() {
    if (!m_scene_manager) return true;
    if (!can_persist_authoring_scenes()) return true;

    nlohmann::json scenes = nlohmann::json::array();
    std::string active_identity;
    Scene* active_scene = m_scene_manager->get_active_scene();
    std::vector<std::string> captured_identities;

    for (const auto& scene : m_scene_manager->get_scenes()) {
        if (!scene) continue;
        EditorSceneSession& session = get_or_create_session(scene.get(), scene->get_name());
        const bool dirty = session.command_history && session.command_history->is_dirty();
        const bool needs_snapshot = dirty || session.file_path.empty();
        nlohmann::json entry{
            {"identity", session.autosave_identity},
            {"source_path", session.file_path},
            {"scene_name", scene->get_name()},
            {"dirty", needs_snapshot},
            {"open", is_scene_tab_open(scene.get())},
            {"selection", nlohmann::json::array()}
        };
        if (session.selection) {
            std::vector<EntityID> selected(
                session.selection->get_selected().begin(),
                session.selection->get_selected().end()
            );
            std::sort(selected.begin(), selected.end());
            entry["selection"] = selected;
        }
        if (needs_snapshot) {
            SceneEditorMetadata metadata = capture_scene_editor_metadata(
                session.entity_state_store.get()
            );
            entry["snapshot_json"] = SceneSerializer(
                scene.get(), m_renderer, nullptr, &metadata
            ).serialize_to_string();
        }
        if (scene.get() == active_scene) active_identity = session.autosave_identity;
        captured_identities.push_back(session.autosave_identity);
        scenes.push_back(std::move(entry));
    }

    nlohmann::json selected_panels = nlohmann::json::array();
    if (ImGui::GetCurrentContext()) {
        for (const auto& panel : m_panel_registry.panels()) {
            if (!panel || !panel->is_open) continue;
            ImGuiWindow* window = ImGui::FindWindowByName(panel->get_name().c_str());
            if (window && window->DockNode &&
                window->DockNode->SelectedTabId == window->TabId) {
                selected_panels.push_back(panel->get_name());
            }
        }
    }

    nlohmann::json viewport_camera = nullptr;
    if (m_scene_manager->is_editing()) {
        const Camera2D* camera = m_scene_manager->get_render_camera(RenderViewKind::Editor);
        if (camera) {
            const glm::vec2 position = camera->get_position();
            viewport_camera = {
                {"position", {position.x, position.y}},
                {"zoom", camera->get_zoom()}
            };
        }
    }
    if (viewport_camera.is_null() && std::filesystem::exists(m_hot_exit_session_path)) {
        try {
            const nlohmann::json previous = nlohmann::json::parse(
                FileSystem::read_text(m_hot_exit_session_path.string())
            );
            if (previous.contains("viewport_camera")) {
                viewport_camera = previous["viewport_camera"];
            }
        } catch (const std::exception&) {
            // A fresh atomic checkpoint below replaces malformed session data.
        }
    }

    const nlohmann::json root{
        {"version", 1},
        {"active_scene", active_identity},
        {"selected_panels", std::move(selected_panels)},
        {"viewport_camera", std::move(viewport_camera)},
        {"scenes", std::move(scenes)}
    };
    std::error_code error;
    std::filesystem::create_directories(m_hot_exit_session_path.parent_path(), error);
    if (error || !FileSystem::write_text_atomically(
            m_hot_exit_session_path.string(), root.dump(2))) {
        ENGINE_LOG_ERROR("[EDITOR] Failed saving hot-exit session");
        return false;
    }
    for (const std::string& identity : captured_identities) {
        static_cast<void>(m_autosave_recovery.remove(identity));
    }
    return true;
}

void EditorLayer::restore_hot_exit_session() {
    if (m_hot_exit_restored) return;
    m_hot_exit_restored = true;
    if (!m_scene_manager || !std::filesystem::exists(m_hot_exit_session_path)) return;

    try {
        const nlohmann::json root = nlohmann::json::parse(
            FileSystem::read_text(m_hot_exit_session_path.string())
        );
        if (root.value("version", 0) != 1 || !root.contains("scenes") ||
            !root["scenes"].is_array()) return;

        m_panels_to_reselect.clear();
        if (root.contains("selected_panels") && root["selected_panels"].is_array()) {
            for (const nlohmann::json& panel : root["selected_panels"]) {
                if (panel.is_string()) m_panels_to_reselect.push_back(panel.get<std::string>());
            }
        }

        std::unordered_map<std::string, Scene*> restored_scenes;
        std::vector<std::string> open_identities;
        for (const nlohmann::json& entry : root["scenes"]) {
            if (!entry.is_object()) continue;
            const std::string identity = entry.value("identity", "");
            const std::string source_path = entry.value("source_path", "");
            const std::string scene_name = entry.value("scene_name", "Scene");
            const std::string snapshot = entry.value("snapshot_json", "");
            Scene* target = nullptr;

            for (const auto& candidate : m_scene_manager->get_scenes()) {
                if (!candidate) continue;
                const auto session_it = m_sessions.sessions().find(candidate.get());
                if (session_it != m_sessions.sessions().end() &&
                    session_it->second.autosave_identity == identity) {
                    target = candidate.get();
                    break;
                }
            }
            if (!target && !source_path.empty() && std::filesystem::exists(source_path)) {
                load_scene(source_path);
                for (const auto& candidate : m_scene_manager->get_scenes()) {
                    if (!candidate) continue;
                    const auto session_it = m_sessions.sessions().find(candidate.get());
                    if (session_it != m_sessions.sessions().end() &&
                        session_it->second.autosave_identity == normalized_scene_identity(source_path)) {
                        target = candidate.get();
                        break;
                    }
                }
            }
            if (!target && !snapshot.empty()) {
                auto restored = std::make_shared<Scene>(scene_name);
                SceneEditorMetadata metadata;
                if (!SceneSerializer(restored.get(), m_renderer, nullptr, &metadata)
                        .deserialize_from_string(snapshot)) continue;
                m_scene_manager->add_scene(restored);
                target = restored.get();
                EditorSceneSession& session = get_or_create_session(target, scene_name);
                session.file_path = source_path;
                session.autosave_identity = identity;
                apply_scene_editor_metadata(metadata, session.entity_state_store.get());
            }
            if (!target) continue;

            EditorSceneSession& session = get_or_create_session(target, scene_name);
            if (!source_path.empty() && session.autosave_identity != identity) {
                bind_scene_path(target, source_path);
            } else if (!identity.empty()) {
                session.autosave_identity = identity;
            }
            if (entry.value("dirty", false) && !snapshot.empty()) {
                SceneEditorMetadata metadata;
                if (SceneSerializer(target, m_renderer, nullptr, &metadata)
                        .deserialize_from_string(snapshot)) {
                    apply_scene_editor_metadata(metadata, session.entity_state_store.get());
                    session.command_history->reset_clean_baseline(0);
                    session.command_history->mark_recovered();
                }
            }
            if (session.selection && entry.contains("selection") &&
                entry["selection"].is_array()) {
                std::unordered_set<EntityID> selected;
                const EntityManager& em = target->get_entity_manager();
                for (const nlohmann::json& selected_id : entry["selection"]) {
                    if (!selected_id.is_number_unsigned()) continue;
                    const EntityID entity = selected_id.get<EntityID>();
                    if (em.is_alive(entity)) selected.insert(entity);
                }
                session.selection->set_selection(selected);
            }
            restored_scenes[identity] = target;
            if (entry.value("open", false)) open_identities.push_back(identity);
        }

        m_sessions.clear_tabs();
        for (const std::string& identity : open_identities) {
            const auto restored = restored_scenes.find(identity);
            if (restored != restored_scenes.end()) open_scene_tab(restored->second);
        }
        const std::string active_identity = root.value("active_scene", "");
        const auto active = restored_scenes.find(active_identity);
        if (active != restored_scenes.end()) {
            const auto& scenes_list = m_scene_manager->get_scenes();
            const auto scene_it = std::find_if(
                scenes_list.begin(), scenes_list.end(), [&active](const auto& scene) {
                    return scene && scene.get() == active->second;
                }
            );
            if (scene_it != scenes_list.end()) {
                activate_scene(static_cast<std::size_t>(
                    std::distance(scenes_list.begin(), scene_it)
                ));
            }
        }
        if (root.contains("viewport_camera") && root["viewport_camera"].is_object()) {
            const nlohmann::json& viewport = root["viewport_camera"];
            const auto position = viewport.find("position");
            const float zoom = viewport.value("zoom", 1.0f);
            if (position != viewport.end() && position->is_array() &&
                position->size() == 2 && (*position)[0].is_number() &&
                (*position)[1].is_number() && std::isfinite(zoom)) {
                const glm::vec2 restored_position{
                    (*position)[0].get<float>(), (*position)[1].get<float>()
                };
                if (std::isfinite(restored_position.x) &&
                    std::isfinite(restored_position.y)) {
                    if (Camera2D* camera = m_scene_manager->get_render_camera(
                            RenderViewKind::Editor)) {
                        camera->set_position(restored_position);
                        camera->set_zoom(zoom);
                    }
                }
            }
        }
        ENGINE_LOG_INFO("[EDITOR] Restored hot-exit session");
    } catch (const std::exception& exception) {
        ENGINE_LOG_WARN("[EDITOR] Ignoring invalid hot-exit session: {}", exception.what());
    }
}

void EditorLayer::create_new_scene(const std::string& name) {
    if (!m_scene_manager) return;
    auto new_scene = std::make_shared<Scene>(name);
    EntityManager& entity_manager = new_scene->get_entity_manager();
    const EntityID camera_entity = entity_manager.create_entity();
    entity_manager.add_component<TagComponent>(camera_entity, TagComponent{"Main Camera"});
    entity_manager.add_component<TransformComponent>(camera_entity, TransformComponent{glm::vec3(0.0f)});
    CameraComponent camera;
    camera.is_primary = true;
    entity_manager.add_component<CameraComponent>(camera_entity, camera);
    m_scene_manager->add_scene(new_scene);

    auto& session = get_or_create_session(new_scene.get(), name);
    session.command_history->reset_clean_baseline(0);
    session.file_path = "";
    SceneEditorMetadata editor_metadata = capture_scene_editor_metadata(session.entity_state_store.get());
    session.initial_snapshot_json = SceneSerializer(
        new_scene.get(), m_renderer, nullptr, &editor_metadata
    ).serialize_to_string();
    session.autosave_identity = "untitled:" + name + ":" + std::to_string(reinterpret_cast<std::uintptr_t>(new_scene.get()));

    const auto& scenes = m_scene_manager->get_scenes();
    const auto scene_it = std::find_if(scenes.begin(), scenes.end(), [&new_scene](const std::shared_ptr<Scene>& scene) {
        return scene.get() == new_scene.get();
    });
    if (scene_it != scenes.end()) {
        activate_scene(static_cast<std::size_t>(std::distance(scenes.begin(), scene_it)));
    }
    ENGINE_LOG_INFO("[EDITOR] Created new blank scene: '{}'", name);
}

bool EditorLayer::save_scene(Scene* scene, EditorSceneSession& session, const std::string& custom_path) {
    if (!scene) return false;
    if (!can_persist_authoring_scenes()) {
        ENGINE_LOG_WARN("[EDITOR] Scene saving is disabled during Play mode");
        return false;
    }
    if (session.recovery_source_conflicted && custom_path.empty()) {
        ENGINE_LOG_WARN("[AUTOSAVE] Recovered scene '{}' must use Save As because its source changed externally", scene->get_name());
        m_show_save_as_popup = true;
        return false;
    }
    if (session.external_source_conflicted && custom_path.empty()) {
        m_external_conflict_scene = scene;
        ENGINE_LOG_WARN("[EDITOR] Scene '{}' changed on disk; reload it or use Save As before saving",
                        scene->get_name());
        return false;
    }

    std::string path = custom_path;
    if (path.empty()) {
        if (!session.file_path.empty()) {
            path = session.file_path;
        } else {
            std::filesystem::create_directories(scenes_directory());
            path = (scenes_directory() / (scene->get_name() + ".scene")).string();
        }
    }

    SceneEditorMetadata editor_metadata = capture_scene_editor_metadata(session.entity_state_store.get());
    SceneSerializer serializer(scene, m_renderer, nullptr, &editor_metadata);
    if (!serializer.serialize(path)) {
        ENGINE_LOG_ERROR("[EDITOR] Failed to save scene '{}' to '{}'", scene->get_name(), path);
        return false;
    }

    session.file_path = path;
    session.initial_snapshot_json.clear();
    session.recovery_source_conflicted = false;
    session.external_source_conflicted = false;
    if (session.command_history) session.command_history->mark_saved();
    static_cast<void>(m_autosave_recovery.remove(session.autosave_identity));
    session.autosave_identity = normalized_scene_identity(path);
    refresh_scene_source_stamp(session);
    save_entity_state_preferences();
    record_recent_scene(path);
    schedule_lighting_bake(path, std::chrono::milliseconds(500));
    ENGINE_LOG_INFO("[EDITOR] Successfully saved scene '{}' to '{}'", scene->get_name(), path);
    return true;
}

void EditorLayer::schedule_lighting_bake(
    const std::filesystem::path& scene_path,
    std::chrono::milliseconds delay
) {
    const LightingBakeStatus status = LightingBakeService::status(m_project_root, scene_path);
    const std::string key = std::filesystem::absolute(scene_path).lexically_normal().generic_string();
    if (!status.has_bake_volume) {
        m_lighting_bake_jobs.erase(key);
        return;
    }
    if (status.current && delay.count() > 0) {
        m_lighting_bake_jobs.erase(key);
        return;
    }
    LightingBakeJob& job = m_lighting_bake_jobs[key];
    job.deadline = std::chrono::steady_clock::now() + delay;
    job.queued = true;
    ENGINE_LOG_INFO("[LIGHTING BAKE] '{}' marked stale; background bake queued in {} ms",
                    scene_path.filename().string(), delay.count());
}

void EditorLayer::update_lighting_bakes() {
    const auto now = std::chrono::steady_clock::now();
    for (auto job_it = m_lighting_bake_jobs.begin(); job_it != m_lighting_bake_jobs.end();) {
        LightingBakeJob& job = job_it->second;
        if (job.running && job.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            LightingBakeJobResult result = job.future.get();
            job.running = false;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - job.started_at);
            const std::string scene_name = std::filesystem::path(job_it->first).filename().string();
            if (result.success) {
                ENGINE_LOG_INFO(
                    "[LIGHTING BAKE] Completed '{}' in {} ms: {} tile(s), {} light(s), {} caster(s)",
                    scene_name, elapsed.count(), result.status.tile_count,
                    result.status.baked_light_count, result.status.baked_caster_count);
            } else {
                ENGINE_LOG_ERROR("[LIGHTING BAKE] Failed '{}' after {} ms: {}",
                                 scene_name, elapsed.count(), result.error);
            }
        }
        if (job.queued && !job.running && now >= job.deadline) {
            const std::filesystem::path project_root = m_project_root;
            const std::filesystem::path scene_path = job_it->first;
            job.queued = false;
            job.running = true;
            job.started_at = std::chrono::steady_clock::now();
            job.future = std::async(std::launch::async, [project_root, scene_path]() {
                LightingBakeJobResult result;
                result.success = LightingBakeService::bake(
                    project_root, scene_path, &result.status, &result.error);
                return result;
            });
            ENGINE_LOG_INFO("[LIGHTING BAKE] Background bake started for '{}'", scene_path.filename().string());
        }
        if (!job.queued && !job.running) job_it = m_lighting_bake_jobs.erase(job_it);
        else ++job_it;
    }
}

void EditorLayer::record_recent_scene(const std::string& path) {
    m_recent_scenes.record(path);
    static_cast<void>(m_recent_scenes.save(m_recent_scenes_path.string()));
}

bool EditorLayer::save_active_scene(const std::string& custom_path) {
    // If a prefab stage is active, save the prefab asset instead
    if (m_prefab_stage && m_prefab_stage->is_active() && custom_path.empty()) {
        bool ok = m_prefab_stage->save();
        update_window_title();
        return ok;
    }

    if (!m_scene_manager) return false;
    // When in prefab stage, get the underlying authoring scene (not the transient one)
    Scene* active_scene = m_scene_manager->is_transient_scene_active()
        ? (m_scene_manager->get_scene_count() > 0
            ? m_scene_manager->get_scenes()[m_scene_manager->get_active_index()].get()
            : nullptr)
        : m_scene_manager->get_active_scene();
    if (!active_scene) {
        ENGINE_LOG_WARN("[EDITOR] No active scene to save.");
        return false;
    }

    auto& session = get_or_create_session(active_scene);
    const bool saved = save_scene(active_scene, session, custom_path);
    update_window_title();
    return saved;
}

bool EditorLayer::save_all_scenes() {
    bool succeeded = true;
    std::size_t saved_count = 0;

    if (m_prefab_stage && m_prefab_stage->is_active() && m_prefab_stage->is_dirty()) {
        if (m_prefab_stage->save()) {
            ++saved_count;
        } else {
            succeeded = false;
        }
    }

    if (m_scene_manager) {
        for (const auto& scene : m_scene_manager->get_scenes()) {
            if (!scene) continue;
            auto& session = get_or_create_session(scene.get());
            if (!session.command_history || !session.command_history->is_dirty()) continue;
            if (save_scene(scene.get(), session)) {
                ++saved_count;
            } else {
                succeeded = false;
            }
        }
    }

    update_window_title();
    if (succeeded) {
        ENGINE_LOG_INFO("[EDITOR] Save All completed ({} item(s) saved)", saved_count);
    } else {
        ENGINE_LOG_ERROR("[EDITOR] Save All completed with failures ({} item(s) saved)", saved_count);
    }
    return succeeded;
}

bool EditorLayer::is_scene_dirty(const Scene* scene) const {
    const EditorSceneSession* session = m_sessions.find(scene);
    return session && session->command_history && session->command_history->is_dirty();
}

bool EditorLayer::is_scene_tab_open(const Scene* scene) const {
    return m_sessions.is_tab_open(scene);
}

void EditorLayer::open_scene_tab(const Scene* scene) {
    m_sessions.open_tab(scene);
}

void EditorLayer::activate_scene(std::size_t index) {
    if (!m_scene_manager || !m_scene_manager->is_editing() || index >= m_scene_manager->get_scene_count()) return;
    const auto& scenes = m_scene_manager->get_scenes();
    if (!scenes[index]) return;
    open_scene_tab(scenes[index].get());
    m_scene_manager->set_active_scene(index);
    sync_active_scene_session();
}

void EditorLayer::close_scene_tab(std::size_t index) {
    if (!m_scene_manager || !m_scene_manager->is_editing() ||
        index >= m_scene_manager->get_scene_count() || m_sessions.tab_count() <= 1) return;
    const auto& scenes = m_scene_manager->get_scenes();
    const Scene* closing = scenes[index].get();
    if (!closing || !is_scene_tab_open(closing)) return;

    m_sessions.close_tab(closing);
    if (m_scene_manager->get_active_index() == static_cast<int>(index)) {
        const auto replacement = std::find_if(
            scenes.begin(), scenes.end(), [this](const std::shared_ptr<Scene>& scene) {
                return scene && is_scene_tab_open(scene.get());
            }
        );
        if (replacement != scenes.end()) {
            activate_scene(static_cast<std::size_t>(std::distance(scenes.begin(), replacement)));
        }
    }
    ENGINE_LOG_INFO("[EDITOR] Closed scene tab '{}'", closing->get_name());
}

bool EditorLayer::has_loaded_scene_path(const std::string& path, std::size_t& index) {
    if (!m_scene_manager) return false;
    const std::string normalized = std::filesystem::path(path).lexically_normal().generic_string();
    const auto& scenes = m_scene_manager->get_scenes();
    for (std::size_t scene_index = 0; scene_index < scenes.size(); ++scene_index) {
        if (!scenes[scene_index]) continue;
        const auto session = m_sessions.sessions().find(scenes[scene_index].get());
        if (session == m_sessions.sessions().end() || session->second.file_path.empty()) continue;
        const std::string loaded_path = std::filesystem::path(session->second.file_path).lexically_normal().generic_string();
        if (loaded_path == normalized) {
            index = scene_index;
            return true;
        }
    }
    return false;
}

bool EditorLayer::discard_active_scene_changes() {
    if (!m_scene_manager) return false;
    Scene* active_scene = m_scene_manager->get_active_scene();
    if (!active_scene) return false;

    auto& session = get_or_create_session(active_scene);
    if (!session.file_path.empty() && std::filesystem::exists(session.file_path)) {
        SceneEditorMetadata editor_metadata;
        SceneSerializer serializer(active_scene, m_renderer, nullptr, &editor_metadata);
        if (!serializer.deserialize(session.file_path)) {
            ENGINE_LOG_ERROR("[EDITOR] Failed to reload scene '{}' from '{}' during discard",
                             active_scene->get_name(), session.file_path);
            return false;
        }
        apply_scene_editor_metadata(editor_metadata, session.entity_state_store.get());
        session.command_history->reset_clean_baseline(0);
        ENGINE_LOG_INFO("[EDITOR] Discarded changes, reloaded scene '{}' from '{}'", active_scene->get_name(), session.file_path);
    } else if (!session.initial_snapshot_json.empty()) {
        SceneEditorMetadata editor_metadata;
        SceneSerializer serializer(active_scene, m_renderer, nullptr, &editor_metadata);
        if (!serializer.deserialize_from_string(session.initial_snapshot_json)) {
            ENGINE_LOG_ERROR("[EDITOR] Failed to restore scene '{}' from initial snapshot during discard",
                             active_scene->get_name());
            return false;
        }
        apply_scene_editor_metadata(editor_metadata, session.entity_state_store.get());
        session.command_history->reset_clean_baseline(0);
        ENGINE_LOG_INFO("[EDITOR] Discarded changes, restored scene '{}' to initial snapshot", active_scene->get_name());
    } else {
        session.command_history->reset_clean_baseline(0);
    }
    if (session.entity_state_store) {
        session.entity_state_store->prune(active_scene->get_entity_manager());
    }
    static_cast<void>(m_autosave_recovery.remove(session.autosave_identity));
    update_window_title();
    return true;
}

void EditorLayer::load_scene(const std::string& path) {
    if (!m_scene_manager) return;
    std::size_t loaded_index = 0;
    if (has_loaded_scene_path(path, loaded_index)) {
        activate_scene(loaded_index);
        return;
    }
    std::filesystem::path p(path);
    std::string scene_name = p.stem().string();

    auto new_scene = std::make_shared<Scene>(scene_name);
    auto loaded_editor_state = std::make_shared<EditorEntityStateStore>();
    SceneEditorMetadata editor_metadata;
    SceneSerializer serializer(new_scene.get(), m_renderer, nullptr, &editor_metadata);
    if (serializer.deserialize(path)) {
        apply_scene_editor_metadata(editor_metadata, loaded_editor_state.get());
        m_scene_manager->add_scene(new_scene);

        auto& session = get_or_create_session(new_scene.get(), scene_name);
        session.entity_state_store = std::move(loaded_editor_state);
        session.command_history->reset_clean_baseline(0);
        session.file_path = path;
        session.initial_snapshot_json.clear();
        session.autosave_identity = normalized_scene_identity(path);
        refresh_scene_source_stamp(session);
        load_entity_state_preferences(new_scene.get(), session);
        session.persisted_entity_state_revision =
            session.entity_state_store->persistence_revision();
        record_recent_scene(path);

        activate_scene(static_cast<std::size_t>(m_scene_manager->get_active_index()));
        ENGINE_LOG_INFO("[EDITOR] Loaded scene '{}' from '{}'", scene_name, path);
    }
}

void EditorLayer::render_dockspace() {
    if (!m_initialized || !m_visible) return;

    // 1. Render Top Main Menu Bar
    render_main_menu_bar();

    // Reserve the remaining top and bottom chrome before positioning the dock
    // host so panels never render underneath workspace or status controls.
    Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    const EditorShellState shell_state{
        .scene_name = active_scene ? active_scene->get_name() : std::string{},
        .scene_dirty = active_scene && is_scene_dirty(active_scene),
        .playing = m_scene_manager && !m_scene_manager->is_editing(),
        .selection_count = m_selection ? m_selection->get_selected().size() : 0
    };
    m_shell.render_workspace_bar(
        shell_state,
        [this](EditorWorkspaceId id) { activate_workspace(id); }
    );
    m_shell.render_status_bar(shell_state);

    // 2. Setup Fullscreen Central DockSpace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_window_flags = ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                                         ImGuiWindowFlags_NoNavFocus |
                                         ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("Engine2DDockSpaceHost", nullptr, host_window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("Engine2DDockSpace");
    // Workspace changes are queued by the top bar and applied only after the
    // dock host is active, but before DockSpace() submits its node. This keeps
    // the ID stack valid and rebuilds docking before any panel is rendered.
    const WorkspaceLayoutApplyResult pending_layout =
        m_workspace_layouts.apply_pending(dockspace_id, m_shell.workspaces());
    const WorkspaceLayoutApplyResult initial_layout =
        m_workspace_layouts.ensure_initialized(dockspace_id, m_shell.workspaces());
    if (pending_layout == WorkspaceLayoutApplyResult::Defaulted ||
        initial_layout == WorkspaceLayoutApplyResult::Defaulted) {
        apply_workspace_panel_visibility(false);
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    ImGui::End();

    m_shell.render_command_palette();

    if (m_show_reset_all_layouts_modal) {
        m_show_reset_all_layouts_modal = false;
        ImGui::OpenPopup("Reset All Workspace Layouts?");
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
            "Reset All Workspace Layouts?", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped(
            "This restores the default docking and visible panels for 2D, "
            "Animation, Tilemap, VFX, and Debug. Scenes, assets, editor "
            "preferences, and shortcuts are not changed."
        );
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        const auto& palette = EditorTheme::palette();
        ImGui::PushStyleColor(ImGuiCol_Button, palette.error);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(palette.error.x, palette.error.y, palette.error.z, 0.85f)
        );
        if (ImGui::Button("Reset All", ImVec2(120.0f, 0.0f))) {
            m_workspace_layouts.reset_all(m_shell.workspaces());
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndPopup();
    }

    // Modal popup: Save Scene As
    if (m_show_save_as_popup) {
        ImGui::OpenPopup("Save Scene As");
        m_show_save_as_popup = false;
    }

    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter Scene Filename (saved in Assets/Scenes/):");
        ImGui::InputText("##saveasname", m_save_as_buf, sizeof(m_save_as_buf));
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            std::string fname = m_save_as_buf;
            if (!fname.empty()) {
                if (fname.find(".scene") == std::string::npos) fname += ".scene";
                std::filesystem::create_directories(scenes_directory());
                save_active_scene((scenes_directory() / fname).string());
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_show_autosave_preferences) {
        ImGui::OpenPopup("Autosave Preferences");
        m_show_autosave_preferences = false;
    }
    if (ImGui::BeginPopupModal("Autosave Preferences", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Editor-only recovery settings");
        ImGui::TextDisabled("They are not stored in scenes or runtime player preferences.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputInt("Interval (seconds)", &m_autosave_interval_seconds);
        m_autosave_interval_seconds = std::clamp(
            m_autosave_interval_seconds, MIN_AUTOSAVE_INTERVAL_SECONDS, MAX_AUTOSAVE_INTERVAL_SECONDS
        );
        ImGui::InputText("Recovery folder", m_autosave_directory, sizeof(m_autosave_directory));
        ImGui::Spacing();
        if (ImGui::Button("Apply", ImVec2(110.0f, 0.0f))) {
            if (m_autosave_directory[0] != '\0') {
                m_autosave_recovery.set_directory(m_autosave_directory);
                m_autosave_elapsed = 0.0f;
                save_autosave_preferences();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(110.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Modal popup: Unsaved Changes Confirmation
    if (m_show_unsaved_changes_modal) {
        ImGui::OpenPopup("Unsaved Changes");
        m_show_unsaved_changes_modal = false;
    }

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string scene_name = (m_scene_manager && m_scene_manager->get_active_scene())
            ? m_scene_manager->get_active_scene()->get_name() : "Current Scene";

        ImGui::Text("You have unsaved changes in '%s'.", scene_name.c_str());
        ImGui::Text("Do you want to save before proceeding?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save & Continue", ImVec2(130, 0))) {
            if (save_active_scene()) {
                ImGui::CloseCurrentPopup();
                if (m_pending_action.action) {
                    auto act = std::move(m_pending_action.action);
                    m_pending_action = {};
                    act();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard Changes", ImVec2(130, 0))) {
            if (discard_active_scene_changes()) {
                ImGui::CloseCurrentPopup();
                if (m_pending_action.action) {
                    auto act = std::move(m_pending_action.action);
                    m_pending_action = {};
                    act();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_pending_action = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    render_prefab_stage_modals();
}

void EditorLayer::render_main_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Project...")) {
                request_project_hub();
            }
            ImGui::Separator();
            std::string new_lbl = ShortcutManager::get().get_label("editor.new_scene");
            if (ImGui::MenuItem("New Scene", new_lbl.empty() ? nullptr : new_lbl.c_str())) {
                static_cast<void>(m_shell.actions().execute("editor.new_scene"));
            }

            std::string save_lbl = ShortcutManager::get().get_label("editor.save_scene");
            const bool can_save_scene = m_shell.actions().is_enabled("editor.save_scene");
            if (ImGui::MenuItem(
                    "Save Scene", save_lbl.empty() ? nullptr : save_lbl.c_str(), false, can_save_scene
                )) {
                static_cast<void>(m_shell.actions().execute("editor.save_scene"));
            }
            std::string save_all_lbl = ShortcutManager::get().get_label("editor.save_all_scenes");
            const bool can_save_all = m_shell.actions().is_enabled("editor.save_all");
            if (ImGui::MenuItem(
                    "Save All", save_all_lbl.empty() ? nullptr : save_all_lbl.c_str(), false, can_save_all
                )) {
                static_cast<void>(m_shell.actions().execute("editor.save_all"));
            }
            if (ImGui::MenuItem("Save Scene As...", nullptr, false, can_persist_authoring_scenes())) {
                m_show_save_as_popup = true;
            }
            if (m_recent_scenes.remove_missing_files()) {
                static_cast<void>(m_recent_scenes.save(m_recent_scenes_path.string()));
            }
            if (!m_recent_scenes.empty()) {
                ImGui::Separator();
                if (ImGui::BeginMenu("Open Recent")) {
                    for (const std::string& path : m_recent_scenes.paths()) {
                        const std::filesystem::path scene_path(path);
                        const std::string label = scene_path.filename().string() + "##" + path;
                        if (ImGui::MenuItem(label.c_str())) {
                            load_scene(path);
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent Scenes")) {
                        m_recent_scenes.clear();
                        static_cast<void>(m_recent_scenes.save(m_recent_scenes_path.string()));
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                request_exit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            bool can_u = m_command_history && m_command_history->can_undo();
            std::string undo_str = "Undo";
            if (can_u) {
                undo_str += " " + m_command_history->get_undo_description();
            }
            std::string undo_lbl = ShortcutManager::get().get_label("editor.undo");
            if (ImGui::MenuItem(undo_str.c_str(), undo_lbl.c_str(), false, can_u)) {
                static_cast<void>(m_shell.actions().execute("editor.undo"));
            }

            bool can_r = m_command_history && m_command_history->can_redo();
            std::string redo_str = "Redo";
            if (can_r) {
                redo_str += " " + m_command_history->get_redo_description();
            }
            std::string redo_lbl = ShortcutManager::get().get_label("editor.redo");
            if (ImGui::MenuItem(redo_str.c_str(), redo_lbl.c_str(), false, can_r)) {
                static_cast<void>(m_shell.actions().execute("editor.redo"));
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Clear History", nullptr, false, m_command_history && (m_command_history->can_undo() || m_command_history->can_redo()))) {
                if (m_command_history) {
                    m_command_history->clear_history_preserving_dirty_state();
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Autosave Preferences...")) {
                m_show_autosave_preferences = true;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Project Settings...")) {
                static_cast<void>(m_shell.actions().execute("editor.open_project_settings"));
            }
            if (ImGui::MenuItem("Shortcuts & Keybindings...")) {
                static_cast<void>(m_shell.actions().execute("editor.open_shortcuts"));
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Current Workspace Layout")) {
                static_cast<void>(m_shell.actions().execute("editor.reset_layout"));
            }
            if (ImGui::MenuItem("Reset All Workspace Layouts...")) {
                static_cast<void>(m_shell.actions().execute("editor.reset_all_layouts"));
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject")) {
            Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
            bool has_scene = (active_scene != nullptr);

            if (ImGui::MenuItem("2D Tilemap", nullptr, false, has_scene)) {
                if (active_scene) {
                    EntityManager& em = active_scene->get_entity_manager();
                    EntitySnapshot snapshot;
                    snapshot.registered_components.set(TagComponent{"Tilemap"});
                    snapshot.registered_components.set(TransformComponent{glm::vec3(0.0f)});
                    snapshot.registered_components.set(TilemapComponent{});
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, nullptr, std::move(snapshot)));
                        update_window_title();
                    }
                }
            }

            if (ImGui::MenuItem("2D Sprite Entity", nullptr, false, has_scene)) {
                if (active_scene) {
                    EntityManager& em = active_scene->get_entity_manager();
                    EntitySnapshot snapshot;
                    snapshot.registered_components.set(TagComponent{"Sprite_Entity"});
                    snapshot.registered_components.set(TransformComponent{glm::vec3(0.0f)});
                    snapshot.registered_components.set(SpriteComponent{nullptr, glm::vec2(64.0f, 64.0f)});
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, nullptr, std::move(snapshot)));
                        update_window_title();
                    }
                }
            }

            if (ImGui::MenuItem("Camera 2D", nullptr, false, has_scene)) {
                if (active_scene) {
                    EntityManager& em = active_scene->get_entity_manager();
                    EntitySnapshot snapshot;
                    snapshot.registered_components.set(TagComponent{"Main_Camera"});
                    snapshot.registered_components.set(TransformComponent{glm::vec3(0.0f, 60.0f, 0.0f)});
                    CameraComponent camera;
                    camera.is_primary = !CameraSystem::has_camera(em);
                    snapshot.registered_components.set(camera);
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, nullptr, std::move(snapshot)));
                        update_window_title();
                    }
                }
            }

            if (ImGui::MenuItem("Empty Entity", nullptr, false, has_scene)) {
                if (active_scene) {
                    EntityManager& em = active_scene->get_entity_manager();
                    EntitySnapshot snapshot;
                    snapshot.registered_components.set(TagComponent{"Empty_Entity"});
                    snapshot.registered_components.set(TransformComponent{glm::vec3(0.0f)});
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, nullptr, std::move(snapshot)));
                        update_window_title();
                    }
                }
            }

            ImGui::Separator();

            bool has_selection = has_scene && (m_selection && !m_selection->empty());
            if (ImGui::MenuItem("Group Selection", "Cmd+G", false, has_selection)) {
                if (active_scene && m_selection && !m_selection->empty()) {
                    EntityManager& em = active_scene->get_entity_manager();
                    std::vector<EntityID> selected_vec(m_selection->get_selected().begin(), m_selection->get_selected().end());
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<GroupSelectedEntitiesCommand>(&em, m_selection, selected_vec));
                        update_window_title();
                    }
                }
            }

            bool has_group_selected = false;
            if (has_selection && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                for (EntityID sel_id : m_selection->get_selected()) {
                    if (em.is_alive(sel_id) && em.has_component<GroupComponent>(sel_id)) {
                        has_group_selected = true;
                        break;
                    }
                }
            }

            if (ImGui::MenuItem("Ungroup", "Cmd+Shift+G", false, has_group_selected)) {
                if (active_scene && m_selection && !m_selection->empty()) {
                    EntityManager& em = active_scene->get_entity_manager();
                    std::vector<EntityID> selected_vec(m_selection->get_selected().begin(), m_selection->get_selected().end());
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<UngroupEntitiesCommand>(&em, m_selection, selected_vec));
                        update_window_title();
                    }
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scenes")) {
            if (m_scene_manager) {
                const auto& scenes = m_scene_manager->get_scenes();
                int active_idx = m_scene_manager->get_active_index();

                if (ImGui::MenuItem("Next Scene", "TAB")) {
                    const int active_index = m_scene_manager->get_active_index();
                    const std::size_t next_index = static_cast<std::size_t>(
                        (active_index + 1 + static_cast<int>(scenes.size())) % static_cast<int>(scenes.size())
                    );
                    activate_scene(next_index);
                }
                if (ImGui::MenuItem("Previous Scene", "Shift+TAB")) {
                    const int active_index = m_scene_manager->get_active_index();
                    const std::size_t previous_index = static_cast<std::size_t>(
                        (active_index - 1 + static_cast<int>(scenes.size())) % static_cast<int>(scenes.size())
                    );
                    activate_scene(previous_index);
                }
                ImGui::Separator();

                for (size_t i = 0; i < scenes.size(); ++i) {
                    if (!scenes[i]) continue;
                    bool is_selected = (active_idx == static_cast<int>(i));
                    std::string label;
                    if (scenes[i]->get_name() == "PlatformerDemo") {
                        label = "1. Platformer Level (Authoring Scene)";
                    } else if (scenes[i]->get_name() == "2D Platformer & Collision Layer Matrix") {
                        label = "2. 2D Platformer & Collision Layers (C++ Sandbox)";
                    } else if (scenes[i]->get_name() == "2D Animated Sprites & Asset Loader") {
                        label = "3. Top-Down Animated Sprites";
                    } else if (scenes[i]->get_name() == "2.5D Depth & Parallax Test") {
                        label = "4. 2.5D Depth & Parallax Test";
                    } else if (scenes[i]->get_name() == "GPU/CPU Stress Test") {
                        label = "5. GPU/CPU Stress Test (3,000 Sprites)";
                    } else {
                        label = std::to_string(i + 1) + ". " + scenes[i]->get_name();
                    }

                    if (ImGui::MenuItem(label.c_str(), nullptr, is_selected)) {
                        activate_scene(i);
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Panels")) {
            for (auto& panel : m_panel_registry.panels()) {
                if (panel && panel->get_id() != EditorPanelId::ProjectSettings &&
                    panel->get_id() != EditorPanelId::Shortcuts) {
                    ImGui::MenuItem(panel->get_name().c_str(), nullptr, &panel->is_open);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Build & Export Game...", "Ctrl+B")) {
                bool stale_bake = false;
                for (const auto& scene : m_scene_manager ? m_scene_manager->get_scenes() : std::vector<std::shared_ptr<Scene>>{}) {
                    if (!scene) continue;
                    const auto session = m_sessions.sessions().find(scene.get());
                    if (session == m_sessions.sessions().end() || session->second.file_path.empty()) continue;
                    const auto bake = LightingBakeService::status(m_project_root, session->second.file_path);
                    if (bake.has_bake_volume && !bake.current) {
                        stale_bake = true;
                        ENGINE_LOG_ERROR("[EXPORT] Blocked: lighting bake for '{}' is {}", scene->get_name(), bake.reason);
                    }
                }
                if (!stale_bake) ENGINE_LOG_INFO("[EDITOR] Export validation passed; export pipeline triggered.");
            }
            if (ImGui::MenuItem("Bake Lighting for Active Scene")) {
                Scene* const scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
                const auto session = scene ? m_sessions.sessions().find(scene) : m_sessions.sessions().end();
                if (!scene || session == m_sessions.sessions().end() || session->second.file_path.empty()) {
                    ENGINE_LOG_WARN("[LIGHTING BAKE] Save the active scene before baking.");
                } else {
                    schedule_lighting_bake(session->second.file_path, std::chrono::milliseconds(0));
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Toggle Editor (F1)", nullptr, m_visible)) {
                toggle_visibility();
            }
            if (m_viewport_panel) {
                ImGui::Separator();
                if (ImGui::BeginMenu("Viewport Grid")) {
                    m_viewport_panel->render_grid_settings_menu();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Arrange Selection")) {
                    m_viewport_panel->render_arrange_menu();
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About Engine2D")) {
                // Future about modal
            }
            ImGui::EndMenu();
        }

        // Right-aligned global tools and status indicators.
        const auto ai_assistant = std::dynamic_pointer_cast<AiAssistantPanel>(
            get_panel("AI Assistant")
        );
        constexpr float AI_BUTTON_WIDTH = 24.0f;
        float right_pos = ImGui::GetWindowWidth() - AI_BUTTON_WIDTH - 8.0f;
        if (right_pos > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(right_pos);
            ImGui::BeginDisabled(!ai_assistant);
            if (editor_icon_button(
                "open_ai_assistant",
                EditorIcon::Assistant,
                ai_assistant && ai_assistant->is_open
                    ? "Focus AI Assistant" : "Open AI Assistant",
                ai_assistant && ai_assistant->is_open,
                ImVec2(AI_BUTTON_WIDTH, 20.0f)
            ) && ai_assistant) {
                ai_assistant->request_focus();
            }
            ImGui::EndDisabled();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorLayer::activate_workspace(EditorWorkspaceId id) {
    m_workspace_layouts.persist();
    persist_panel_states();
    if (!m_shell.activate_workspace(id)) return;
    apply_workspace_panel_visibility();
    m_workspace_layouts.request(id);
}

void EditorLayer::render_panels(float delta_time) {
    if (!m_initialized || !m_visible) return;

    if (!m_recovery_scan_complete) {
        m_pending_recoveries = m_autosave_recovery.discover();
        m_recovery_scan_complete = true;
    }
    update_autosaves(delta_time);
    update_external_scene_changes(delta_time);
    render_recovery_modal();
    render_external_scene_change_modal();

    for (auto& panel : m_panel_registry.panels()) {
        if (panel && panel->is_open) {
            panel->on_update(delta_time);
            panel->on_gui();
        }
    }

    if (!m_panels_to_reselect.empty()) {
        for (const std::string& panel_name : m_panels_to_reselect) {
            const auto panel = get_panel(panel_name);
            if (panel && panel->is_open) ImGui::SetWindowFocus(panel_name.c_str());
        }
        m_panels_to_reselect.clear();
    }

    const bool entity_state_changed = std::any_of(
        m_sessions.sessions().begin(), m_sessions.sessions().end(), [](const auto& entry) {
            const EditorSceneSession& session = entry.second;
            return session.entity_state_store &&
                session.entity_state_store->persistence_revision() !=
                    session.persisted_entity_state_revision;
        }
    );
    if (entity_state_changed) save_entity_state_preferences();
    persist_panel_states();

    m_hot_exit_checkpoint_elapsed += std::max(delta_time, 0.0f);
    if (m_hot_exit_checkpoint_elapsed >= 2.0f) {
        m_hot_exit_checkpoint_elapsed = 0.0f;
        save_hot_exit_session();
    }
}

void EditorLayer::refresh_scene_source_stamp(EditorSceneSession& session) {
    session.source_write_time_initialized = false;
    if (session.file_path.empty()) return;
    std::error_code error;
    const auto write_time = std::filesystem::last_write_time(session.file_path, error);
    if (error) return;
    session.observed_source_write_time = write_time;
    session.source_write_time_initialized = true;
}

bool EditorLayer::reload_scene_from_disk(
    Scene* scene,
    EditorSceneSession& session,
    const char* reason
) {
    if (!scene || session.file_path.empty()) return false;

    SceneEditorMetadata editor_metadata;
    SceneSerializer serializer(scene, m_renderer, nullptr, &editor_metadata);
    if (!serializer.deserialize(session.file_path)) {
        session.external_source_conflicted = true;
        m_external_conflict_scene = scene;
        ENGINE_LOG_ERROR("[EDITOR] Failed to reload externally modified scene '{}' from '{}'",
                         scene->get_name(), session.file_path);
        return false;
    }

    apply_scene_editor_metadata(editor_metadata, session.entity_state_store.get());
    if (session.entity_state_store) {
        session.entity_state_store->prune(scene->get_entity_manager());
    }
    if (session.selection) {
        std::unordered_set<EntityID> surviving_selection;
        const EntityManager& entities = scene->get_entity_manager();
        for (EntityID selected : session.selection->get_selected()) {
            if (entities.is_alive(selected)) surviving_selection.insert(selected);
        }
        session.selection->set_selection(surviving_selection);
    }
    if (session.command_history) session.command_history->reset_clean_baseline(0);
    session.initial_snapshot_json.clear();
    session.external_source_conflicted = false;
    refresh_scene_source_stamp(session);
    schedule_lighting_bake(session.file_path, std::chrono::milliseconds(500));
    update_window_title();
    ENGINE_LOG_INFO("[EDITOR] Reloaded scene '{}' from disk ({})", scene->get_name(), reason);
    return true;
}

void EditorLayer::update_external_scene_changes(float delta_time) {
    if (!m_scene_manager || !m_scene_manager->is_editing()) return;
    m_external_scene_poll_elapsed += delta_time;
    if (m_external_scene_poll_elapsed < 0.25f) return;
    m_external_scene_poll_elapsed = 0.0f;

    for (const auto& scene : m_scene_manager->get_scenes()) {
        if (!scene) continue;
        auto session_it = m_sessions.sessions().find(scene.get());
        if (session_it == m_sessions.sessions().end()) continue;
        EditorSceneSession& session = session_it->second;
        if (session.file_path.empty()) continue;

        std::error_code error;
        const auto write_time = std::filesystem::last_write_time(session.file_path, error);
        if (error) continue;
        if (!session.source_write_time_initialized) {
            session.observed_source_write_time = write_time;
            session.source_write_time_initialized = true;
            continue;
        }
        if (write_time == session.observed_source_write_time) continue;
        session.observed_source_write_time = write_time;

        if (session.command_history && session.command_history->is_dirty()) {
            session.external_source_conflicted = true;
            m_external_conflict_scene = scene.get();
            ENGINE_LOG_WARN(
                "[EDITOR] Scene '{}' changed on disk while local edits are unsaved; save is blocked",
                scene->get_name());
            continue;
        }
        static_cast<void>(reload_scene_from_disk(scene.get(), session, "external change"));
    }
}

void EditorLayer::render_external_scene_change_modal() {
    if (m_external_conflict_scene) {
        ImGui::OpenPopup("Scene changed on disk");
    }
    if (!ImGui::BeginPopupModal(
            "Scene changed on disk", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const Scene* conflicted_scene = m_external_conflict_scene;
    ImGui::TextWrapped(
        "This scene changed on disk while it has unsaved editor changes. "
        "Saving over the external version is blocked.");
    if (conflicted_scene) ImGui::Text("Scene: %s", conflicted_scene->get_name().c_str());
    ImGui::Spacing();

    if (ImGui::Button("Reload from disk", ImVec2(150.0f, 0.0f))) {
        auto session_it = m_sessions.sessions().find(conflicted_scene);
        bool reloaded = false;
        if (session_it != m_sessions.sessions().end()) {
            reloaded = reload_scene_from_disk(
                const_cast<Scene*>(conflicted_scene), session_it->second, "conflict resolved");
        }
        if (reloaded) {
            m_external_conflict_scene = nullptr;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep editing", ImVec2(120.0f, 0.0f))) {
        m_external_conflict_scene = nullptr;
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Your local edits stay in memory; use Save As to preserve them separately.");
    }
    ImGui::EndPopup();
}

void EditorLayer::update_autosaves(float delta_time) {
    if (!m_scene_manager || delta_time <= 0.0f) return;
    if (!can_persist_authoring_scenes()) return;
    m_autosave_elapsed += delta_time;
    if (m_autosave_elapsed < static_cast<float>(m_autosave_interval_seconds)) return;
    m_autosave_elapsed = 0.0f;

    for (const auto& scene : m_scene_manager->get_scenes()) {
        if (!scene) continue;
        auto& session = get_or_create_session(scene.get());
        if (!session.command_history || !session.command_history->is_dirty()) continue;
        SceneEditorMetadata editor_metadata = capture_scene_editor_metadata(session.entity_state_store.get());
        const std::string snapshot = SceneSerializer(
            scene.get(), m_renderer, nullptr, &editor_metadata
        ).serialize_to_string();
        if (m_autosave_recovery.write_snapshot(
                session.autosave_identity, session.file_path, scene->get_name(), snapshot
            )) {
            ENGINE_LOG_INFO("[AUTOSAVE] Snapshot written for scene '{}'", scene->get_name());
        }
    }
}

void EditorLayer::render_recovery_modal() {
    if (m_pending_recoveries.empty()) return;

    ImGui::OpenPopup("Recover Autosaved Scene");
    if (!ImGui::BeginPopupModal("Recover Autosaved Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const AutosaveRecord& record = m_pending_recoveries.front();
    ImGui::Text("An autosaved scene was found for '%s'.", record.scene_name.c_str());
    if (record.source_conflicted) {
        ImGui::TextWrapped("The source scene changed outside the editor. Recovery will require Save As, so it cannot overwrite that file.");
    } else if (!record.source_path.empty()) {
        ImGui::TextWrapped("Recover the unsaved changes over: %s", record.source_path.c_str());
    } else {
        ImGui::TextWrapped("Recover the unsaved changes as a new scene.");
    }
    ImGui::Spacing();
    if (ImGui::Button("Recover", ImVec2(120, 0))) {
        recover_autosave(record);
        m_pending_recoveries.erase(m_pending_recoveries.begin());
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard Autosave", ImVec2(140, 0))) {
        static_cast<void>(m_autosave_recovery.remove(record.identity));
        m_pending_recoveries.erase(m_pending_recoveries.begin());
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorLayer::recover_autosave(const AutosaveRecord& record) {
    if (!m_scene_manager) return;

    auto recovered_scene = std::make_shared<Scene>(record.scene_name);
    auto recovered_editor_state = std::make_shared<EditorEntityStateStore>();
    SceneEditorMetadata editor_metadata;
    SceneSerializer serializer(recovered_scene.get(), m_renderer, nullptr, &editor_metadata);
    if (!serializer.deserialize_from_string(record.snapshot_json)) {
        ENGINE_LOG_ERROR("[AUTOSAVE] Failed recovering scene '{}'", record.scene_name);
        return;
    }
    apply_scene_editor_metadata(editor_metadata, recovered_editor_state.get());

    m_scene_manager->add_scene(recovered_scene);
    auto& session = get_or_create_session(recovered_scene.get(), record.scene_name);
    session.entity_state_store = std::move(recovered_editor_state);
    session.command_history->reset_clean_baseline(0);
    session.command_history->mark_recovered();
    session.file_path = record.source_conflicted ? "" : record.source_path;
    session.initial_snapshot_json.clear();
    session.autosave_identity = record.identity;
    session.recovery_source_conflicted = record.source_conflicted;
    load_entity_state_preferences(recovered_scene.get(), session);
    session.persisted_entity_state_revision =
        session.entity_state_store->persistence_revision();
    if (!record.source_conflicted && !record.source_path.empty()) record_recent_scene(record.source_path);

    const auto& scenes = m_scene_manager->get_scenes();
    const auto recovered = std::find_if(scenes.begin(), scenes.end(), [&recovered_scene](const std::shared_ptr<Scene>& scene) {
        return scene.get() == recovered_scene.get();
    });
    if (recovered != scenes.end()) activate_scene(static_cast<std::size_t>(std::distance(scenes.begin(), recovered)));
    ENGINE_LOG_INFO("[AUTOSAVE] Recovered scene '{}'", record.scene_name);
}

void EditorLayer::end_frame(SDL_Renderer* renderer) {
    if (!m_initialized || !m_visible) return;

    ImGui::Render();
    // Persist only when the serialized layout changed. This is independent of
    // Dear ImGui's delayed dirty timer and therefore survives dev.sh SIGTERM.
    m_workspace_layouts.persist();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

void EditorLayer::invalidate_render_resources() {
    if (!m_initialized) return;
    m_panel_registry.invalidate_render_resources();
    ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
}

void EditorLayer::trim_render_resources() {
    if (!m_initialized) return;
    m_panel_registry.invalidate_render_resources();
}

void EditorLayer::restore_render_resources() {
    if (!m_initialized) return;
    ImGui_ImplSDLRenderer3_CreateDeviceObjects();
}

void EditorLayer::register_panel(EditorPanelId id, std::shared_ptr<EditorPanel> panel) {
    if (!panel) return;
    if (id == EditorPanelId::ProjectSettings || id == EditorPanelId::Shortcuts) {
        panel->is_open = false;
    } else {
        const bool legacy_open = m_panel_states.is_open(
            editor_panel_id_key(id), panel->is_open
        );
        panel->is_open = m_panel_states.is_open(
            workspace_panel_state_key(m_shell.workspaces(), id), legacy_open
        );
    }
    if (!m_panel_registry.add(
            id, panel, {m_command_history, m_entity_state_store, m_selection})) {
        ENGINE_LOG_WARN("[EDITOR] Ignored duplicate or invalid panel: '{}'", panel->get_name());
        return;
    }
    ENGINE_LOG_INFO("[EDITOR] Registered panel: '{}'", panel->get_name());
}

void EditorLayer::unregister_panel(const std::string& name) {
    if (m_panel_registry.remove(name)) {
        ENGINE_LOG_INFO("[EDITOR] Unregistered panel: '{}'", name);
    }
}

std::shared_ptr<EditorPanel> EditorLayer::get_panel(const std::string& name) const {
    return m_panel_registry.find(name);
}

std::shared_ptr<EditorPanel> EditorLayer::get_panel(EditorPanelId id) const {
    return m_panel_registry.find(id);
}

PrefabStageController* EditorLayer::get_prefab_stage() const {
    return m_prefab_stage.get();
}

std::filesystem::path EditorLayer::scenes_directory() const {
    return m_project_root / "Assets/Scenes";
}

void EditorLayer::open_prefab_stage(const std::string& prefab_path) {
    if (!m_prefab_stage || !m_scene_manager) return;
    if (m_prefab_stage->is_active()) {
        ENGINE_LOG_WARN("[EDITOR] Cannot open prefab stage — one is already active ('{}'). Close it first.",
                        m_prefab_stage->get_prefab_name());
        return;
    }
    if (!m_prefab_stage->open(prefab_path, m_scene_manager, m_renderer)) {
        ENGINE_LOG_ERROR("[EDITOR] Failed to open prefab stage for '{}'", prefab_path);
        return;
    }
    // Force session resync so all panels switch to the stage's isolated session.
    m_last_active_scene = nullptr;
    sync_active_scene_session();
}

void EditorLayer::render_prefab_stage_modals() {
    if (!m_prefab_stage) return;

    // Prefab Stage Exit Modal — shown when a dirty stage blocks a destructive action.
    if (m_show_prefab_stage_exit_modal) {
        ImGui::OpenPopup("Unsaved Prefab Changes");
        m_show_prefab_stage_exit_modal = false;
    }

    if (ImGui::BeginPopupModal("Unsaved Prefab Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Prefab '%s' has unsaved changes.", m_prefab_stage->get_prefab_name().c_str());
        ImGui::Text("Do you want to save before leaving the Prefab Stage?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save & Continue", ImVec2(140, 0))) {
            if (m_prefab_stage->save()) {
                m_prefab_stage->close_discard();
                m_last_active_scene = nullptr;
                sync_active_scene_session();
                ImGui::CloseCurrentPopup();
                if (m_pending_action.action) {
                    auto act = std::move(m_pending_action.action);
                    m_pending_action = {};
                    act();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard Changes", ImVec2(140, 0))) {
            m_prefab_stage->close_discard();
            m_last_active_scene = nullptr;
            sync_active_scene_session();
            ImGui::CloseCurrentPopup();
            if (m_pending_action.action) {
                auto act = std::move(m_pending_action.action);
                m_pending_action = {};
                act();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_pending_action = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace Engine
