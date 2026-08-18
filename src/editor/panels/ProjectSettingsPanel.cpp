#include "ProjectSettingsPanel.hpp"

#include "../commands/EditorCommandHistory.hpp"
#include "../commands/ProjectSettingsCommands.hpp"
#include "../../scene/SceneManager.hpp"
#include <algorithm>
#include <imgui.h>
#include <utility>

namespace Engine {

ProjectSettingsPanel::ProjectSettingsPanel(
    ProjectSettings* settings,
    SceneManager* scene_manager,
    std::shared_ptr<EditorCommandHistory> command_history
)
    : m_settings(settings),
      m_scene_manager(scene_manager),
      m_command_history(std::move(command_history)) {
    is_open = false;
}

void ProjectSettingsPanel::apply_changes() {
    if (!m_settings || !m_command_history || m_pending == m_settings->game_view()) return;

    const GameViewSettings before = m_settings->game_view();
    const GameViewSettings after = m_pending;
    m_command_history->execute_command(std::make_unique<ModifyGameViewSettingsCommand>(
        m_settings,
        before,
        after,
        [scene_manager = m_scene_manager](const GameViewSettings& value) {
            if (scene_manager) {
                scene_manager->configure_view(RenderViewKind::Game, {
                    value.reference_width, value.reference_height,
                    value.reference_width, value.reference_height
                });
            }
        }
    ));
    m_observed = m_settings->game_view();
    m_pending = m_observed;
}

void ProjectSettingsPanel::on_gui() {
    if (!is_open || !m_settings) return;

    const GameViewSettings current = m_settings->game_view();
    if (!m_initialized || (m_pending == m_observed && current != m_observed)) {
        m_pending = current;
        m_observed = current;
        m_initialized = true;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        if (ImGui::BeginTable("ProjectSettingsLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthFixed, 145.0f);
            ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::Selectable("Display", true);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Display");
            ImGui::TextDisabled("Reference canvas and window presentation used by the game.");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Reference resolution");
            if (ImGui::Button("1920 x 1080")) m_pending = {1920, 1080, m_pending.presentation};
            ImGui::SameLine();
            if (ImGui::Button("1280 x 720")) m_pending = {1280, 720, m_pending.presentation};
            ImGui::SameLine();
            if (ImGui::Button("1080 x 1920")) m_pending = {1080, 1920, m_pending.presentation};

            ImGui::Spacing();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputInt("Width", &m_pending.reference_width, 0);
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputInt("Height", &m_pending.reference_height, 0);
            m_pending.reference_width = std::clamp(m_pending.reference_width, 64, 16384);
            m_pending.reference_height = std::clamp(m_pending.reference_height, 64, 16384);

            int presentation = static_cast<int>(m_pending.presentation);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::Combo("Presentation", &presentation, "Fit\0Fill\0Stretch\0Responsive\0")) {
                m_pending.presentation = static_cast<PresentationMode>(presentation);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Fit preserves the image, Fill crops, Stretch distorts, Responsive reprojects for the available aspect ratio.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            const bool has_changes = m_pending != current;
            ImGui::BeginDisabled(!has_changes);
            if (ImGui::Button("Apply", ImVec2(100.0f, 0.0f))) apply_changes();
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(100.0f, 0.0f))) m_pending = current;
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace Engine
