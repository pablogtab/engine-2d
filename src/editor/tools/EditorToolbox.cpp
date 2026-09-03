#include "EditorToolbox.hpp"
#include "../EditorTheme.hpp"

namespace Engine {

bool EditorToolbox::begin(const char* id, float height) {
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    if (height <= 0.0f) height = metrics.toolbar_height;
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(4.0f * metrics.scale, 3.0f * metrics.scale)
    );
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panel);
    const bool open = ImGui::BeginChild(
        id,
        ImVec2(0.0f, height),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav
    );
    m_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    return open;
}

void EditorToolbox::end() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
}

} // namespace Engine
