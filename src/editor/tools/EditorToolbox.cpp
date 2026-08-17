#include "EditorToolbox.hpp"
#include <algorithm>
#include <cmath>

namespace Engine {

bool EditorToolbox::begin(const char* id, const ImVec2& size, ImVec2 host_min, ImVec2 host_max) {
    m_size = size;
    m_host_min = host_min;
    m_host_max = host_max;
    constexpr float inset = 6.0f;
    constexpr float ruler_clearance = 18.0f;
    if (m_position.x < 0.0f) m_position = ImVec2(host_min.x + ruler_clearance + inset, host_min.y + ruler_clearance + inset);
    if (m_anchor == Anchor::Top) m_position = ImVec2(host_min.x + ruler_clearance + inset, host_min.y + ruler_clearance + inset);
    if (m_anchor == Anchor::Bottom) m_position = ImVec2(host_min.x + ruler_clearance + inset, host_max.y - size.y - inset);

    ImGui::SetNextWindowPos(m_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.085f, 0.095f, 0.125f, 0.94f));
    const bool open = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    m_hovered = ImGui::IsWindowHovered();
    return open;
}

void EditorToolbox::render_drag_handle(const char* id) {
    ImGui::InvisibleButton(id, ImVec2(5.0f, 18.0f));
    const ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (int row = 0; row < 3; ++row) {
        draw_list->AddCircleFilled(ImVec2(min.x + 2.5f, min.y + 5.0f + row * 4.0f), 0.65f, IM_COL32(125, 133, 153, 210));
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_position.x += delta.x;
        m_position.y += delta.y;
        m_anchor = Anchor::Floating;

        constexpr float snap_distance = 32.0f;
        constexpr float inset = 6.0f;
        constexpr float ruler_clearance = 18.0f;
        ImVec2 preview(m_host_min.x + ruler_clearance + inset, m_host_min.y + ruler_clearance + inset);
        const bool top = std::abs(m_position.y - preview.y) < snap_distance;
        const float bottom_y = m_host_max.y - m_size.y - inset;
        const bool bottom = std::abs(m_position.y - bottom_y) < snap_distance;
        if (bottom) preview.y = bottom_y;
        const bool will_dock = top || bottom;
        if (will_dock) {
            ImDrawList* foreground = ImGui::GetForegroundDrawList();
            foreground->AddRectFilled(preview, ImVec2(preview.x + m_size.x, preview.y + m_size.y), IM_COL32(99, 102, 241, 55));
            foreground->AddRect(preview, ImVec2(preview.x + m_size.x, preview.y + m_size.y), IM_COL32(129, 140, 248, 210), 0.0f, 0, 1.0f);
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        constexpr float snap_distance = 32.0f;
        constexpr float inset = 6.0f;
        constexpr float ruler_clearance = 18.0f;
        const float top_y = m_host_min.y + ruler_clearance + inset;
        const float bottom_y = m_host_max.y - m_size.y - inset;
        if (std::abs(m_position.y - top_y) < snap_distance) m_anchor = Anchor::Top;
        else if (std::abs(m_position.y - bottom_y) < snap_distance) m_anchor = Anchor::Bottom;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag toolbar; release near the viewport top or bottom edge to dock");
}

void EditorToolbox::end() {
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
}

} // namespace Engine
