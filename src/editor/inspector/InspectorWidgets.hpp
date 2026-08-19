#pragma once

#include "../tools/EditorIcons.hpp"
#include "InspectorPropertyPolicies.hpp"
#include <imgui.h>
#include <string_view>

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

// Resolves presentation through the editor-only component registry.
[[nodiscard]] ComponentHeaderResult registered_component_header(
    std::string_view component_key,
    bool removable = true,
    bool default_open = true
);

void begin_property_row(const char* label, bool reserve_reset = false);
bool property_float(const char* label, float* value, float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool mixed = false);
bool property_axis_float(const char* label, const char* axis, float* value, float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool reserve_reset = false, bool mixed = false);
bool property_int(const char* label, int* value, float speed, int min = 0, int max = 0, const char* format = "%d", bool mixed = false);
bool property_bool(const char* label, bool* value, bool mixed = false);
bool property_vec2(
    const char* label,
    float values[2],
    float speed,
    float min = 0.0f,
    float max = 0.0f,
    const char* format = "%.3f",
    bool reserve_reset = false,
    bool mixed_x = false,
    bool mixed_y = false,
    bool* changed_x = nullptr,
    bool* changed_y = nullptr
);
bool property_vec3(const char* label, float values[3], float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool reserve_reset = false);
bool property_reset_button(const char* id);

inline bool property_float(
    const char* label,
    float* value,
    const InspectorPropertyPolicies::FloatPolicy& policy,
    bool mixed = false
) {
    const bool changed = property_float(
        label, value, policy.speed, policy.minimum, policy.maximum, policy.format, mixed
    );
    if (changed) *value = policy.sanitize(*value);
    return changed;
}

inline bool property_axis_float(
    const char* label,
    const char* axis,
    float* value,
    const InspectorPropertyPolicies::FloatPolicy& policy,
    bool reserve_reset = false,
    bool mixed = false
) {
    const bool changed = property_axis_float(
        label, axis, value, policy.speed, policy.minimum, policy.maximum, policy.format, reserve_reset, mixed
    );
    if (changed) *value = policy.sanitize(*value);
    return changed;
}

inline bool property_int(
    const char* label,
    int* value,
    const InspectorPropertyPolicies::IntPolicy& policy,
    bool mixed = false
) {
    const bool changed = property_int(
        label, value, policy.speed, policy.minimum, policy.maximum, policy.format, mixed
    );
    if (changed) *value = policy.sanitize(*value);
    return changed;
}

inline bool property_vec2(
    const char* label,
    float values[2],
    const InspectorPropertyPolicies::FloatPolicy& policy,
    bool reserve_reset = false,
    bool mixed_x = false,
    bool mixed_y = false,
    bool* changed_x = nullptr,
    bool* changed_y = nullptr
) {
    const bool changed = property_vec2(
        label, values, policy.speed, policy.minimum, policy.maximum, policy.format,
        reserve_reset, mixed_x, mixed_y, changed_x, changed_y
    );
    if (changed && (changed_x == nullptr || *changed_x)) values[0] = policy.sanitize(values[0]);
    if (changed && (changed_y == nullptr || *changed_y)) values[1] = policy.sanitize(values[1]);
    return changed;
}

inline bool property_vec3(
    const char* label,
    float values[3],
    const InspectorPropertyPolicies::FloatPolicy& policy,
    bool reserve_reset = false
) {
    const bool changed = property_vec3(
        label, values, policy.speed, policy.minimum, policy.maximum, policy.format, reserve_reset
    );
    if (changed) {
        values[0] = policy.sanitize(values[0]);
        values[1] = policy.sanitize(values[1]);
        values[2] = policy.sanitize(values[2]);
    }
    return changed;
}

} // namespace Engine::InspectorWidgets
