#include "UiGalleryPanel.hpp"

#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"

#include <imgui.h>

namespace Engine {

void UiGalleryPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    EditorUi::area_heading(EditorIcon::Material, "Engine2D UI Kit");

    EditorUi::section_heading("STATUS");
    EditorUi::badge("NEUTRAL");
    ImGui::SameLine();
    EditorUi::badge("ACTIVE", EditorUi::BadgeTone::Accent);
    ImGui::SameLine();
    EditorUi::badge("READY", EditorUi::BadgeTone::Success);
    ImGui::SameLine();
    EditorUi::badge("WARNING", EditorUi::BadgeTone::Warning);
    ImGui::SameLine();
    EditorUi::badge("ERROR", EditorUi::BadgeTone::Error);

    ImGui::Spacing();
    EditorUi::section_heading("FIELDS", "normal, focused, mixed, disabled");
    EditorUi::search_field(
        "##gallery_search", "Search project content...",
        m_search.data(), m_search.size(), 320.0f
    );
    ImGui::Checkbox("Boolean property", &m_toggle);
    const char* modes[] = {"Edit", "Preview", "Runtime"};
    ImGui::Combo("Mode", &m_mode, modes, 3);
    ImGui::BeginDisabled();
    ImGui::Button("Disabled action");
    ImGui::EndDisabled();

    ImGui::Spacing();
    EditorUi::section_heading("TOOLBAR");
    EditorUi::toolbar_button("gallery_move", EditorIcon::Move, "Move", true);
    ImGui::SameLine();
    EditorUi::toolbar_button("gallery_rotate", EditorIcon::Rotate, "Rotate");
    ImGui::SameLine();
    EditorUi::toolbar_button("gallery_scale", EditorIcon::Scale, "Scale");
    ImGui::SameLine();
    EditorUi::toolbar_button("gallery_locked", EditorIcon::Lock, "Unavailable", false, false);

    ImGui::Spacing();
    EditorUi::section_heading("SELECTION");
    if (ImGui::BeginTable(
            "GalleryTable", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        constexpr const char* rows[][3] = {
            {"Player", "Sprite", "Ready"},
            {"Main Camera", "Camera", "Ready"},
            {"Ground", "Tilemap", "Modified"}
        };
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            for (int column = 0; column < 3; ++column) {
                ImGui::TableSetColumnIndex(column);
                ImGui::TextUnformatted(row[column]);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    EditorUi::section_heading("EMPTY STATE");
    ImGui::BeginChild("GalleryEmptyState", ImVec2(0.0f, 120.0f), true);
    EditorUi::empty_state(
        EditorIcon::Sprite,
        "Nothing selected",
        "Choose an entity or asset to inspect its properties"
    );
    ImGui::EndChild();

    ImGui::End();
}

} // namespace Engine
