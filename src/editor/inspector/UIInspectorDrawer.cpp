#include "UIInspectorDrawer.hpp"

#include "AssetDropTarget.hpp"
#include "InspectorPropertyPolicies.hpp"
#include "InspectorWidgets.hpp"
#include "UIAnchorPresets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/UIComponents.hpp"
#include "../../renderer/material/Material.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../../renderer/material/SDLGPUMaterialBackend.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include "../../ui/CanvasLayout.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_set>

namespace Engine {
namespace {

template<typename Component>
void track(
    EntityManager& em,
    EntityID entity,
    const Component& before,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context,
    bool force_commit = false
) {
    track_component_edit<Component>(
        em, entity, before, property, description, context,
        std::equal_to<Component>{}, {}, force_commit
    );
}

bool authored_transform_equal(
    const TransformComponent& lhs,
    const TransformComponent& rhs
) {
    return lhs.position == rhs.position &&
        lhs.scale == rhs.scale &&
        lhs.rotation == rhs.rotation &&
        lhs.pivot == rhs.pivot &&
        lhs.skew == rhs.skew &&
        lhs.visible == rhs.visible;
}

void track_ui_rotation(
    EntityManager& em,
    EntityID entity,
    const TransformComponent& before,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context,
    bool force_commit = false
) {
    track_component_edit<TransformComponent>(
        em, entity, before, property, description, context,
        authored_transform_equal,
        [](EntityManager& manager, EntityID) { TransformSystem::update(manager); },
        force_commit
    );
}

template<typename Component>
bool begin_component(
    EntityManager& em,
    EntityID entity,
    const char* key,
    const char* remove_description,
    const ComponentTransactionContext& context
) {
    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, key);
    const auto header = InspectorWidgets::registered_component_header(
        key, true, true, has_override
    );
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, key, nullptr
        ));
        return false;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, key, nullptr
        ));
        return false;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<Component>>(
            &em, entity, em.get_component<Component>(entity), std::nullopt, remove_description
        ));
        return false;
    }
    return header.open;
}

template<typename Component>
void draw_color(
    EntityManager& em,
    EntityID entity,
    Component& component,
    SDL_Color& color,
    const char* label,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context
) {
    const Component before = component;
    float values[4] = {
        color.r / 255.0f, color.g / 255.0f,
        color.b / 255.0f, color.a / 255.0f
    };
    InspectorWidgets::begin_property_interaction(property);
    const bool changed = ImGui::ColorEdit4(label, values);
    InspectorWidgets::capture_property_interaction();
    if (changed) {
        color = {
            static_cast<std::uint8_t>(values[0] * 255.0f),
            static_cast<std::uint8_t>(values[1] * 255.0f),
            static_cast<std::uint8_t>(values[2] * 255.0f),
            static_cast<std::uint8_t>(values[3] * 255.0f)
        };
    }
    track(em, entity, before, property, description, context);
}

bool draw_asset_handle(AssetHandle& handle, AssetType type, const char* label, const char* id) {
    InspectorWidgets::begin_property_row(label);
    AssetDatabase& database = AssetDatabase::get();
    const AssetRecord* selected = handle.is_valid() ? database.find(handle) : nullptr;
    const char* preview = selected ? selected->source_path.c_str() : "None";
    bool changed = false;
    if (ImGui::BeginCombo(id, preview)) {
        if (ImGui::Selectable("None", !handle.is_valid())) {
            handle = {};
            changed = true;
        }
        for (const AssetRecord& record : database.records()) {
            if (record.type != type || record.status != AssetStatus::Ready) continue;
            const bool is_selected = record.handle == handle;
            if (ImGui::Selectable(record.source_path.c_str(), is_selected)) {
                handle = record.handle;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (const auto dropped = accept_asset_drop_target(database, type)) {
        if (*dropped != handle) {
            handle = *dropped;
            changed = true;
        }
    }
    return changed;
}

bool draw_material_handle(
    EntityManager& em,
    AssetHandle& handle,
    std::shared_ptr<const Material>& material,
    SDL_Renderer* renderer,
    const char* id,
    const MaterialInspectorActions& actions
) {
    std::size_t reference_count = 0;
    if (handle.is_valid()) {
        em.for_each<SpriteComponent>([&](EntityID, const SpriteComponent& sprite) {
            if (sprite.material_handle == handle) ++reference_count;
        });
        em.for_each<UITextComponent>([&](EntityID, const UITextComponent& text) {
            if (text.material_handle == handle) ++reference_count;
        });
        em.for_each<UIImageComponent>([&](EntityID, const UIImageComponent& image) {
            if (image.material_handle == handle) ++reference_count;
        });
    }
    return draw_material_asset_field(
        "Material", id, handle, material, renderer, actions, reference_count
    );
}

std::optional<UIRect> resolve_parent_layout_rect(EntityManager& em, EntityID entity) {
    std::unordered_set<EntityID> visited;
    EntityID current = entity;
    EntityID canvas_entity = 0;
    while (current != 0 && em.is_alive(current) && visited.insert(current).second) {
        if (em.has_component<CanvasComponent>(current)) {
            canvas_entity = current;
            break;
        }
        if (!em.has_component<HierarchyComponent>(current)) break;
        current = em.get_component<HierarchyComponent>(current).parent;
    }
    if (canvas_entity == 0 || canvas_entity == entity) return std::nullopt;

    const auto& canvas = em.get_component<CanvasComponent>(canvas_entity);
    CanvasViewport viewport;
    viewport.logical_rect = {{0.0f, 0.0f}, canvas.reference_size};
    viewport.physical_rect = viewport.logical_rect;
    viewport.safe_rect = viewport.logical_rect;
    viewport.scale = {1.0f, 1.0f};
    for (const ResolvedUILayout& resolved : resolve_canvas_hierarchy(
            em, canvas_entity, viewport, &AssetDatabase::get()
        )) {
        if (resolved.entity == entity) return resolved.parent_logical_rect;
    }
    return std::nullopt;
}

std::optional<UIRect> resolve_entity_layout_rect(EntityManager& em, EntityID entity) {
    std::unordered_set<EntityID> visited;
    EntityID current = entity;
    EntityID canvas_entity = 0;
    while (current != 0 && em.is_alive(current) && visited.insert(current).second) {
        if (em.has_component<CanvasComponent>(current)) {
            canvas_entity = current;
            break;
        }
        if (!em.has_component<HierarchyComponent>(current)) break;
        current = em.get_component<HierarchyComponent>(current).parent;
    }
    if (canvas_entity == 0 || canvas_entity == entity) return std::nullopt;

    const auto& canvas = em.get_component<CanvasComponent>(canvas_entity);
    CanvasViewport viewport;
    viewport.logical_rect = {{0.0f, 0.0f}, canvas.reference_size};
    viewport.physical_rect = viewport.logical_rect;
    viewport.safe_rect = viewport.logical_rect;
    viewport.scale = {1.0f, 1.0f};
    for (const ResolvedUILayout& resolved : resolve_canvas_hierarchy(
            em, canvas_entity, viewport, &AssetDatabase::get()
        )) {
        if (resolved.entity == entity) return resolved.logical_rect;
    }
    return std::nullopt;
}

struct LengthEditResult {
    bool changed{false};
    bool unit_changed{false};
};

LengthEditResult draw_ui_length(
    const char* label,
    UILength& length,
    float reference_size
) {
    InspectorWidgets::begin_property_row(label);
    InspectorWidgets::begin_property_interaction(label);
    ImGui::PushID(label);
    const float unit_width = 58.0f;
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - unit_width - spacing, 40.0f));
    const float limit = length.unit == UILengthUnit::Percent ? 1000.0f : 100000.0f;
    bool changed = ImGui::DragFloat("##value", &length.value, 1.0f, -limit, limit, "%.1f");
    InspectorWidgets::capture_property_interaction();
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(unit_width);
    int unit = static_cast<int>(length.unit);
    const char* const units[] = {"px", "%"};
    const bool unit_changed = ImGui::Combo("##unit", &unit, units, 2);
    InspectorWidgets::capture_property_interaction();
    if (unit_changed) {
        const UILengthUnit next = static_cast<UILengthUnit>(unit);
        if (reference_size > 0.0001f) {
            length.value = next == UILengthUnit::Percent
                ? length.value / reference_size * 100.0f
                : length.value * reference_size * 0.01f;
        }
        length.unit = next;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Percent values resolve against the current Rect Transform size.");
    }
    ImGui::PopID();
    return {changed, unit_changed};
}

void draw_anchor_preview(
    ImDrawList* draw_list,
    const ImVec2& minimum,
    const ImVec2& maximum,
    const glm::vec2& anchor_min,
    const glm::vec2& anchor_max,
    bool selected
) {
    const ImU32 border = selected
        ? IM_COL32(93, 203, 255, 255)
        : IM_COL32(124, 132, 156, 220);
    const ImU32 anchor = IM_COL32(255, 188, 74, 255);
    draw_list->AddRectFilled(minimum, maximum, IM_COL32(25, 29, 40, 255), 2.0f);
    draw_list->AddRect(minimum, maximum, border, 2.0f, 0, selected ? 2.0f : 1.0f);

    const ImVec2 inner_min{minimum.x + 7.0f, minimum.y + 7.0f};
    const ImVec2 inner_max{maximum.x - 7.0f, maximum.y - 7.0f};
    draw_list->AddRect(inner_min, inner_max, IM_COL32(90, 98, 120, 190));
    const auto map_anchor = [&](const glm::vec2& value) {
        return ImVec2(
            inner_min.x + (inner_max.x - inner_min.x) * value.x,
            inner_min.y + (inner_max.y - inner_min.y) * value.y
        );
    };
    ImVec2 point_min = map_anchor(anchor_min);
    ImVec2 point_max = map_anchor(anchor_max);
    if (point_min.x > point_max.x) std::swap(point_min.x, point_max.x);
    if (point_min.y > point_max.y) std::swap(point_min.y, point_max.y);
    if (std::abs(point_min.x - point_max.x) <= 0.5f &&
        std::abs(point_min.y - point_max.y) <= 0.5f) {
        draw_list->AddRectFilled(
            {point_min.x - 3.0f, point_min.y - 3.0f},
            {point_min.x + 3.0f, point_min.y + 3.0f},
            anchor,
            1.0f
        );
    } else {
        draw_list->AddRectFilled(point_min, point_max, IM_COL32(255, 188, 74, 45));
        draw_list->AddRect(point_min, point_max, anchor, 0.0f, 0, 1.5f);
    }
}

bool draw_anchor_preset_picker(
    UILayoutComponent& layout,
    const std::optional<UIRect>& parent_rect
) {
    const auto current = classify_ui_anchor_preset(layout);
    const char* current_name = "Custom";
    if (current) {
        for (const auto& definition : ui_anchor_preset_definitions()) {
            if (definition.preset == *current) current_name = definition.name;
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Anchors");
    ImGui::SameLine(104.0f);
    ImGui::PushID("AnchorPresetPicker");
    const ImVec2 preview_size{52.0f, 52.0f};
    const bool clicked = ImGui::InvisibleButton("##preview", preview_size);
    draw_anchor_preview(
        ImGui::GetWindowDrawList(),
        ImGui::GetItemRectMin(),
        ImGui::GetItemRectMax(),
        layout.anchor_min,
        layout.anchor_max,
        false
    );
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose an anchor preset");
    if (clicked && parent_rect) ImGui::OpenPopup("AnchorPresets");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", current_name);

    bool changed = false;
    if (ImGui::BeginPopup("AnchorPresets")) {
        ImGui::TextDisabled("Anchor presets");
        ImGui::Separator();
        const auto& presets = ui_anchor_preset_definitions();
        for (std::size_t index = 0; index < presets.size(); ++index) {
            const auto& preset = presets[index];
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 button_size{42.0f, 42.0f};
            const bool chosen = ImGui::InvisibleButton("##preset", button_size);
            draw_anchor_preview(
                ImGui::GetWindowDrawList(),
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax(),
                preset.anchor_min,
                preset.anchor_max,
                current && preset.preset == *current
            );
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\nAlt/Option: also snap position and size\nShift: also set pivot",
                    preset.name
                );
            }
            if (chosen && parent_rect) {
                const auto& io = ImGui::GetIO();
                layout = apply_ui_anchor_preset(
                    layout,
                    *parent_rect,
                    preset,
                    {io.KeyAlt, io.KeyShift}
                );
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            if (index % 4 != 3) ImGui::SameLine();
        }
        ImGui::EndPopup();
    }
    if (!parent_rect) {
        ImGui::TextDisabled("Requires a Canvas parent.");
    } else {
        ImGui::TextDisabled("Alt/Option: snap   Shift: set pivot");
    }
    ImGui::PopID();
    return changed;
}

} // namespace

void UIInspectorDrawer::draw_canvas(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<CanvasComponent>(entity) ||
        !begin_component<CanvasComponent>(em, entity, "canvas", "Remove Canvas", context)) return;

    ImGui::PushID("CanvasComponentDrawer");
    auto& canvas = em.get_component<CanvasComponent>(entity);
    namespace Policies = InspectorPropertyPolicies;

    const CanvasComponent reference_before = canvas;
    float reference[2] = {canvas.reference_size.x, canvas.reference_size.y};
    if (InspectorWidgets::property_vec2("Reference Size", reference, Policies::UI::ReferenceSize)) {
        canvas.reference_size = {reference[0], reference[1]};
    }
    track(em, entity, reference_before, "Canvas.ReferenceSize", "Edit Canvas Reference Size", context);

    const char* const policy_labels[] = {"Fit", "Fill", "Stretch", "Responsive"};
    int policy = static_cast<int>(canvas.scaling_policy);
    const CanvasComponent policy_before = canvas;
    if (InspectorWidgets::property_enum("Scaling", &policy, policy_labels, 4)) {
        canvas.scaling_policy = static_cast<PresentationMode>(policy);
    }
    track(em, entity, policy_before, "Canvas.Scaling", "Change Canvas Scaling", context,
          policy_before.scaling_policy != canvas.scaling_policy);

    const CanvasComponent order_before = canvas;
    int sorting_order = canvas.sorting_order;
    if (InspectorWidgets::property_int("Sorting Order", &sorting_order, Policies::UI::SortingOrder)) {
        canvas.sorting_order = static_cast<std::int16_t>(sorting_order);
    }
    track(em, entity, order_before, "Canvas.SortingOrder", "Edit Canvas Sorting Order", context);

    const CanvasComponent safe_before = canvas;
    const bool safe_changed = InspectorWidgets::property_bool("Use Safe Area", &canvas.use_safe_area);
    track(em, entity, safe_before, "Canvas.SafeArea", "Toggle Canvas Safe Area", context, safe_changed);

    const CanvasComponent enabled_before = canvas;
    const bool enabled_changed = InspectorWidgets::property_bool("Enabled", &canvas.enabled);
    track(em, entity, enabled_before, "Canvas.Enabled", "Toggle Canvas", context, enabled_changed);
    ImGui::PopID();
}

void UIInspectorDrawer::draw_layout(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<UILayoutComponent>(entity) ||
        !begin_component<UILayoutComponent>(em, entity, "ui_layout", "Remove UI Layout", context)) return;

    ImGui::PushID("UILayoutComponentDrawer");
    auto& layout = em.get_component<UILayoutComponent>(entity);
    namespace Policies = InspectorPropertyPolicies;
    const auto draw_vec2 = [&](
        const char* label,
        glm::vec2& value,
        const char* property,
        const char* description,
        const Policies::FloatPolicy& policy
    ) {
        const UILayoutComponent before = layout;
        float values[2] = {value.x, value.y};
        if (InspectorWidgets::property_vec2(label, values, policy)) value = {values[0], values[1]};
        track(em, entity, before, property, description, context);
    };

    const UILayoutComponent preset_before = layout;
    const bool preset_changed = draw_anchor_preset_picker(
        layout, resolve_parent_layout_rect(em, entity)
    );
    track(
        em, entity, preset_before, "UILayout.AnchorPreset", "Change UI Anchor Preset",
        context, preset_changed
    );

    draw_vec2("Pivot", layout.pivot, "UILayout.Pivot", "Edit UI Pivot", Policies::UI::Normalized);
    draw_vec2("Position", layout.anchored_position, "UILayout.Position", "Edit UI Anchored Position", Policies::UI::Coordinate);
    const bool fixed_size = layout.anchor_min == layout.anchor_max;
    draw_vec2(
        fixed_size ? "Size" : "Size Delta",
        layout.size_delta,
        "UILayout.SizeDelta",
        "Edit UI Size Delta",
        Policies::UI::Coordinate
    );
    if (em.has_component<TransformComponent>(entity)) {
        auto& transform = em.get_component<TransformComponent>(entity);
        const TransformComponent before = transform;
        InspectorWidgets::property_axis_float(
            "Rotation", "Z", &transform.rotation, Policies::Transform::Rotation, true
        );
        track_ui_rotation(
            em, entity, before, "UILayout.Rotation", "Edit UI Rotation", context
        );
        if (InspectorWidgets::property_reset_button("R##ui_rotation")) {
            const TransformComponent reset_before = transform;
            transform.rotation = 0.0f;
            track_ui_rotation(
                em, entity, reset_before, "UILayout.ResetRotation",
                "Reset UI Rotation", context, true
            );
        }
    }
    if (ImGui::TreeNodeEx("Advanced Anchors", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        draw_vec2("Anchor Min", layout.anchor_min, "UILayout.AnchorMin", "Edit UI Minimum Anchor", Policies::UI::Normalized);
        draw_vec2("Anchor Max", layout.anchor_max, "UILayout.AnchorMax", "Edit UI Maximum Anchor", Policies::UI::Normalized);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void UIInspectorDrawer::draw_text(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context,
    SDL_Renderer* renderer,
    const MaterialInspectorActions& material_actions
) {
    if (!em.has_component<UITextComponent>(entity) ||
        !begin_component<UITextComponent>(em, entity, "ui_text", "Remove UI Text", context)) return;

    ImGui::PushID("UITextComponentDrawer");
    auto& text = em.get_component<UITextComponent>(entity);
    namespace Policies = InspectorPropertyPolicies;

    const UITextComponent content_before = text;
    std::array<char, 1024> buffer{};
    std::strncpy(buffer.data(), text.text.c_str(), buffer.size() - 1);
    if (InspectorWidgets::property_text("Text", buffer.data(), buffer.size())) text.text = buffer.data();
    track(em, entity, content_before, "UIText.Content", "Edit UI Text", context);

    const UITextComponent font_before = text;
    const bool font_changed = draw_asset_handle(
        text.font_handle, AssetType::Font, "Font", "##ui_text_font"
    );
    track(em, entity, font_before, "UIText.Font", "Assign UI Text Font", context, font_changed);

    const UITextComponent material_before = text;
    const bool material_changed = draw_material_handle(
        em, text.material_handle, text.material, renderer, "##ui_text_material", material_actions
    );
    track(
        em, entity, material_before, "UIText.Material", "Assign UI Text Material",
        context, material_changed
    );

    const UITextComponent size_before = text;
    InspectorWidgets::property_float("Font Size", &text.font_size, Policies::UI::FontSize);
    track(em, entity, size_before, "UIText.FontSize", "Edit UI Font Size", context);

    draw_color(em, entity, text, text.color, "Color", "UIText.Color", "Edit UI Text Color", context);

    const char* const horizontal_labels[] = {"Left", "Center", "Right"};
    int horizontal = static_cast<int>(text.horizontal_alignment);
    const UITextComponent horizontal_before = text;
    if (InspectorWidgets::property_enum("Horizontal", &horizontal, horizontal_labels, 3)) {
        text.horizontal_alignment = static_cast<UIHorizontalAlignment>(horizontal);
    }
    track(em, entity, horizontal_before, "UIText.Horizontal", "Change UI Text Alignment", context,
          horizontal_before.horizontal_alignment != text.horizontal_alignment);

    const char* const vertical_labels[] = {"Top", "Center", "Bottom"};
    int vertical = static_cast<int>(text.vertical_alignment);
    const UITextComponent vertical_before = text;
    if (InspectorWidgets::property_enum("Vertical", &vertical, vertical_labels, 3)) {
        text.vertical_alignment = static_cast<UIVerticalAlignment>(vertical);
    }
    track(em, entity, vertical_before, "UIText.Vertical", "Change UI Text Alignment", context,
          vertical_before.vertical_alignment != text.vertical_alignment);

    const glm::vec2 rect_size = resolve_entity_layout_rect(em, entity)
        .value_or(UIRect{{}, {100.0f, 100.0f}}).size;
    const auto draw_margin = [&](const char* label, UILength& margin, float reference) {
        const UITextComponent before = text;
        const LengthEditResult edit = draw_ui_length(label, margin, reference);
        track(
            em, entity, before, "UIText.Margins", "Edit UI Text Margins",
            context, edit.unit_changed
        );
    };
    ImGui::TextDisabled("Margins (negative values expand the text area)");
    draw_margin("Left", text.margins.left, rect_size.x);
    draw_margin("Top", text.margins.top, rect_size.y);
    draw_margin("Right", text.margins.right, rect_size.x);
    draw_margin("Bottom", text.margins.bottom, rect_size.y);

    const UITextComponent fit_width_before = text;
    const bool fit_width_changed = InspectorWidgets::property_bool(
        "Fit Width", &text.fit_content_width
    );
    track(
        em, entity, fit_width_before, "UIText.FitWidth", "Toggle UI Text Fit Width",
        context, fit_width_changed
    );

    const UITextComponent fit_height_before = text;
    const bool fit_height_changed = InspectorWidgets::property_bool(
        "Fit Height", &text.fit_content_height
    );
    track(
        em, entity, fit_height_before, "UIText.FitHeight", "Toggle UI Text Fit Height",
        context, fit_height_changed
    );
    if (text.fit_content_width || text.fit_content_height) {
        ImGui::TextDisabled("Enabled Fit axes drive the resolved Rect Transform size.");
    }

    const UITextComponent padding_before = text;
    float padding[4] = {
        text.fit_padding.x, text.fit_padding.y,
        text.fit_padding.z, text.fit_padding.w
    };
    bool padding_changed[4]{};
    const char* const padding_labels[4] = {"L", "T", "R", "B"};
    if (InspectorWidgets::property_vec4(
            "Fit Padding",
            padding,
            Policies::UI::Padding,
            false,
            false,
            false,
            false,
            false,
            &padding_changed[0],
            &padding_changed[1],
            &padding_changed[2],
            &padding_changed[3],
            padding_labels
        )) {
        text.fit_padding = {padding[0], padding[1], padding[2], padding[3]};
    }
    track(em, entity, padding_before, "UIText.FitPadding", "Edit UI Text Fit Padding", context);

    const UITextComponent wrap_before = text;
    const bool wrap_changed = InspectorWidgets::property_bool("Wrap", &text.wrap);
    track(em, entity, wrap_before, "UIText.Wrap", "Toggle UI Text Wrapping", context, wrap_changed);

    const UITextComponent enabled_before = text;
    const bool enabled_changed = InspectorWidgets::property_bool("Enabled", &text.enabled);
    track(em, entity, enabled_before, "UIText.Enabled", "Toggle UI Text", context, enabled_changed);
    ImGui::PopID();
}

void UIInspectorDrawer::draw_image(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context,
    SDL_Renderer* renderer,
    const MaterialInspectorActions& material_actions
) {
    if (!em.has_component<UIImageComponent>(entity) ||
        !begin_component<UIImageComponent>(em, entity, "ui_image", "Remove UI Image", context)) return;

    ImGui::PushID("UIImageComponentDrawer");
    auto& image = em.get_component<UIImageComponent>(entity);
    const UIImageComponent texture_before = image;
    const bool texture_changed = draw_asset_handle(
        image.texture_handle, AssetType::Texture, "Texture", "##ui_image_texture"
    );
    track(em, entity, texture_before, "UIImage.Texture", "Assign UI Image Texture", context, texture_changed);

    const UIImageComponent material_before = image;
    const bool material_changed = draw_material_handle(
        em, image.material_handle, image.material, renderer, "##ui_image_material", material_actions
    );
    track(
        em, entity, material_before, "UIImage.Material", "Assign UI Image Material",
        context, material_changed
    );

    draw_color(em, entity, image, image.color, "Color", "UIImage.Color", "Edit UI Image Color", context);

    const char* const scale_labels[] = {"Stretch", "Fit", "Fill"};
    int scale_mode = static_cast<int>(image.scale_mode);
    const UIImageComponent scale_before = image;
    if (InspectorWidgets::property_enum("Scale Mode", &scale_mode, scale_labels, 3)) {
        image.scale_mode = static_cast<UIImageScaleMode>(scale_mode);
    }
    track(em, entity, scale_before, "UIImage.ScaleMode", "Change UI Image Scale", context,
          scale_before.scale_mode != image.scale_mode);

    const UIImageComponent raycast_before = image;
    const bool raycast_changed = InspectorWidgets::property_bool("Raycast Target", &image.raycast_target);
    track(em, entity, raycast_before, "UIImage.Raycast", "Toggle UI Image Raycast", context, raycast_changed);

    const UIImageComponent enabled_before = image;
    const bool enabled_changed = InspectorWidgets::property_bool("Enabled", &image.enabled);
    track(em, entity, enabled_before, "UIImage.Enabled", "Toggle UI Image", context, enabled_changed);
    ImGui::PopID();
}

void UIInspectorDrawer::draw_button(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<UIButtonComponent>(entity) ||
        !begin_component<UIButtonComponent>(em, entity, "ui_button", "Remove UI Button", context)) return;

    ImGui::PushID("UIButtonComponentDrawer");
    auto& button = em.get_component<UIButtonComponent>(entity);
    draw_color(em, entity, button, button.normal_color, "Normal", "UIButton.Normal", "Edit Button Normal Color", context);
    draw_color(em, entity, button, button.hovered_color, "Hovered", "UIButton.Hovered", "Edit Button Hovered Color", context);
    draw_color(em, entity, button, button.pressed_color, "Pressed", "UIButton.Pressed", "Edit Button Pressed Color", context);
    draw_color(em, entity, button, button.disabled_color, "Disabled", "UIButton.Disabled", "Edit Button Disabled Color", context);

    const UIButtonComponent interactable_before = button;
    const bool interactable_changed = InspectorWidgets::property_bool("Interactable", &button.interactable);
    track(em, entity, interactable_before, "UIButton.Interactable", "Toggle Button Interaction", context, interactable_changed);

    const UIButtonComponent enabled_before = button;
    const bool enabled_changed = InspectorWidgets::property_bool("Enabled", &button.enabled);
    track(em, entity, enabled_before, "UIButton.Enabled", "Toggle Button", context, enabled_changed);
    ImGui::PopID();
}

} // namespace Engine
