#include "InspectorWidgets.hpp"
#include "EditorComponentRegistry.hpp"
#include "../tools/EditorIcons.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
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
    EditorIcon icon,
    bool has_override
) {
    ImGui::PushID(id);
    const ImVec2 header_position = ImGui::GetCursorScreenPos();
    const ImGuiTreeNodeFlags flags = (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0) |
        (removable ? ImGuiTreeNodeFlags_AllowOverlap : 0);
    const std::string header_label = editor_icon_label(icon, label);
    const bool open = ImGui::CollapsingHeader(header_label.c_str(), flags);
    const ImVec2 content_position = ImGui::GetCursorScreenPos();
    bool remove_requested = false;
    bool revert_override_requested = false;
    bool apply_override_requested = false;

    // Draw subtle vertical gutter indicator bar on the left margin when overridden
    if (has_override) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const float bar_height = ImGui::GetFrameHeight();
        draw_list->AddRectFilled(
            header_position,
            ImVec2(header_position.x + 3.5f, header_position.y + bar_height),
            IM_COL32(95, 185, 255, 255),
            1.5f
        );
    }

    // Right-click context popup directly on header item
    if (ImGui::BeginPopupContextItem("ComponentHeaderContextMenu")) {
        if (has_override) {
            if (ImGui::MenuItem("Revert to Prefab")) {
                revert_override_requested = true;
            }
            if (ImGui::MenuItem("Apply to Prefab Asset")) {
                apply_override_requested = true;
            }
            ImGui::Separator();
        }
        if (removable) {
            const std::string remove_label = editor_icon_label(EditorIcon::Trash, "Remove Component");
            remove_requested = ImGui::MenuItem(remove_label.c_str());
        }
        ImGui::EndPopup();
    }

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
            if (has_override) {
                if (ImGui::MenuItem("Revert to Prefab")) {
                    revert_override_requested = true;
                }
                if (ImGui::MenuItem("Apply to Prefab Asset")) {
                    apply_override_requested = true;
                }
                ImGui::Separator();
            }
            const std::string remove_label = editor_icon_label(EditorIcon::Trash, "Remove Component");
            remove_requested = ImGui::MenuItem(remove_label.c_str());
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(content_position);
    }

    ImGui::PopID();
    return {open, remove_requested, revert_override_requested, apply_override_requested};
}

ComponentHeaderResult registered_component_header(
    std::string_view component_key,
    bool removable,
    bool default_open,
    bool has_override
) {
    const EditorComponentDescriptor* descriptor = default_editor_component_registry().find(component_key);
    if (!descriptor) {
        const std::string fallback_label(component_key);
        const std::string fallback_id = fallback_label + "Header";
        return component_header(
            fallback_label.c_str(), fallback_id.c_str(), removable, default_open, EditorIcon::Component, has_override
        );
    }
    return component_header(
        descriptor->display_name.c_str(),
        descriptor->drawer_key.c_str(),
        removable,
        default_open,
        descriptor->icon,
        has_override
    );
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
    const bool changed = ImGui::DragFloat("##value", value, speed, min, max, mixed ? "---" : format);
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
    const bool changed = ImGui::DragFloat("##value", value, speed, min, max, mixed ? "---" : format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_int(const char* label, int* value, float speed, int min, int max, const char* format, bool mixed) {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const bool changed = ImGui::DragInt("##value", value, speed, min, max, mixed ? "---" : format);
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
    const bool x_changed = ImGui::DragFloat("##x", &values[0], speed, min, max, mixed_x ? "---" : format);
    capture_property_interaction();
    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("Y");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    const bool y_changed = ImGui::DragFloat("##y", &values[1], speed, min, max, mixed_y ? "---" : format);
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

bool property_vec4(
    const char* label,
    float values[4],
    float speed,
    float min,
    float max,
    const char* format,
    bool reserve_reset,
    bool mixed_x,
    bool mixed_y,
    bool mixed_z,
    bool mixed_w,
    bool* changed_x,
    bool* changed_y,
    bool* changed_z,
    bool* changed_w,
    const char* const labels[4]
) {
    begin_property_row(label, reserve_reset);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float reset_space = reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f;
    const float width = (ImGui::GetContentRegionAvail().x - reset_space - spacing * 7.0f - 48.0f) * 0.25f;

    const char* default_labels[4] = {"X", "Y", "Z", "W"};
    const char* const* axis_labels = labels ? labels : default_labels;

    ImGui::TextDisabled("%s", axis_labels[0]);
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    const bool x_changed = ImGui::DragFloat("##x", &values[0], speed, min, max, mixed_x ? "---" : format);
    capture_property_interaction();

    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("%s", axis_labels[1]);
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    const bool y_changed = ImGui::DragFloat("##y", &values[1], speed, min, max, mixed_y ? "---" : format);
    capture_property_interaction();

    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("%s", axis_labels[2]);
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    const bool z_changed = ImGui::DragFloat("##z", &values[2], speed, min, max, mixed_z ? "---" : format);
    capture_property_interaction();

    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("%s", axis_labels[3]);
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    const bool w_changed = ImGui::DragFloat("##w", &values[3], speed, min, max, mixed_w ? "---" : format);
    capture_property_interaction();

    ImGui::PopID();

    if (changed_x) *changed_x = x_changed;
    if (changed_y) *changed_y = y_changed;
    if (changed_z) *changed_z = z_changed;
    if (changed_w) *changed_w = w_changed;

    return x_changed || y_changed || z_changed || w_changed;
}

bool property_reset_button(const char* id) {
    ImGui::SameLine(0.0f, PROPERTY_GAP);
    return ImGui::Button(id, ImVec2(RESET_BUTTON_WIDTH, 0.0f));
}

bool property_enum(const char* label, int* current_index, const char* const items[], int count, bool mixed) {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 11.0f);
    const char* preview = (mixed || *current_index < 0 || *current_index >= count) ? "---" : items[*current_index];
    bool changed = false;
    if (ImGui::BeginCombo("##value", preview)) {
        for (int i = 0; i < count; ++i) {
            const bool is_selected = (!mixed && *current_index == i);
            if (ImGui::Selectable(items[i], is_selected)) {
                *current_index = i;
                changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_entity_picker(
    const char* label,
    EntityID* target_id,
    EntityManager& em,
    bool mixed,
    std::function<bool(EntityID)> filter
) {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 11.0f);

    std::string preview = "None";
    if (mixed) {
        preview = "---";
    } else if (*target_id != 0 && em.is_alive(*target_id)) {
        preview = em.has_component<TagComponent>(*target_id)
            ? em.get_component<TagComponent>(*target_id).tag
            : "Entity #" + std::to_string(*target_id);
    }

    bool changed = false;
    if (ImGui::BeginCombo("##value", preview.c_str())) {
        if (ImGui::Selectable("None", !mixed && *target_id == 0)) {
            *target_id = 0;
            changed = true;
        }
        for (EntityID other : em.get_living_entities()) {
            if (!em.is_alive(other)) continue;
            if (filter && !filter(other)) continue;
            std::string item_name = "Entity #" + std::to_string(other);
            if (em.has_component<TagComponent>(other)) {
                item_name = em.get_component<TagComponent>(other).tag;
            }
            const bool is_selected = (!mixed && *target_id == other);
            if (ImGui::Selectable(item_name.c_str(), is_selected)) {
                *target_id = other;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_text(const char* label, char* buffer, size_t buffer_size, bool mixed) {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 11.0f);
    bool changed = false;
    if (mixed) {
        char temp_buf[256] = "";
        if (ImGui::InputTextWithHint("##value", "---", temp_buf, sizeof(temp_buf))) {
            if (temp_buf[0] != '\0') {
                std::strncpy(buffer, temp_buf, buffer_size - 1);
                buffer[buffer_size - 1] = '\0';
                changed = true;
            }
        }
    } else {
        changed = ImGui::InputText("##value", buffer, buffer_size);
    }
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

} // namespace Engine::InspectorWidgets
