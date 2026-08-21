#include "EditorLayer.hpp"
#include "EditorTheme.hpp"
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
#include "../core/Window.hpp"
#include "../scene/SceneManager.hpp"
#include "../scene/Scene.hpp"
#include "../scene/GenericScene.hpp"
#include "../scene/SceneSerializer.hpp"
#include "panels/ViewportPanel.hpp"
#include "panels/SceneHierarchyPanel.hpp"
#include "panels/InspectorPanel.hpp"
#include "panels/SceneDiagnosticsPanel.hpp"
#include "panels/SpriteAnimationStudioPanel.hpp"
#include "panels/PostProcessTunerPanel.hpp"
#include "panels/ProjectSettingsPanel.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../ecs/CameraSystem.hpp"
#include "../tilemap/Tilemap.hpp"
#include <filesystem>
#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

namespace Engine {

namespace {

constexpr const char* RECENT_SCENES_PATH = "config/editor_recent_scenes.json";

} // namespace

EditorLayer::~EditorLayer() {
    // PrefabStageController must be torn down before EditorLayer members.
    if (m_prefab_stage && m_prefab_stage->is_active()) {
        m_prefab_stage->close_discard();
    }
    m_prefab_stage.reset();
    shutdown();
}

void EditorLayer::init(SDL_Window* window, SDL_Renderer* renderer) {
    if (m_initialized) return;

    m_window = window;
    m_renderer = renderer;
    static_cast<void>(m_recent_scenes.load(RECENT_SCENES_PATH));
    if (m_recent_scenes.remove_missing_files()) {
        static_cast<void>(m_recent_scenes.save(RECENT_SCENES_PATH));
    }

    // 1. Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    static_cast<void>(initialize_editor_icons());

    // 2. Setup Dear ImGui Modern Theme
    EditorTheme::apply_dark_theme();

    // 3. Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // 4. Register Default Actions in ShortcutManager
    auto& sm = ShortcutManager::get();
    sm.register_shortcut({
        .id = "editor.undo",
        .display_name = "Undo Action",
        .category = "Editing",
        .primary_key = ImGuiKey_Z,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = true,
        .callback = [this]() {
            if (m_command_history) {
                m_command_history->undo();
                update_window_title();
            }
        }
    });

    sm.register_shortcut({
        .id = "editor.redo",
        .display_name = "Redo Action",
        .category = "Editing",
        .primary_key = ImGuiKey_Z,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = true,
        .mod_alt = false,
        .enable_repeat = true,
        .callback = [this]() {
            if (m_command_history) {
                m_command_history->redo();
                update_window_title();
            }
        }
    });

    sm.register_shortcut({
        .id = "editor.save_scene",
        .display_name = "Save Scene",
        .category = "File",
        .primary_key = ImGuiKey_S,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() { save_active_scene(); }
    });

    sm.register_shortcut({
        .id = "editor.save_all_scenes",
        .display_name = "Save All Scenes",
        .category = "File",
        .primary_key = ImGuiKey_S,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = true,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() { save_all_scenes(); }
    });

    sm.register_shortcut({
        .id = "editor.new_scene",
        .display_name = "New Scene",
        .category = "File",
        .primary_key = ImGuiKey_N,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() {
            request_destructive_action({
                .type = PendingActionType::NewScene,
                .target = "New Scene",
                .action = [this]() { create_new_scene("New Scene"); }
            });
        }
    });

    sm.register_shortcut({
        .id = "editor.duplicate",
        .display_name = "Duplicate Entities",
        .category = "Editing",
        .primary_key = ImGuiKey_D,
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
        .primary_key = ImGuiKey_Backspace,
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
        .primary_key = ImGuiKey_G,
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
        .id = "editor.ungroup_selection",
        .display_name = "Ungroup Selection",
        .category = "Hierarchy",
        .primary_key = ImGuiKey_G,
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
        .primary_key = ImGuiKey_F,
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
        .primary_key = ImGuiKey_F,
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
        .primary_key = ImGuiKey_Home,
        .mod_cmd = false,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = nullptr
    });

    sm.load_from_file("config/shortcuts.json");

    m_prefab_stage = std::make_unique<PrefabStageController>();

    m_initialized = true;
    ENGINE_LOG_INFO("[EDITOR] Dear ImGui initialized successfully (Docking Enabled).");
}

void EditorLayer::shutdown() {
    if (!m_initialized) return;

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    m_initialized = false;
    ENGINE_LOG_INFO("[EDITOR] Dear ImGui shutdown cleanly.");
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

    auto it = m_scene_sessions.find(scene);
    if (it == m_scene_sessions.end()) {
        EditorSceneSession session;
        session.command_history = std::make_shared<EditorCommandHistory>();
        session.command_history->reset_clean_baseline(0);
        session.entity_state_store = std::make_shared<EditorEntityStateStore>();
        session.selection = std::make_shared<EditorSelection>();

        std::string scene_name = scene->get_name();
        if (scene_name.empty()) scene_name = default_name;
        if (std::filesystem::exists("assets/scenes/" + scene_name + ".scene")) {
            session.file_path = "assets/scenes/" + scene_name + ".scene";
        } else {
            // Save initial snapshot for rollback on discard
            SceneSerializer serializer(const_cast<Scene*>(scene), m_renderer);
            session.initial_snapshot_json = serializer.serialize_to_string();
        }

        it = m_scene_sessions.emplace(scene, std::move(session)).first;
    }
    return it->second;
}

void EditorLayer::update_panel_sessions(std::shared_ptr<EditorCommandHistory> ch, std::shared_ptr<EditorEntityStateStore> store, std::shared_ptr<EditorSelection> sel) {
    if (m_viewport_panel) {
        m_viewport_panel->set_command_history(ch);
        m_viewport_panel->set_entity_state_store(store);
        if (sel) m_viewport_panel->set_selection(sel);
    }
    for (auto& panel : m_panels) {
        if (!panel) continue;
        if (auto p = std::dynamic_pointer_cast<SceneHierarchyPanel>(panel)) {
            p->set_command_history(ch);
            p->set_entity_state_store(store);
            if (sel) p->set_selection(sel);
        } else if (auto p = std::dynamic_pointer_cast<InspectorPanel>(panel)) {
            p->set_command_history(ch);
            p->set_entity_state_store(store);
            if (sel) p->set_selection(sel);
        } else if (auto p = std::dynamic_pointer_cast<SceneDiagnosticsPanel>(panel)) {
            p->set_command_history(ch);
            if (sel) p->set_selection(sel);
        } else if (auto p = std::dynamic_pointer_cast<SpriteAnimationStudioPanel>(panel)) {
            p->set_command_history(ch);
        } else if (auto p = std::dynamic_pointer_cast<PostProcessTunerPanel>(panel)) {
            p->set_command_history(ch);
        }
    }
}

void EditorLayer::update_panel_command_histories(std::shared_ptr<EditorCommandHistory> ch) {
    update_panel_sessions(ch, m_entity_state_store);
}

void EditorLayer::cleanup_stale_sessions() {
    if (!m_scene_manager) return;
    const auto& scenes = m_scene_manager->get_scenes();
    for (auto it = m_scene_sessions.begin(); it != m_scene_sessions.end(); ) {
        bool found = false;
        for (const auto& scene : scenes) {
            if (scene.get() == it->first) {
                found = true;
                break;
            }
        }
        if (!found) {
            it = m_scene_sessions.erase(it);
        } else {
            ++it;
        }
    }
    std::erase_if(m_open_scene_tabs, [&scenes](const Scene* scene) {
        return std::none_of(scenes.begin(), scenes.end(), [scene](const std::shared_ptr<Scene>& candidate) {
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
    if (m_open_scene_tabs.empty()) open_scene_tab(active_scene);

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
    return request_destructive_action({
        .type = PendingActionType::ExitApp,
        .target = "",
        .action = [this]() {
            if (m_window_wrapper) {
                m_window_wrapper->force_close();
            }
        }
    });
}

void EditorLayer::begin_frame() {
    if (!m_initialized || !m_visible) return;

    sync_active_scene_session();
    update_window_title();

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Process all registered actions with smart acceleration curves
    float dt = ImGui::GetIO().DeltaTime;
    ShortcutManager::get().update(dt, want_text_input());
}

void EditorLayer::setup_default_dockspace_layout(uint32_t dockspace_id) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID dock_main_id = dockspace_id;
    ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.22f, nullptr, &dock_main_id);
    ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.28f, nullptr, &dock_main_id);
    ImGuiID dock_down_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.28f, nullptr, &dock_main_id);

    ImGui::DockBuilderDockWindow("Scene Viewport", dock_main_id);
    ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left_id);
    ImGui::DockBuilderDockWindow("Engine & Metrics", dock_right_id);
    ImGui::DockBuilderDockWindow("Entity Inspector", dock_right_id);
    ImGui::DockBuilderDockWindow("Scene Diagnostics", dock_right_id);
    ImGui::DockBuilderDockWindow("Asset Browser", dock_down_id);
    ImGui::DockBuilderDockWindow("Console & Logs", dock_down_id);

    ImGui::DockBuilderFinish(dockspace_id);

}

void EditorLayer::create_new_scene(const std::string& name) {
    if (!m_scene_manager) return;
    auto new_scene = std::make_shared<GenericScene>(name);
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
    session.initial_snapshot_json = SceneSerializer(new_scene.get(), m_renderer).serialize_to_string();

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

    std::string path = custom_path;
    if (path.empty()) {
        if (!session.file_path.empty()) {
            path = session.file_path;
        } else {
            std::filesystem::create_directories("assets/scenes");
            path = "assets/scenes/" + scene->get_name() + ".scene";
        }
    }

    SceneSerializer serializer(scene, m_renderer);
    if (!serializer.serialize(path)) {
        ENGINE_LOG_ERROR("[EDITOR] Failed to save scene '{}' to '{}'", scene->get_name(), path);
        return false;
    }

    session.file_path = path;
    session.initial_snapshot_json.clear();
    if (session.command_history) session.command_history->mark_saved();
    record_recent_scene(path);
    ENGINE_LOG_INFO("[EDITOR] Successfully saved scene '{}' to '{}'", scene->get_name(), path);
    return true;
}

void EditorLayer::record_recent_scene(const std::string& path) {
    m_recent_scenes.record(path);
    static_cast<void>(m_recent_scenes.save(RECENT_SCENES_PATH));
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
    const auto session = m_scene_sessions.find(scene);
    return session != m_scene_sessions.end() && session->second.command_history &&
        session->second.command_history->is_dirty();
}

bool EditorLayer::is_scene_tab_open(const Scene* scene) const {
    return std::find(m_open_scene_tabs.begin(), m_open_scene_tabs.end(), scene) != m_open_scene_tabs.end();
}

void EditorLayer::open_scene_tab(const Scene* scene) {
    if (!scene || is_scene_tab_open(scene)) return;
    m_open_scene_tabs.push_back(scene);
}

void EditorLayer::activate_scene(std::size_t index) {
    if (!m_scene_manager || !m_scene_manager->is_editing() || index >= m_scene_manager->get_scene_count()) return;
    const auto& scenes = m_scene_manager->get_scenes();
    if (!scenes[index]) return;
    open_scene_tab(scenes[index].get());
    m_scene_manager->set_active_scene(index);
    sync_active_scene_session();
}

bool EditorLayer::has_loaded_scene_path(const std::string& path, std::size_t& index) {
    if (!m_scene_manager) return false;
    const std::string normalized = std::filesystem::path(path).lexically_normal().generic_string();
    const auto& scenes = m_scene_manager->get_scenes();
    for (std::size_t scene_index = 0; scene_index < scenes.size(); ++scene_index) {
        if (!scenes[scene_index]) continue;
        const auto session = m_scene_sessions.find(scenes[scene_index].get());
        if (session == m_scene_sessions.end() || session->second.file_path.empty()) continue;
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
        SceneSerializer serializer(active_scene, m_renderer);
        if (!serializer.deserialize(session.file_path)) {
            ENGINE_LOG_ERROR("[EDITOR] Failed to reload scene '{}' from '{}' during discard",
                             active_scene->get_name(), session.file_path);
            return false;
        }
        session.command_history->reset_clean_baseline(0);
        ENGINE_LOG_INFO("[EDITOR] Discarded changes, reloaded scene '{}' from '{}'", active_scene->get_name(), session.file_path);
    } else if (!session.initial_snapshot_json.empty()) {
        SceneSerializer serializer(active_scene, m_renderer);
        if (!serializer.deserialize_from_string(session.initial_snapshot_json)) {
            ENGINE_LOG_ERROR("[EDITOR] Failed to restore scene '{}' from initial snapshot during discard",
                             active_scene->get_name());
            return false;
        }
        session.command_history->reset_clean_baseline(0);
        ENGINE_LOG_INFO("[EDITOR] Discarded changes, restored scene '{}' to initial snapshot", active_scene->get_name());
    } else {
        session.command_history->reset_clean_baseline(0);
    }
    if (session.entity_state_store) {
        session.entity_state_store->prune(active_scene->get_entity_manager());
    }
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

    auto new_scene = std::make_shared<GenericScene>(scene_name);
    SceneSerializer serializer(new_scene.get(), m_renderer);
    if (serializer.deserialize(path)) {
        m_scene_manager->add_scene(new_scene);

        auto& session = get_or_create_session(new_scene.get(), scene_name);
        session.command_history->reset_clean_baseline(0);
        session.file_path = path;
        session.initial_snapshot_json.clear();
        record_recent_scene(path);

        activate_scene(static_cast<std::size_t>(m_scene_manager->get_active_index()));
        ENGINE_LOG_INFO("[EDITOR] Loaded scene '{}' from '{}'", scene_name, path);
    }
}

void EditorLayer::render_dockspace() {
    if (!m_initialized || !m_visible) return;

    // 1. Render Top Main Menu Bar
    render_main_menu_bar();

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
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (!m_dockspace_initialized) {
        m_dockspace_initialized = true;
        setup_default_dockspace_layout(dockspace_id);
    }

    ImGui::End();

    // Modal popup: Save Scene As
    if (m_show_save_as_popup) {
        ImGui::OpenPopup("Save Scene As");
        m_show_save_as_popup = false;
    }

    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter Scene Filename (saved in assets/scenes/):");
        ImGui::InputText("##saveasname", m_save_as_buf, sizeof(m_save_as_buf));
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            std::string fname = m_save_as_buf;
            if (!fname.empty()) {
                if (fname.find(".scene") == std::string::npos) fname += ".scene";
                std::filesystem::create_directories("assets/scenes");
                save_active_scene("assets/scenes/" + fname);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
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
            std::string new_lbl = ShortcutManager::get().get_label("editor.new_scene");
            if (ImGui::MenuItem("New Scene", new_lbl.empty() ? nullptr : new_lbl.c_str())) {
                request_destructive_action({
                    .type = PendingActionType::NewScene,
                    .target = "New Scene",
                    .action = [this]() { create_new_scene("New Scene"); }
                });
            }

            std::string save_lbl = ShortcutManager::get().get_label("editor.save_scene");
            if (ImGui::MenuItem("Save Scene", save_lbl.empty() ? nullptr : save_lbl.c_str())) {
                save_active_scene();
            }
            std::string save_all_lbl = ShortcutManager::get().get_label("editor.save_all_scenes");
            if (ImGui::MenuItem("Save All", save_all_lbl.empty() ? nullptr : save_all_lbl.c_str())) {
                save_all_scenes();
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                m_show_save_as_popup = true;
            }
            if (m_recent_scenes.remove_missing_files()) {
                static_cast<void>(m_recent_scenes.save(RECENT_SCENES_PATH));
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
                        static_cast<void>(m_recent_scenes.save(RECENT_SCENES_PATH));
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
                if (m_command_history) {
                    m_command_history->undo();
                    update_window_title();
                }
            }

            bool can_r = m_command_history && m_command_history->can_redo();
            std::string redo_str = "Redo";
            if (can_r) {
                redo_str += " " + m_command_history->get_redo_description();
            }
            std::string redo_lbl = ShortcutManager::get().get_label("editor.redo");
            if (ImGui::MenuItem(redo_str.c_str(), redo_lbl.c_str(), false, can_r)) {
                if (m_command_history) {
                    m_command_history->redo();
                    update_window_title();
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Clear History", nullptr, false, m_command_history && (m_command_history->can_undo() || m_command_history->can_redo()))) {
                if (m_command_history) {
                    m_command_history->clear_history_preserving_dirty_state();
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Project Settings...")) {
                auto panel = get_panel("Project Settings");
                if (panel) panel->is_open = true;
            }
            if (ImGui::MenuItem("Shortcuts & Keybindings...")) {
                auto panel = get_panel("Shortcuts & Keybindings");
                if (panel) panel->is_open = true;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                ImGuiID dockspace_id = ImGui::GetID("Engine2DDockSpace");
                setup_default_dockspace_layout(dockspace_id);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject")) {
            Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
            bool has_scene = (active_scene != nullptr);

            if (ImGui::MenuItem("🗺️ 2D Tilemap", nullptr, false, has_scene)) {
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

            if (ImGui::MenuItem("🟦 2D Sprite Entity", nullptr, false, has_scene)) {
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

            if (ImGui::MenuItem("🎥 Camera 2D", nullptr, false, has_scene)) {
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

            if (ImGui::MenuItem("⭕ Empty Entity", nullptr, false, has_scene)) {
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
            if (ImGui::MenuItem("📁 Group Selection", "Cmd+G", false, has_selection)) {
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

            if (ImGui::MenuItem("📂 Ungroup", "Cmd+Shift+G", false, has_group_selected)) {
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

        if (ImGui::BeginMenu("🎮 Demos")) {
            if (m_scene_manager) {
                const auto& scenes = m_scene_manager->get_scenes();
                int active_idx = m_scene_manager->get_active_index();

                if (ImGui::MenuItem("🔄 Next Demo Scene", "TAB")) {
                    const int active_index = m_scene_manager->get_active_index();
                    const std::size_t next_index = static_cast<std::size_t>(
                        (active_index + 1 + static_cast<int>(scenes.size())) % static_cast<int>(scenes.size())
                    );
                    activate_scene(next_index);
                }
                if (ImGui::MenuItem("⏮️ Previous Demo Scene", "Shift+TAB")) {
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
                        label = "1. 🎮 Platformer Level (Authoring Scene)";
                    } else if (scenes[i]->get_name() == "2D Platformer & Collision Layer Matrix") {
                        label = "2. 🏃 2D Platformer & Collision Layers (C++ Sandbox)";
                    } else if (scenes[i]->get_name() == "2D Animated Sprites & Asset Loader") {
                        label = "3. 🧙 Top-Down Animated Sprites";
                    } else if (scenes[i]->get_name() == "2.5D Depth & Parallax Test") {
                        label = "4. 🌌 2.5D Depth & Parallax Test";
                    } else if (scenes[i]->get_name() == "GPU/CPU Stress Test") {
                        label = "5. ⚡ GPU/CPU Stress Test (3,000 Sprites)";
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
            for (auto& panel : m_panels) {
                if (panel) {
                    ImGui::MenuItem(panel->get_name().c_str(), nullptr, &panel->is_open);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Build & Export Game...", "Ctrl+B")) {
                ENGINE_LOG_INFO("[EDITOR] Export Game pipeline triggered.");
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

        // Right-aligned status indicators
        float right_pos = ImGui::GetWindowWidth() - 170.0f;
        if (right_pos > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(right_pos);
            ImGui::TextDisabled("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorLayer::render_panels(float delta_time) {
    if (!m_initialized || !m_visible) return;

    for (auto& panel : m_panels) {
        if (panel && panel->is_open) {
            panel->on_update(delta_time);
            panel->on_gui();
        }
    }
}

void EditorLayer::end_frame(SDL_Renderer* renderer) {
    if (!m_initialized || !m_visible) return;

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

void EditorLayer::register_panel(std::shared_ptr<EditorPanel> panel) {
    if (!panel) return;
    m_panels.push_back(panel);
    ENGINE_LOG_INFO("[EDITOR] Registered panel: '{}'", panel->get_name());
}

void EditorLayer::unregister_panel(const std::string& name) {
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (*it && (*it)->get_name() == name) {
            ENGINE_LOG_INFO("[EDITOR] Unregistered panel: '{}'", name);
            m_panels.erase(it);
            return;
        }
    }
}

std::shared_ptr<EditorPanel> EditorLayer::get_panel(const std::string& name) const {
    for (const auto& panel : m_panels) {
        if (panel && panel->get_name() == name) {
            return panel;
        }
    }
    return nullptr;
}

PrefabStageController* EditorLayer::get_prefab_stage() const {
    return m_prefab_stage.get();
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
