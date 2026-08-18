#pragma once

#include <imgui.h>

namespace Engine {

enum class EditorIcon {
    Move,
    Rotate,
    Scale,
    World,
    Local,
    Grid,
    Snap,
    Settings,
    Camera
};

// DPI-independent editor glyphs drawn from normalized vector primitives.
// Keeping icon geometry here gives every toolbar the same visual language and
// allows the implementation to move to an icon font/atlas without panel churn.
void draw_editor_icon(ImDrawList* draw_list, EditorIcon icon, ImVec2 center, float size, ImU32 color);

bool editor_icon_button(
    const char* id,
    EditorIcon icon,
    const char* tooltip,
    bool active = false,
    ImVec2 hit_size = ImVec2(18.0f, 18.0f)
);

} // namespace Engine
