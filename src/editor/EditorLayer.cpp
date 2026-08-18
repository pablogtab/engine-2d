#include "EditorLayer.hpp"
#include "EditorTheme.hpp"
#include "commands/EditorCommandHistory.hpp"
#include "commands/EntityCommands.hpp"
#include "shortcuts/ShortcutManager.hpp"
#include "inspector/InspectorWidgets.hpp"
#include "tools/EditorIcons.hpp"
#include "../core/Log.hpp"
#include "../scene/SceneManager.hpp"
#include "../scene/GenericScene.hpp"
#include "../scene/SceneSerializer.hpp"
#include "panels/ViewportPanel.hpp"
#include "../ecs/Components.hpp"
#include "../ecs/CameraSystem.hpp"
#include "../tilemap/Tilemap.hpp"
#include <filesystem>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

namespace Engine {

EditorLayer::~EditorLayer() {
    shutdown();
}

void EditorLayer::init(SDL_Window* window, SDL_Renderer* renderer) {
    if (m_initialized) return;

    m_window = window;
    m_renderer = renderer;

    // 1. Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    initialize_editor_icons();

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
        .callback = [this]() { if (m_command_history) m_command_history->undo(); }
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
        .callback = [this]() { if (m_command_history) m_command_history->redo(); }
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
        .id = "editor.new_scene",
        .display_name = "New Scene",
        .category = "File",
        .primary_key = ImGuiKey_N,
        .mod_cmd = true,
        .mod_ctrl = false,
        .mod_shift = false,
        .mod_alt = false,
        .enable_repeat = false,
        .callback = [this]() { create_new_scene("New Scene"); }
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

void EditorLayer::begin_frame() {
    if (!m_initialized || !m_visible) return;

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
    ImGui::DockBuilderDockWindow("Entity Inspector", dock_right_id);
    ImGui::DockBuilderDockWindow("Engine & Metrics", dock_right_id);
    ImGui::DockBuilderDockWindow("Console & Logs", dock_down_id);
    ImGui::DockBuilderDockWindow("Asset Browser", dock_down_id);

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
    m_scene_manager->set_active_scene(name);
    ENGINE_LOG_INFO("[EDITOR] Created new blank scene: '{}'", name);
}

void EditorLayer::save_active_scene(const std::string& custom_path) {
    if (!m_scene_manager) return;
    auto active_scene = m_scene_manager->get_active_scene();
    if (!active_scene) {
        ENGINE_LOG_WARN("[EDITOR] No active scene to save.");
        return;
    }

    std::string path = custom_path;
    if (path.empty()) {
        std::filesystem::create_directories("assets/scenes");
        path = "assets/scenes/" + active_scene->get_name() + ".scene";
    }

    SceneSerializer serializer(active_scene, m_renderer);
    serializer.serialize(path);
}

void EditorLayer::load_scene(const std::string& path) {
    if (!m_scene_manager) return;
    std::filesystem::path p(path);
    std::string scene_name = p.stem().string();

    auto new_scene = std::make_shared<GenericScene>(scene_name);
    SceneSerializer serializer(new_scene.get(), m_renderer);
    if (serializer.deserialize(path)) {
        m_scene_manager->add_scene(new_scene);
        m_scene_manager->set_active_scene(scene_name);
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
}

void EditorLayer::render_main_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            std::string new_lbl = ShortcutManager::get().get_label("editor.new_scene");
            if (ImGui::MenuItem("New Scene", new_lbl.empty() ? nullptr : new_lbl.c_str())) {
                create_new_scene("New Scene");
            }

            std::string save_lbl = ShortcutManager::get().get_label("editor.save_scene");
            if (ImGui::MenuItem("Save Scene", save_lbl.empty() ? nullptr : save_lbl.c_str())) {
                save_active_scene();
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                m_show_save_as_popup = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                SDL_Event quit_ev;
                quit_ev.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit_ev);
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
                if (m_command_history) m_command_history->undo();
            }

            bool can_r = m_command_history && m_command_history->can_redo();
            std::string redo_str = "Redo";
            if (can_r) {
                redo_str += " " + m_command_history->get_redo_description();
            }
            std::string redo_lbl = ShortcutManager::get().get_label("editor.redo");
            if (ImGui::MenuItem(redo_str.c_str(), redo_lbl.c_str(), false, can_r)) {
                if (m_command_history) m_command_history->redo();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Clear History", nullptr, false, m_command_history && (m_command_history->can_undo() || m_command_history->can_redo()))) {
                if (m_command_history) m_command_history->clear();
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
                    snapshot.has_tag = true;
                    snapshot.tag = TagComponent{"Tilemap"};
                    snapshot.has_transform = true;
                    snapshot.transform = TransformComponent{glm::vec3(0.0f)};
                    snapshot.has_tilemap = true;
                    snapshot.tilemap = TilemapComponent{};
                    if (m_command_history) m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, nullptr, std::move(snapshot)));
                }
            }

            if (ImGui::MenuItem("🟦 2D Sprite Entity", nullptr, false, has_scene)) {
                if (active_scene) {
                    EntityManager& em = active_scene->get_entity_manager();
                    EntitySnapshot snapshot;
                    snapshot.has_tag = true;
                    snapshot.tag = TagComponent{"Sprite_Entity"};
                    snapshot.has_transform = true;
                    snapshot.transform = TransformComponent{glm::vec3(0.0f)};
                    snapshot.has_sprite = true;
                    snapshot.sprite = SpriteComponent{nullptr, glm::vec2(64.0f, 64.0f)};
                    if (m_command_history) m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, nullptr, std::move(snapshot)));
                }
            }

            if (ImGui::MenuItem("🎥 Camera 2D", nullptr, false, has_scene)) {
                if (active_scene) {
                    EntityManager& em = active_scene->get_entity_manager();
                    EntitySnapshot snapshot;
                    snapshot.has_tag = true;
                    snapshot.tag = TagComponent{"Main_Camera"};
                    snapshot.has_transform = true;
                    snapshot.transform = TransformComponent{glm::vec3(0.0f, 60.0f, 0.0f)};
                    snapshot.has_camera = true;
                    snapshot.camera = CameraComponent{};
                    snapshot.camera.is_primary = !CameraSystem::has_camera(em);
                    if (m_command_history) m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, nullptr, std::move(snapshot)));
                }
            }

            if (ImGui::MenuItem("⭕ Empty Entity", nullptr, false, has_scene)) {
                if (active_scene) {
                    EntityManager& em = active_scene->get_entity_manager();
                    EntitySnapshot snapshot;
                    snapshot.has_tag = true;
                    snapshot.tag = TagComponent{"Empty_Entity"};
                    snapshot.has_transform = true;
                    snapshot.transform = TransformComponent{glm::vec3(0.0f)};
                    if (m_command_history) m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, nullptr, std::move(snapshot)));
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("🎮 Demos")) {
            if (m_scene_manager) {
                const auto& scenes = m_scene_manager->get_scenes();
                int active_idx = m_scene_manager->get_active_index();

                if (ImGui::MenuItem("🔄 Next Demo Scene", "TAB")) {
                    m_scene_manager->next_scene();
                }
                if (ImGui::MenuItem("⏮️ Previous Demo Scene", "Shift+TAB")) {
                    m_scene_manager->previous_scene();
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
                        m_scene_manager->set_active_scene(i);
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

} // namespace Engine
