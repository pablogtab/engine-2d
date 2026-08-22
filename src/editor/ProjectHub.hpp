#pragma once

#include "core/ProjectWorkspace.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct SDL_Window;

namespace Engine {

class Window;

class ProjectHub {
public:
    struct DialogState;

    ProjectHub();
    ~ProjectHub();

    ProjectHub(const ProjectHub&) = delete;
    ProjectHub& operator=(const ProjectHub&) = delete;

    [[nodiscard]] std::optional<std::filesystem::path> run(int frame_limit = 0);
    static void record_recent_project(
        const std::filesystem::path& project_path,
        const std::string& project_name
    );

private:
    enum class DialogPurpose {
        OpenProject,
        SelectCreateParent
    };

    void render();
    void render_recent_projects();
    void render_create_modal();
    void show_folder_dialog(DialogPurpose purpose);
    void poll_folder_dialog();
    void select_project(const std::filesystem::path& path);
    void create_project();
    void save_registry();

    static std::filesystem::path registry_path();

    std::unique_ptr<Window> m_window;
    ProjectRegistry m_registry;
    std::filesystem::path m_registry_path;
    std::optional<std::filesystem::path> m_selection;
    std::shared_ptr<DialogState> m_dialog_state;
    std::array<char, 256> m_project_name{};
    std::array<char, 1024> m_parent_directory{};
    std::string m_status;
    bool m_initialized{false};
    bool m_show_create_modal{false};
    bool m_open_create_modal{false};
};

} // namespace Engine
