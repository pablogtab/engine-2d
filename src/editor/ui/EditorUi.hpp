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

// Shared presentation for blocking editor workflows. Call set_next_modal_width()
// before BeginPopupModal(), then compose the body with the helpers below.
void set_next_modal_width(float width = 520.0f);
void modal_status(const char* label, BadgeTone tone);
void modal_lead(const char* message, const char* detail = nullptr);
void modal_resource(const char* label, const char* value, const char* detail = nullptr);
void modal_callout(const char* message, BadgeTone tone = BadgeTone::Neutral);
void modal_footer(float actions_width);

} // namespace Engine::EditorUi
