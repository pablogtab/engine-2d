#include "InspectorWidgets.hpp"
#include "../tools/EditorIcons.hpp"
#include <imgui_internal.h>
#include <string>

namespace Engine::InspectorWidgets {

namespace {

constexpr float PROPERTY_LABEL_WIDTH = 104.0f;
constexpr float PROPERTY_GAP = 6.0f;
constexpr float RESET_BUTTON_WIDTH = 28.0f;
PropertyInteraction g_property_interaction;
ImGuiID g_active_property_id = 0;
ImGuiID g_cancel_requested_for = 0;

} // namespace

void begin_property_interaction(const char* label) {
    g_property_interaction = {ImGui::GetID(label), false, false, false};
}

void request_cancel_active_property() {
    g_cancel_requested_for = g_active_property_id;
}

void capture_property_interaction() {
    const bool activated = ImGui::IsItemActivated();
    g_property_interaction.activated |= activated;
    if (activated) {
        g_active_property_id = g_property_interaction.id;
    }

    g_property_interaction.deactivated_after_edit |= ImGui::IsItemDeactivatedAfterEdit();
    const bool deactivated = ImGui::IsItemDeactivated();
    const bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const bool cancelled = g_active_property_id == g_property_interaction.id &&
        (escape_pressed || g_cancel_requested_for == g_property_interaction.id);
    g_property_interaction.cancelled |= cancelled;

    if (cancelled) {
        // InputText and temporary scalar editors can clear their active item
        // before the caller observes IsItemDeactivated(). Keep our own
        // cross-frame property identity so Escape still cancels the matching
        // transaction, then release all keyboard/mouse capture immediately.
        ImGui::ClearActiveID();
        g_active_property_id = 0;
        g_cancel_requested_for = 0;
    } else if (deactivated && g_active_property_id == g_property_interaction.id) {
        g_active_property_id = 0;
        g_cancel_requested_for = 0;
    }
}

const PropertyInteraction& last_property_interaction() {
    return g_property_interaction;
}

ComponentHeaderResult component_header(
    const char* label,
    const char* id,
    bool removable,
    bool default_open,
    EditorIcon icon
) {
    ImGui::PushID(id);
    const ImVec2 header_position = ImGui::GetCursorScreenPos();
    const ImGuiTreeNodeFlags flags = (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0) |
        (removable ? ImGuiTreeNodeFlags_AllowOverlap : 0);
    const std::string header_label = editor_icon_label(icon, label);
    const bool open = ImGui::CollapsingHeader(header_label.c_str(), flags);
    const ImVec2 content_position = ImGui::GetCursorScreenPos();
    bool remove_requested = false;

    if (removable) {
        const ImGuiStyle& style = ImGui::GetStyle();
        constexpr float button_size = 18.0f;
        const float content_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGui::SetCursorScreenPos(ImVec2(
            content_right - button_size - style.FramePadding.x,
            header_position.y
        ));
        if (editor_icon_button(
            "ComponentOptionsButton",
            EditorIcon::Ellipsis,
            nullptr,
            false,
            ImVec2(button_size, button_size)
        )) {
            ImGui::OpenPopup("ComponentOptions");
        }

        if (ImGui::BeginPopup("ComponentOptions")) {
            const std::string remove_label = editor_icon_label(EditorIcon::Trash, "Remove Component");
            remove_requested = ImGui::MenuItem(remove_label.c_str());
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(content_position);
    }

    ImGui::PopID();
    return {open, remove_requested};
}

void begin_property_row(const char* label, bool reserve_reset) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::SameLine(PROPERTY_LABEL_WIDTH);
    const float reset_space = reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reset_space);
}

bool property_float(const char* label, float* value, float speed, float min, float max, const char* format, bool mixed) {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const bool changed = ImGui::DragFloat("##value", value, speed, min, max, mixed ? "—" : format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_axis_float(const char* label, const char* axis, float* value, float speed, float min, float max, const char* format, bool reserve_reset, bool mixed) {
    begin_property_row(label, reserve_reset);
    begin_property_interaction(label);
    ImGui::PushID(label);
    ImGui::TextDisabled("%s", axis);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    const float reset_space = reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reset_space - 11.0f);
    const bool changed = ImGui::DragFloat("##value", value, speed, min, max, mixed ? "—" : format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_int(const char* label, int* value, float speed, int min, int max, const char* format, bool mixed) {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const bool changed = ImGui::DragInt("##value", value, speed, min, max, mixed ? "—" : format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_bool(const char* label, bool* value, bool mixed) {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    const bool changed = ImGui::Checkbox("##value", value);
    if (mixed) ImGui::PopItemFlag();
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_vec2(
    const char* label,
    float values[2],
    float speed,
    float min,
    float max,
    const char* format,
    bool reserve_reset,
    bool mixed_x,
    bool mixed_y,
    bool* changed_x,
    bool* changed_y
) {
    begin_property_row(label, reserve_reset);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - (reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f) - spacing * 3.0f - 24.0f) * 0.5f;
    ImGui::TextDisabled("X");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    const bool x_changed = ImGui::DragFloat("##x", &values[0], speed, min, max, mixed_x ? "—" : format);
    capture_property_interaction();
    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("Y");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    const bool y_changed = ImGui::DragFloat("##y", &values[1], speed, min, max, mixed_y ? "—" : format);
    capture_property_interaction();
    ImGui::PopID();
    if (changed_x) *changed_x = x_changed;
    if (changed_y) *changed_y = y_changed;
    return x_changed || y_changed;
}

bool property_vec3(const char* label, float values[3], float speed, float min, float max, const char* format, bool reserve_reset) {
    begin_property_row(label, reserve_reset);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - (reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f) - spacing * 5.0f - 36.0f) / 3.0f;
    ImGui::TextDisabled("X");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::DragFloat("##x", &values[0], speed, min, max, format);
    capture_property_interaction();
    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("Y");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat("##y", &values[1], speed, min, max, format);
    capture_property_interaction();
    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("Z");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat("##z", &values[2], speed, min, max, format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_reset_button(const char* id) {
    ImGui::SameLine(0.0f, PROPERTY_GAP);
    return ImGui::Button(id, ImVec2(RESET_BUTTON_WIDTH, 0.0f));
}

} // namespace Engine::InspectorWidgets
