#pragma once

#include "app/EditorActionRegistry.hpp"
#include "workspace/WorkspaceManager.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace Engine {

struct EditorShellState {
    std::string scene_name;
    bool scene_dirty{false};
    bool playing{false};
    std::size_t selection_count{0};
};

class EditorShell {
public:
    void initialize(const std::filesystem::path& workspace_state_path);

    [[nodiscard]] EditorActionRegistry& actions() { return m_actions; }
    [[nodiscard]] const EditorActionRegistry& actions() const { return m_actions; }
    [[nodiscard]] WorkspaceManager& workspaces() { return m_workspaces; }
    [[nodiscard]] const WorkspaceManager& workspaces() const { return m_workspaces; }

    [[nodiscard]] bool activate_workspace(EditorWorkspaceId id);
    void request_command_palette() { m_show_command_palette = true; }

    void render_workspace_bar(
        const EditorShellState& state,
        const std::function<void(EditorWorkspaceId)>& on_workspace_selected
    );
    void render_status_bar(const EditorShellState& state);
    void render_command_palette();

private:
    std::filesystem::path m_workspace_state_path{"Library/Editor/WorkspaceState.json"};
    WorkspaceManager m_workspaces;
    EditorActionRegistry m_actions;
    bool m_show_command_palette{false};
    char m_command_palette_query[128]{};
};

} // namespace Engine
