#include "ProjectSettingsPanel.hpp"
#include "../ui/EditorUi.hpp"

#include "../commands/EditorCommandHistory.hpp"
#include "../commands/ProjectSettingsCommands.hpp"
#include "../../scene/SceneManager.hpp"
#include <algorithm>
#include <cmath>
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
    if (!m_settings || !m_command_history || m_pending == m_settings->display()) return;

    const ProjectDisplaySettings before = m_settings->display();
    const ProjectDisplaySettings after = m_pending;
    m_command_history->execute_command(std::make_unique<ModifyProjectDisplaySettingsCommand>(
        m_settings,
        before,
        after,
        [scene_manager = m_scene_manager, before, after](const ProjectDisplaySettings& value) {
            const bool view_contract_changed =
                before.reference_width != after.reference_width ||
                before.reference_height != after.reference_height ||
                before.presentation != after.presentation;
            if (scene_manager && view_contract_changed) {
                scene_manager->configure_view(RenderViewKind::Game, {
                    value.reference_width, value.reference_height,
                    value.reference_width, value.reference_height
                });
            }
        }
    ));
    m_observed = m_settings->display();
    m_pending = m_observed;
}

void ProjectSettingsPanel::on_gui() {
    if (!is_open || !m_settings) return;

    const ProjectDisplaySettings current = m_settings->display();
    if (!m_initialized || (m_pending == m_observed && current != m_observed)) {
        m_pending = current;
        m_observed = current;
        m_initialized = true;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        EditorUi::area_heading(EditorIcon::Settings, "Project Settings", "Display and authoring defaults");
        if (ImGui::BeginTable("ProjectSettingsLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthFixed, 145.0f);
            ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::Selectable("Display", true);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Display");
            ImGui::TextDisabled("Authoring reference and independent desktop window defaults.");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Authoring reference");
            if (ImGui::Button("1280 x 720")) {
                m_pending.reference_width = 1280;
                m_pending.reference_height = 720;
            }
            ImGui::SameLine();
            if (ImGui::Button("1920 x 1080")) {
                m_pending.reference_width = 1920;
                m_pending.reference_height = 1080;
            }
            ImGui::SameLine();
            if (ImGui::Button("1080 x 1920")) {
                m_pending.reference_width = 1080;
                m_pending.reference_height = 1920;
            }

            ImGui::TextDisabled("Defines game coordinates and camera composition, not the player's monitor resolution.");

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

            int orientation = static_cast<int>(m_pending.orientation_policy);
            ImGui::SetNextItemWidth(190.0f);
            if (ImGui::Combo(
                    "Allowed orientations",
                    &orientation,
                    "Auto Rotation\0Landscape\0Portrait\0Landscape Left\0Landscape Right\0Portrait Upright\0Portrait Upside Down\0"
                )) {
                m_pending.orientation_policy = static_cast<OrientationPolicy>(orientation);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Applied before SDL starts on iOS and Android. Restart the runtime after changing it."
                );
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted("Rendering resolution");
            ImGui::TextDisabled("World, lighting, and post-processing only. Canvas UI remains native.");

            int scale_mode = static_cast<int>(m_pending.render_resolution.mode);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::Combo("Scale mode", &scale_mode, "Fixed\0Dynamic\0")) {
                m_pending.render_resolution.mode = static_cast<RenderScaleMode>(scale_mode);
            }

            if (m_pending.render_resolution.mode == RenderScaleMode::Fixed) {
                const float current_scale = m_pending.render_resolution.fixed_scale;
                int preset = current_scale < 0.625f ? 0 : (current_scale < 0.875f ? 1 : 2);
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::Combo("Render scale", &preset, "50%\0" "75%\0" "100%\0")) {
                    constexpr float scales[]{0.5f, 0.75f, 1.0f};
                    m_pending.render_resolution.fixed_scale = scales[preset];
                }
            } else {
                int minimum = static_cast<int>(std::round(
                    m_pending.render_resolution.dynamic_min_scale * 100.0f
                ));
                int maximum = static_cast<int>(std::round(
                    m_pending.render_resolution.dynamic_max_scale * 100.0f
                ));
                int target = static_cast<int>(std::round(
                    m_pending.render_resolution.dynamic_target_fps
                ));
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::SliderInt("Minimum scale", &minimum, 50, 100, "%d%%")) {
                    m_pending.render_resolution.dynamic_min_scale = minimum / 100.0f;
                }
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::SliderInt("Maximum scale", &maximum, 50, 100, "%d%%")) {
                    m_pending.render_resolution.dynamic_max_scale = maximum / 100.0f;
                }
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::InputInt("Target FPS", &target, 0)) {
                    m_pending.render_resolution.dynamic_target_fps = static_cast<float>(target);
                }
                m_pending.render_resolution = normalize_render_resolution_settings(
                    m_pending.render_resolution
                );
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted("Desktop defaults");
            ImGui::TextDisabled("Initial window values. Runtime player preferences remain separate.");

            int window_mode = static_cast<int>(m_pending.default_window_mode);
            ImGui::SetNextItemWidth(190.0f);
            if (ImGui::Combo("Window mode", &window_mode, "Windowed\0Borderless Fullscreen\0Exclusive Fullscreen\0")) {
                m_pending.default_window_mode = static_cast<WindowMode>(window_mode);
            }

            ImGui::BeginDisabled(m_pending.default_window_mode == WindowMode::BorderlessFullscreen);
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputInt("Window width", &m_pending.default_window_width, 0);
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputInt("Window height", &m_pending.default_window_height, 0);
            ImGui::EndDisabled();
            m_pending.default_window_width = std::clamp(m_pending.default_window_width, 320, 16384);
            m_pending.default_window_height = std::clamp(m_pending.default_window_height, 240, 16384);

            ImGui::BeginDisabled(m_pending.default_window_mode != WindowMode::ExclusiveFullscreen);
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputFloat("Refresh rate", &m_pending.default_refresh_rate, 0.0f, 0.0f, "%.2f Hz");
            ImGui::EndDisabled();
            m_pending.default_refresh_rate = std::max(m_pending.default_refresh_rate, 0.0f);
            ImGui::Checkbox("VSync", &m_pending.default_vsync);

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
