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
                scene_manager->resize_view(RenderViewKind::Game, value.width, value.height, 1.0f);
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
            ImGui::TextUnformatted("Game Output");
            ImGui::TextDisabled("Default resolution and presentation used in Play mode.");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Resolution presets");
            if (ImGui::Button("1920 x 1080")) m_pending = {1920, 1080, m_pending.scaling};
            ImGui::SameLine();
            if (ImGui::Button("1280 x 720")) m_pending = {1280, 720, m_pending.scaling};
            ImGui::SameLine();
            if (ImGui::Button("1080 x 1920")) m_pending = {1080, 1920, m_pending.scaling};

            ImGui::Spacing();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputInt("Width", &m_pending.width, 0);
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputInt("Height", &m_pending.height, 0);
            m_pending.width = std::clamp(m_pending.width, 64, 16384);
            m_pending.height = std::clamp(m_pending.height, 64, 16384);

            int scaling = static_cast<int>(m_pending.scaling);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::Combo("Presentation", &scaling, "Fit\0Fill\0Stretch\0")) {
                m_pending.scaling = static_cast<GameViewScaling>(scaling);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Fit preserves the full image, Fill crops it, Stretch fills the available area.");
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
