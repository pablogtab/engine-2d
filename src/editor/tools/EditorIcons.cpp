#include "EditorIcons.hpp"

#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

ImVec2 point(ImVec2 center, float half_size, float x, float y) {
    return ImVec2(center.x + x * half_size, center.y + y * half_size);
}

} // namespace

void draw_editor_icon(ImDrawList* draw_list, EditorIcon icon, ImVec2 center, float size, ImU32 color) {
    const float half = size * 0.5f;
    const float stroke = std::max(1.0f, size * 0.105f);
    const auto p = [center, half](float x, float y) { return point(center, half, x, y); };
    const auto line = [draw_list, color, stroke](ImVec2 from, ImVec2 to, float weight = 1.0f) {
        draw_list->AddLine(from, to, color, stroke * weight);
    };

    switch (icon) {
        case EditorIcon::Move:
            line(p(-0.72f, 0.0f), p(0.72f, 0.0f));
            line(p(0.0f, -0.72f), p(0.0f, 0.72f));
            draw_list->AddTriangleFilled(p(1.0f, 0.0f), p(0.58f, -0.27f), p(0.58f, 0.27f), color);
            draw_list->AddTriangleFilled(p(-1.0f, 0.0f), p(-0.58f, -0.27f), p(-0.58f, 0.27f), color);
            draw_list->AddTriangleFilled(p(0.0f, -1.0f), p(-0.27f, -0.58f), p(0.27f, -0.58f), color);
            draw_list->AddTriangleFilled(p(0.0f, 1.0f), p(-0.27f, 0.58f), p(0.27f, 0.58f), color);
            break;
        case EditorIcon::Rotate:
            draw_list->PathArcTo(center, half * 0.78f, -2.65f, 2.05f, 18);
            draw_list->PathStroke(color, 0, stroke);
            draw_list->AddTriangleFilled(p(-0.80f, -0.42f), p(-0.29f, -0.47f), p(-0.58f, -0.04f), color);
            break;
        case EditorIcon::Scale:
            line(p(-0.58f, 0.58f), p(0.58f, -0.58f));
            draw_list->AddRectFilled(p(-0.92f, 0.42f), p(-0.42f, 0.92f), color);
            draw_list->AddRectFilled(p(0.42f, -0.92f), p(0.92f, -0.42f), color);
            break;
        case EditorIcon::World:
            draw_list->AddCircle(center, half * 0.86f, color, 18, stroke);
            draw_list->AddEllipse(center, ImVec2(half * 0.38f, half * 0.86f), color, 0.0f, 18, stroke * 0.8f);
            line(p(-0.78f, 0.0f), p(0.78f, 0.0f), 0.8f);
            break;
        case EditorIcon::Local:
            line(p(-0.72f, 0.65f), p(0.70f, 0.65f));
            line(p(-0.72f, 0.65f), p(-0.72f, -0.72f));
            draw_list->AddTriangleFilled(p(0.98f, 0.65f), p(0.55f, 0.39f), p(0.55f, 0.91f), color);
            draw_list->AddTriangleFilled(p(-0.72f, -1.0f), p(-0.98f, -0.56f), p(-0.46f, -0.56f), color);
            break;
        case EditorIcon::Grid:
            for (float offset : {-0.48f, 0.0f, 0.48f}) {
                line(p(-0.88f, offset), p(0.88f, offset), 0.76f);
                line(p(offset, -0.88f), p(offset, 0.88f), 0.76f);
            }
            break;
        case EditorIcon::Snap:
            draw_list->PathArcTo(p(-0.15f, -0.02f), half * 0.62f, 1.57f, 4.72f, 14);
            draw_list->PathStroke(color, 0, stroke * 1.25f);
            line(p(-0.15f, 0.60f), p(0.80f, 0.60f), 1.25f);
            line(p(0.80f, 0.60f), p(0.80f, 0.18f), 1.25f);
            break;
        case EditorIcon::Settings:
            for (int row = -1; row <= 1; ++row) {
                const float y = static_cast<float>(row) * 0.58f;
                const float knob = row == -1 ? 0.35f : (row == 0 ? -0.36f : 0.15f);
                line(p(-0.88f, y), p(0.88f, y), 0.78f);
                draw_list->AddCircleFilled(p(knob, y), half * 0.18f, color, 10);
            }
            break;
    }
}

bool editor_icon_button(const char* id, EditorIcon icon, const char* tooltip, bool active, ImVec2 hit_size) {
    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("##button", hit_size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (hovered || active) {
        const ImU32 background = active ? IM_COL32(77, 82, 113, 210) : IM_COL32(73, 80, 101, 145);
        draw_list->AddRectFilled(minimum, maximum, background);
    }
    if (active) {
        draw_list->AddLine(
            ImVec2(minimum.x + 2.0f, maximum.y - 1.0f),
            ImVec2(maximum.x - 2.0f, maximum.y - 1.0f),
            IM_COL32(139, 135, 246, 255)
        );
    }

    const ImU32 icon_color = active
        ? IM_COL32(229, 231, 241, 255)
        : (hovered ? IM_COL32(205, 210, 224, 255) : IM_COL32(151, 159, 178, 245));
    draw_editor_icon(
        draw_list,
        icon,
        ImVec2((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f),
        11.0f,
        icon_color
    );

    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return pressed;
}

} // namespace Engine
