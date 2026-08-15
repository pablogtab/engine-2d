#include "EditorLayer.hpp"
#include "EditorTheme.hpp"
#include "../core/Log.hpp"

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

    // 2. Setup Dear ImGui Modern Theme
    EditorTheme::apply_dark_theme();

    // 3. Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    m_initialized = true;
    ENGINE_LOG_INFO("[EDITOR] Dear ImGui initialized successfully (Docking Enabled).");
}

void EditorLayer::shutdown() {
    if (!m_initialized) return;

    m_panels.clear();
    m_viewport_panel = nullptr;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    m_initialized = false;
    m_window = nullptr;
    m_renderer = nullptr;
    ENGINE_LOG_INFO("[EDITOR] Dear ImGui shutdown cleanly.");
}

void EditorLayer::process_event(const SDL_Event& event) {
    if (!m_initialized) return;

    // Toggle editor visibility with F1 or Tilde
    if (event.type == SDL_EVENT_KEY_DOWN) {
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

    ImGui::DockBuilderFinish(dockspace_id);
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
}

void EditorLayer::render_main_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                ENGINE_LOG_INFO("[EDITOR] 'New Scene' triggered.");
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                ENGINE_LOG_INFO("[EDITOR] 'Open Scene' triggered.");
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                ENGINE_LOG_INFO("[EDITOR] 'Save Scene' triggered.");
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
            if (ImGui::MenuItem("Reset Layout")) {
                ImGuiID dockspace_id = ImGui::GetID("Engine2DDockSpace");
                setup_default_dockspace_layout(dockspace_id);
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
