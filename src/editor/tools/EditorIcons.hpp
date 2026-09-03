#pragma once

#include <imgui.h>
#include <string>

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
    Camera,
    Ellipsis,
    Trash,
    Sprite,
    Collider,
    Physics,
    Animation,
    Visible,
    Hidden,
    Plus,
    Component,
    Lock,
    Unlock,
    Play,
    Pause,
    Stop,
    Zap,
    ExternalLink,
    Material,
    Clear,
    Copy,
    ArrowUp,
    ArrowLeft,
    History,
    Edit,
    Shield,
    Assistant,
    Save,
    StepForward,
    Folder,
    File,
    Star,
    List,
    Details,
    Refresh,
    Count
};

// Loads the editor-only Lucide font into ImGui's atlas. The font is deliberately
// kept outside runtime assets and standalone packaging.
[[nodiscard]] bool initialize_editor_icons();

// UTF-8 glyph from the pinned Lucide codepoint registry.
[[nodiscard]] const char* editor_icon_glyph(EditorIcon icon);
[[nodiscard]] std::string editor_icon_label(EditorIcon icon, const char* label);

// DPI-independent Lucide glyph rendering shared by editor surfaces.
void draw_editor_icon(ImDrawList* draw_list, EditorIcon icon, ImVec2 center, float size, ImU32 color);

bool editor_icon_button(
    const char* id,
    EditorIcon icon,
    const char* tooltip,
    bool active = false,
    ImVec2 hit_size = ImVec2(18.0f, 18.0f),
    float icon_size = 12.0f
);

} // namespace Engine
