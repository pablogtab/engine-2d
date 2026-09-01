#include "ProjectHub.hpp"

#include "EditorTheme.hpp"
#include "core/Log.hpp"
#include "core/ProjectContext.hpp"
#include "core/Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace fs = std::filesystem;

namespace Engine {

struct ProjectHub::DialogState {
    std::mutex mutex;
    DialogPurpose purpose{DialogPurpose::OpenProject};
    bool complete{false};
    std::optional<fs::path> selection;
    std::string error;
};

namespace {

void SDLCALL folder_dialog_callback(void* userdata, const char* const* filelist, int) {
    std::unique_ptr<std::shared_ptr<ProjectHub::DialogState>> state_owner(
        static_cast<std::shared_ptr<ProjectHub::DialogState>*>(userdata)
    );
    const std::shared_ptr<ProjectHub::DialogState>& state = *state_owner;
    std::lock_guard lock(state->mutex);
    if (!filelist) {
        state->error = std::string("Folder dialog failed: ") + SDL_GetError();
    } else if (filelist[0]) {
        state->selection = fs::path(filelist[0]);
    }
    state->complete = true;
}

void copy_path_to_buffer(const fs::path& path, std::array<char, 1024>& buffer) {
    const std::string text = path.string();
    std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
}

} // namespace

ProjectHub::ProjectHub() {
    Log::init((fs::temp_directory_path() / "engine2d-project-hub.log").string());
    m_window = std::make_unique<Window>("Engine2D Project Hub", 920, 580, false);
    if (!m_window->get_sdl_window() || !m_window->get_sdl_renderer()) {
        m_status = "Cannot create the Project Hub window";
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    EditorTheme::apply_dark_theme();
    ImGui_ImplSDL3_InitForSDLRenderer(m_window->get_sdl_window(), m_window->get_sdl_renderer());
    ImGui_ImplSDLRenderer3_Init(m_window->get_sdl_renderer());
    m_window->set_event_callback([](const SDL_Event& event, const SDL_Event&) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    });

    m_registry_path = registry_path();
    std::string registry_error;
    if (!m_registry.load(m_registry_path, &registry_error)) m_status = std::move(registry_error);
    if (m_registry.remove_missing_projects()) save_registry();
    copy_path_to_buffer(fs::current_path(), m_parent_directory);
    m_initialized = true;
}

ProjectHub::~ProjectHub() {
    if (m_initialized) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
    m_window.reset();
    Log::shutdown();
}

std::optional<fs::path> ProjectHub::run(int frame_limit) {
    if (!m_initialized) return std::nullopt;
    int rendered_frames = 0;
    while (!m_window->is_should_close() && !m_selection) {
        m_window->poll_events();
        poll_folder_dialog();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        render();
        ImGui::Render();

        SDL_Renderer* renderer = m_window->get_sdl_renderer();
        SDL_SetRenderDrawColor(renderer, 18, 20, 27, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        if (frame_limit > 0 && ++rendered_frames >= frame_limit) break;
        SDL_Delay(8);
    }
    return m_selection;
}

void ProjectHub::render() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Engine2D Project Hub", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    ImGui::SetWindowFontScale(1.45f);
    ImGui::TextUnformatted("Engine2D");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Choose a project to start authoring");
    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    const float action_width = 230.0f;
    ImGui::BeginChild("RecentProjects", ImVec2(-action_width - 14.0f, 0.0f), true);
    render_recent_projects();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("ProjectActions", ImVec2(0.0f, 0.0f), true);
    ImGui::TextUnformatted("Projects");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button("Open Existing...", ImVec2(-1.0f, 42.0f))) {
        m_status.clear();
        show_folder_dialog(DialogPurpose::OpenProject);
    }
    if (ImGui::Button("Create New...", ImVec2(-1.0f, 42.0f))) {
        m_status.clear();
        m_project_name.fill('\0');
        copy_path_to_buffer(fs::current_path(), m_parent_directory);
        m_open_create_modal = true;
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextWrapped("A project owns its Assets, ProjectSettings, Library, and startup scene. Engine source remains separate.");
    if (!m_status.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.49f, 0.49f, 1.0f));
        ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    render_create_modal();
    ImGui::End();
}

void ProjectHub::render_recent_projects() {
    ImGui::TextUnformatted("Recent projects");
    ImGui::Separator();
    if (m_registry.projects().empty()) {
        ImGui::Dummy(ImVec2(0.0f, 18.0f));
        ImGui::TextDisabled("No recent projects yet.");
        ImGui::TextDisabled("Open an existing folder or create a new project.");
        return;
    }

    for (std::size_t index = 0; index < m_registry.projects().size(); ++index) {
        const RecentProject project = m_registry.projects()[index];
        ImGui::PushID(static_cast<int>(index));
        const ImVec2 start = ImGui::GetCursorPos();
        if (ImGui::Selectable("##Project", false, 0, ImVec2(-42.0f, 58.0f))) {
            select_project(project.path);
        }
        ImGui::SetCursorPos(ImVec2(start.x + 12.0f, start.y + 8.0f));
        ImGui::TextUnformatted(project.name.c_str());
        ImGui::SetCursorPos(ImVec2(start.x + 12.0f, start.y + 31.0f));
        ImGui::TextDisabled("%s", project.path.string().c_str());
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - 28.0f, start.y + 16.0f));
        if (ImGui::SmallButton("x")) {
            m_registry.remove(project.path);
            save_registry();
            ImGui::PopID();
            break;
        }
        ImGui::SetCursorPosY(start.y + 64.0f);
        ImGui::PopID();
    }
}

void ProjectHub::render_create_modal() {
    if (m_open_create_modal) {
        ImGui::OpenPopup("Create Engine2D Project");
        m_open_create_modal = false;
        m_show_create_modal = true;
    }
    ImGui::SetNextWindowSize(ImVec2(610.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Create Engine2D Project", &m_show_create_modal, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Project name");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##ProjectName", m_project_name.data(), m_project_name.size());
    ImGui::TextUnformatted("Parent directory");
    ImGui::SetNextItemWidth(-92.0f);
    ImGui::InputText("##ParentDirectory", m_parent_directory.data(), m_parent_directory.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(84.0f, 0.0f))) {
        show_folder_dialog(DialogPurpose::SelectCreateParent);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (!m_status.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.49f, 0.49f, 1.0f));
        ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }
    const bool can_create = ProjectWorkspace::is_valid_name(m_project_name.data()) &&
        m_parent_directory[0] != '\0' && !m_dialog_state;
    ImGui::BeginDisabled(!can_create);
    if (ImGui::Button("Create and Open", ImVec2(150.0f, 0.0f))) create_project();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
        m_show_create_modal = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ProjectHub::show_folder_dialog(DialogPurpose purpose) {
    if (m_dialog_state) return;
    auto state = std::make_shared<DialogState>();
    state->purpose = purpose;
    m_dialog_state = state;
    auto* callback_state = new std::shared_ptr<DialogState>(std::move(state));
    const char* default_location = purpose == DialogPurpose::SelectCreateParent
        ? m_parent_directory.data() : nullptr;
    SDL_ShowOpenFolderDialog(
        folder_dialog_callback,
        callback_state,
        m_window->get_sdl_window(),
        default_location,
        false
    );
}

void ProjectHub::poll_folder_dialog() {
    if (!m_dialog_state) return;
    std::optional<fs::path> selection;
    std::string error;
    DialogPurpose purpose;
    {
        std::lock_guard lock(m_dialog_state->mutex);
        if (!m_dialog_state->complete) return;
        selection = m_dialog_state->selection;
        error = m_dialog_state->error;
        purpose = m_dialog_state->purpose;
    }
    m_dialog_state.reset();
    if (!error.empty()) {
        m_status = std::move(error);
    } else if (selection) {
        if (purpose == DialogPurpose::OpenProject) select_project(*selection);
        else copy_path_to_buffer(*selection, m_parent_directory);
    }
}

void ProjectHub::select_project(const fs::path& path) {
    std::string error;
    const auto project = ProjectContext::open(path, &error);
    if (!project) {
        m_status = std::move(error);
        return;
    }
    m_registry.record({project->descriptor().name, project->root()});
    save_registry();
    m_selection = project->root();
}

void ProjectHub::create_project() {
    std::string error;
    const auto project_path = ProjectWorkspace::create(
        m_parent_directory.data(), m_project_name.data(), &error
    );
    if (!project_path) {
        m_status = std::move(error);
        return;
    }
    select_project(*project_path);
}

void ProjectHub::save_registry() {
    std::string error;
    if (!m_registry.save(m_registry_path, &error)) m_status = std::move(error);
}

fs::path ProjectHub::registry_path() {
    char* preference_path = SDL_GetPrefPath("Engine2D", "Editor");
    if (!preference_path) return fs::temp_directory_path() / "Engine2D/projects.json";
    const fs::path path = fs::path(preference_path) / "projects.json";
    SDL_free(preference_path);
    return path;
}

void ProjectHub::record_recent_project(const fs::path& project_path, const std::string& project_name) {
    ProjectRegistry registry;
    const fs::path path = registry_path();
    std::string error;
    static_cast<void>(registry.load(path, &error));
    registry.record({project_name, project_path});
    static_cast<void>(registry.save(path, &error));
}

} // namespace Engine
