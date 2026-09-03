#include "ProjectSettingsPanel.hpp"
#include "../EditorTheme.hpp"
#include "../inspector/InspectorWidgets.hpp"
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

void ProjectSettingsPanel::draw_navigation() {
    const auto& metrics = EditorTheme::metrics();
    EditorUi::section_heading("SETTINGS");

    const auto item = [this, &metrics](Category category, EditorIcon icon, const char* label) {
        const bool selected = m_category == category;
        const std::string item_label = editor_icon_label(icon, label);
        if (ImGui::Selectable(
                item_label.c_str(), selected, ImGuiSelectableFlags_None,
                ImVec2(0.0f, metrics.row_height))) {
            m_category = category;
        }
    };

    item(Category::Display, EditorIcon::Camera, "Display");
    item(Category::Rendering, EditorIcon::Grid, "Rendering");
    item(Category::Window, EditorIcon::World, "Window");
}

void ProjectSettingsPanel::draw_display_settings() {
    ImGui::TextUnformatted("Display");
    ImGui::TextDisabled("Game coordinates, composition, and supported orientations.");
    ImGui::Spacing();
    EditorUi::section_heading("AUTHORING REFERENCE");

    ImGui::PushTextWrapPos();
    ImGui::TextDisabled(
        "Defines game coordinates and camera composition, not the player's monitor resolution."
    );
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

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
    ImGui::Spacing();

    InspectorWidgets::property_int("Width", &m_pending.reference_width, 1.0f, 64, 16384);
    InspectorWidgets::property_int("Height", &m_pending.reference_height, 1.0f, 64, 16384);
    m_pending.reference_width = std::clamp(m_pending.reference_width, 64, 16384);
    m_pending.reference_height = std::clamp(m_pending.reference_height, 64, 16384);

    int presentation = static_cast<int>(m_pending.presentation);
    constexpr const char* presentation_labels[]{"Fit", "Fill", "Stretch", "Responsive"};
    if (InspectorWidgets::property_enum(
            "Presentation", &presentation, presentation_labels, 4)) {
        m_pending.presentation = static_cast<PresentationMode>(presentation);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Fit preserves the image, Fill crops, Stretch distorts, and Responsive reprojects."
        );
    }

    int orientation = static_cast<int>(m_pending.orientation_policy);
    constexpr const char* orientation_labels[]{
        "Auto Rotation", "Landscape", "Portrait", "Landscape Left", "Landscape Right",
        "Portrait Upright", "Portrait Upside Down"
    };
    if (InspectorWidgets::property_enum(
            "Orientations", &orientation, orientation_labels, 7)) {
        m_pending.orientation_policy = static_cast<OrientationPolicy>(orientation);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Applied before SDL starts on mobile platforms. Restart after changing it."
        );
    }
}

void ProjectSettingsPanel::draw_rendering_settings() {
    ImGui::TextUnformatted("Rendering");
    ImGui::TextDisabled("Resolution policy for the world rendering pipeline.");
    ImGui::Spacing();
    EditorUi::section_heading("RENDER RESOLUTION");

    ImGui::PushTextWrapPos();
    ImGui::TextDisabled(
        "Affects world, lighting, and post-processing. Canvas UI remains at native resolution."
    );
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    int scale_mode = static_cast<int>(m_pending.render_resolution.mode);
    constexpr const char* scale_mode_labels[]{"Fixed", "Dynamic"};
    if (InspectorWidgets::property_enum("Scale mode", &scale_mode, scale_mode_labels, 2)) {
        m_pending.render_resolution.mode = static_cast<RenderScaleMode>(scale_mode);
    }

    if (m_pending.render_resolution.mode == RenderScaleMode::Fixed) {
        const float current_scale = m_pending.render_resolution.fixed_scale;
        int preset = current_scale < 0.625f ? 0 : (current_scale < 0.875f ? 1 : 2);
        constexpr const char* scale_labels[]{"50%", "75%", "100%"};
        if (InspectorWidgets::property_enum("Render scale", &preset, scale_labels, 3)) {
            constexpr float scales[]{0.5f, 0.75f, 1.0f};
            m_pending.render_resolution.fixed_scale = scales[preset];
        }
        return;
    }

    int minimum = static_cast<int>(std::round(
        m_pending.render_resolution.dynamic_min_scale * 100.0f
    ));
    int maximum = static_cast<int>(std::round(
        m_pending.render_resolution.dynamic_max_scale * 100.0f
    ));
    int target = static_cast<int>(std::round(
        m_pending.render_resolution.dynamic_target_fps
    ));
    if (InspectorWidgets::property_int("Minimum scale", &minimum, 1.0f, 50, 100, "%d%%")) {
        m_pending.render_resolution.dynamic_min_scale = minimum / 100.0f;
    }
    if (InspectorWidgets::property_int("Maximum scale", &maximum, 1.0f, 50, 100, "%d%%")) {
        m_pending.render_resolution.dynamic_max_scale = maximum / 100.0f;
    }
    if (InspectorWidgets::property_int("Target FPS", &target, 1.0f, 1, 1000)) {
        m_pending.render_resolution.dynamic_target_fps = static_cast<float>(target);
    }
    m_pending.render_resolution = normalize_render_resolution_settings(
        m_pending.render_resolution
    );
}

void ProjectSettingsPanel::draw_window_settings() {
    ImGui::TextUnformatted("Window");
    ImGui::TextDisabled("Initial desktop window defaults for exported games.");
    ImGui::Spacing();
    EditorUi::section_heading("DESKTOP DEFAULTS");

    ImGui::PushTextWrapPos();
    ImGui::TextDisabled(
        "These values initialize the game window. Runtime player preferences remain separate."
    );
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    int window_mode = static_cast<int>(m_pending.default_window_mode);
    constexpr const char* window_mode_labels[]{
        "Windowed", "Borderless Fullscreen", "Exclusive Fullscreen"
    };
    if (InspectorWidgets::property_enum("Window mode", &window_mode, window_mode_labels, 3)) {
        m_pending.default_window_mode = static_cast<WindowMode>(window_mode);
    }

    const bool uses_desktop_size =
        m_pending.default_window_mode == WindowMode::BorderlessFullscreen;
    ImGui::BeginDisabled(uses_desktop_size);
    InspectorWidgets::property_int(
        "Window width", &m_pending.default_window_width, 1.0f, 320, 16384
    );
    InspectorWidgets::property_int(
        "Window height", &m_pending.default_window_height, 1.0f, 240, 16384
    );
    ImGui::EndDisabled();
    m_pending.default_window_width = std::clamp(m_pending.default_window_width, 320, 16384);
    m_pending.default_window_height = std::clamp(m_pending.default_window_height, 240, 16384);

    const bool exclusive =
        m_pending.default_window_mode == WindowMode::ExclusiveFullscreen;
    ImGui::BeginDisabled(!exclusive);
    InspectorWidgets::property_float(
        "Refresh rate", &m_pending.default_refresh_rate, 0.25f, 0.0f, 1000.0f, "%.2f Hz"
    );
    ImGui::EndDisabled();
    m_pending.default_refresh_rate = std::max(m_pending.default_refresh_rate, 0.0f);
    InspectorWidgets::property_bool("VSync", &m_pending.default_vsync);

    if (uses_desktop_size) {
        ImGui::TextDisabled("Borderless mode uses the current desktop dimensions.");
    } else if (!exclusive) {
        ImGui::TextDisabled("Refresh rate is controlled by the desktop in windowed mode.");
    }
}

void ProjectSettingsPanel::draw_action_bar(const ProjectDisplaySettings& current) {
    const auto& metrics = EditorTheme::metrics();
    const bool has_changes = m_pending != current;
    if (!ImGui::BeginTable(
            "##project_settings_actions", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }

    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 220.0f * metrics.scale);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (has_changes) {
        EditorUi::badge("UNAPPLIED CHANGES", EditorUi::BadgeTone::Warning);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("All changes applied");
    }

    ImGui::TableSetColumnIndex(1);
    if (EditorUi::action_button(
            "revert_project_settings", EditorIcon::History, "Revert",
            EditorUi::ButtonTone::Neutral, has_changes, ImVec2(100.0f * metrics.scale, 0.0f))) {
        m_pending = current;
    }
    ImGui::SameLine();
    if (EditorUi::action_button(
            "apply_project_settings", EditorIcon::Save, "Apply",
            EditorUi::ButtonTone::Primary, has_changes, ImVec2(100.0f * metrics.scale, 0.0f))) {
        apply_changes();
    }
    ImGui::EndTable();
}

void ProjectSettingsPanel::on_gui() {
    if (!is_open || !m_settings) return;

    const ProjectDisplaySettings current = m_settings->display();
    if (!m_initialized || (m_pending == m_observed && current != m_observed)) {
        m_pending = current;
        m_observed = current;
        m_initialized = true;
    }

    if (!ImGui::IsPopupOpen(m_name.c_str())) ImGui::OpenPopup(m_name.c_str());

    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)
    );
    ImGui::SetNextWindowSize(ImVec2(760.0f, 540.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(620.0f, 420.0f), ImVec2(1100.0f, 850.0f));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::BeginPopupModal(m_name.c_str(), &is_open, flags)) return;

    const float footer_height = metrics.toolbar_height + metrics.compact_spacing * 2.0f;
    ImGui::BeginChild("ProjectSettingsBody", ImVec2(0.0f, -footer_height), false);
    if (ImGui::BeginTable(
            "ProjectSettingsLayout", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(
            "Categories", ImGuiTableColumnFlags_WidthFixed, 165.0f * metrics.scale
        );
        ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panel);
        ImGui::BeginChild(
            "ProjectSettingsNavigation", ImVec2(0.0f, 0.0f), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        );
        draw_navigation();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("ProjectSettingsContent", ImVec2(0.0f, 0.0f), false);
        switch (m_category) {
            case Category::Display: draw_display_settings(); break;
            case Category::Rendering: draw_rendering_settings(); break;
            case Category::Window: draw_window_settings(); break;
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::Separator();
    draw_action_bar(current);
    ImGui::EndPopup();
}

} // namespace Engine
