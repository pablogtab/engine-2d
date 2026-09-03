#include "EditorShell.hpp"

#include "EditorTheme.hpp"
#include "shortcuts/ShortcutManager.hpp"
#include "tools/EditorIcons.hpp"
#include "ui/EditorUi.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>

namespace Engine {

void EditorShell::initialize(const std::filesystem::path& workspace_state_path) {
    m_workspace_state_path = workspace_state_path;
    static_cast<void>(m_workspaces.load(m_workspace_state_path.string()));
}

bool EditorShell::activate_workspace(EditorWorkspaceId id) {
    if (!m_workspaces.set_active(id)) return false;
    static_cast<void>(m_workspaces.save(m_workspace_state_path.string()));
    return true;
}

void EditorShell::render_workspace_bar(
    const EditorShellState& state,
    const std::function<void(EditorWorkspaceId)>& on_workspace_selected
) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, palette.panel_raised);
    if (ImGui::BeginViewportSideBar(
            "##Engine2DWorkspaceBar", viewport, ImGuiDir_Up,
            metrics.toolbar_height, flags)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::TextDisabled("Workspaces:");
            ImGui::SameLine(0.0f, 10.0f * metrics.scale);

            for (const auto& descriptor : m_workspaces.descriptors()) {
                const bool active = descriptor.id == m_workspaces.active();
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    active ? palette.selected : ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
                );
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.selected);
                const std::string button_id =
                    std::string(descriptor.label) + "##workspace_" + descriptor.key;
                if (ImGui::Button(button_id.c_str()) && on_workspace_selected) {
                    on_workspace_selected(descriptor.id);
                }
                ImGui::PopStyleColor(2);
                ImGui::SameLine();
            }

            if (!state.scene_name.empty()) {
                const std::string scene_label =
                    state.scene_name + (state.scene_dirty ? " *" : "");
                const float label_width = ImGui::CalcTextSize(scene_label.c_str()).x;
                const float right = ImGui::GetWindowWidth() - label_width - 12.0f * metrics.scale;
                if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
                ImGui::TextDisabled("%s", scene_label.c_str());
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void EditorShell::render_status_bar(const EditorShellState& state) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, palette.header);
    if (ImGui::BeginViewportSideBar(
            "##Engine2DStatusBar", viewport, ImGuiDir_Down,
            metrics.status_height, flags)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::TextColored(
                state.playing ? palette.warning : palette.success,
                "%s", state.playing ? "PLAY MODE" : "READY"
            );
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", m_workspaces.active_descriptor().label);
            if (state.selection_count > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("| %zu selected", state.selection_count);
            }

            const float fps = ImGui::GetIO().Framerate;
            const std::string performance = fps > 0.0f
                ? "SDL GPU  |  " + std::to_string(static_cast<int>(std::round(fps))) + " FPS"
                : "SDL GPU";
            const float width = ImGui::CalcTextSize(performance.c_str()).x;
            const float right = ImGui::GetWindowWidth() - width - 10.0f * metrics.scale;
            if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
            ImGui::TextDisabled("%s", performance.c_str());
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void EditorShell::render_command_palette() {
    if (m_show_command_palette) {
        m_show_command_palette = false;
        m_command_palette_query[0] = '\0';
        ImGui::OpenPopup("Command Palette");
    }

    ImGui::SetNextWindowSize(ImVec2(620.0f, 420.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(
            "Command Palette", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    EditorUi::area_heading(EditorIcon::Zap, "Command Palette");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    EditorUi::search_field(
        "##command_palette_query", "Type an action name...",
        m_command_palette_query, sizeof(m_command_palette_query), -1.0f
    );
    ImGui::Separator();

    const auto results = m_actions.search(m_command_palette_query);
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("CommandPaletteResults", ImVec2(0.0f, -footer), false);
    const EditorAction* first_enabled = nullptr;
    for (const EditorAction* action : results) {
        if (!action) continue;
        const bool enabled = m_actions.is_enabled(action->id);
        if (enabled && !first_enabled) first_enabled = action;
        const std::string shortcut = ShortcutManager::get().get_label(
            action->shortcut_id.empty() ? action->id : action->shortcut_id
        );
        const std::string row = action->label + "##palette_" + action->id;
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Selectable(row.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
            static_cast<void>(m_actions.execute(action->id));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(360.0f);
        ImGui::TextDisabled("%s", action->category.c_str());
        if (!shortcut.empty()) {
            const float shortcut_width = ImGui::CalcTextSize(shortcut.c_str()).x;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - shortcut_width);
            ImGui::TextDisabled("%s", shortcut.c_str());
        }
        ImGui::EndDisabled();
    }
    if (results.empty()) {
        EditorUi::empty_state(EditorIcon::Zap, "No matching actions", "Try a shorter query");
    }
    ImGui::EndChild();

    if (first_enabled && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        static_cast<void>(m_actions.execute(first_enabled->id));
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace Engine
