#pragma once

#include "../tools/EditorIcons.hpp"
#include <imgui.h>

namespace Engine::InspectorWidgets {

struct PropertyInteraction {
    ImGuiID id{0};
    bool activated{false};
    bool deactivated_after_edit{false};
    bool cancelled{false};
};

struct ComponentHeaderResult {
    bool open{false};
    bool remove_requested{false};
};

void begin_property_interaction(const char* label);
void capture_property_interaction();
// Called by the platform event route before the ImGui frame begins. This
// preserves Escape cancellation even when the widget consumes the key itself.
void request_cancel_active_property();
[[nodiscard]] const PropertyInteraction& last_property_interaction();

// Unity-style component header. Removable components expose a compact options
// menu in their own header instead of relying on a global removal picker.
[[nodiscard]] ComponentHeaderResult component_header(
    const char* label,
    const char* id,
    bool removable = true,
    bool default_open = true,
    EditorIcon icon = EditorIcon::Component
);

void begin_property_row(const char* label, bool reserve_reset = false);
bool property_float(const char* label, float* value, float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f");
bool property_axis_float(const char* label, const char* axis, float* value, float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool reserve_reset = false);
bool property_int(const char* label, int* value, float speed, int min = 0, int max = 0, const char* format = "%d");
bool property_bool(const char* label, bool* value);
bool property_vec2(const char* label, float values[2], float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool reserve_reset = false);
bool property_vec3(const char* label, float values[3], float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool reserve_reset = false);
bool property_reset_button(const char* id);

} // namespace Engine::InspectorWidgets
