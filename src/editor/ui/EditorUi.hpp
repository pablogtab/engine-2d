#pragma once

#include "../tools/EditorIcons.hpp"

#include <imgui.h>

namespace Engine::EditorUi {

enum class BadgeTone {
    Neutral,
    Accent,
    Success,
    Warning,
    Error
};

enum class ButtonTone {
    Neutral,
    Primary,
    Success,
    Danger
};

void section_heading(const char* label, const char* trailing = nullptr);
void badge(const char* label, BadgeTone tone = BadgeTone::Neutral);
void empty_state(EditorIcon icon, const char* title, const char* detail = nullptr);

bool toolbar_button(
    const char* id,
    EditorIcon icon,
    const char* tooltip,
    bool active = false,
    bool enabled = true
);

bool action_button(
    const char* id,
    EditorIcon icon,
    const char* label,
    ButtonTone tone = ButtonTone::Neutral,
    bool enabled = true,
    ImVec2 size = ImVec2(0.0f, 0.0f)
);

bool search_field(
    const char* id,
    const char* hint,
    char* buffer,
    std::size_t buffer_size,
    float width = -1.0f
);

} // namespace Engine::EditorUi
