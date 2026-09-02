#include "TrailRendererInspectorDrawer.hpp"
#include "AssetDropTarget.hpp"
#include "EditorComponentRegistry.hpp"
#include "InspectorPropertyPolicies.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace Engine {
namespace {

void track(
    EntityManager& entities,
    EntityID entity,
    const TrailRenderer2DComponent& before,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context,
    bool force_commit = false
) {
    track_component_edit<TrailRenderer2DComponent>(
        entities, entity, before, property, description, context,
        std::equal_to<TrailRenderer2DComponent>{}, {}, force_commit
    );
}

bool draw_color(const char* label, SDL_Color& color) {
    float values[4]{
        color.r / 255.0f, color.g / 255.0f,
        color.b / 255.0f, color.a / 255.0f
    };
    InspectorWidgets::begin_property_row(label);
    if (!ImGui::ColorEdit4((std::string("##") + label).c_str(), values)) return false;
    color = {
        static_cast<std::uint8_t>(std::clamp(values[0], 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint8_t>(std::clamp(values[1], 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint8_t>(std::clamp(values[2], 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint8_t>(std::clamp(values[3], 0.0f, 1.0f) * 255.0f)
    };
    return true;
}

} // namespace

void TrailRendererInspectorDrawer::draw(
    EntityManager& entities,
    EntityID entity,
    const EditorInspectorContext& context
) {
    if (!entities.has_component<TrailRenderer2DComponent>(entity)) return;
    const bool overridden = PrefabOverrides::is_component_overridden(
        entities, entity, "trail_renderer_2d"
    );
    const auto header = InspectorWidgets::registered_component_header(
        "trail_renderer_2d", true, true, overridden
    );
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &entities, entity, "trail_renderer_2d", nullptr
        ));
        return;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &entities, entity, "trail_renderer_2d", nullptr
        ));
        return;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<TrailRenderer2DComponent>>(
            &entities, entity, entities.get_component<TrailRenderer2DComponent>(entity),
            std::nullopt, "Remove Trail Renderer 2D"
        ));
        return;
    }
    if (!header.open) return;

    ImGui::PushID("TrailRenderer2DComponentDrawer");
    auto& trail = entities.get_component<TrailRenderer2DComponent>(entity);
    const ComponentTransactionContext transaction{context.command_history, context.transactions};
    AssetDatabase& database = AssetDatabase::get();

    const TrailRenderer2DComponent material_before = trail;
    const AssetRecord* selected = trail.material_handle.is_valid()
        ? database.find(trail.material_handle) : nullptr;
    InspectorWidgets::begin_property_row("Material");
    if (ImGui::BeginCombo("##trail_material", selected ? selected->source_path.c_str() : "None")) {
        if (ImGui::Selectable("None", !trail.material_handle.is_valid())) {
            trail.material_handle = {};
            trail.material.reset();
        }
        for (const AssetRecord& record : database.records()) {
            if (record.type != AssetType::Material || record.status != AssetStatus::Ready) continue;
            const bool active = record.handle == trail.material_handle;
            if (ImGui::Selectable(record.source_path.c_str(), active)) {
                trail.material_handle = record.handle;
                trail.material = MaterialAsset::load(record.handle, database);
            }
            if (active) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (const auto dropped = accept_asset_drop_target(database, AssetType::Material)) {
        trail.material_handle = *dropped;
        trail.material = MaterialAsset::load(*dropped, database);
    }
    track(entities, entity, material_before, "Trail.Material", "Assign Trail Material", transaction,
          material_before.material_handle != trail.material_handle);

    const TrailRenderer2DComponent colors_before = trail;
    draw_color("Head Color", trail.head_color);
    draw_color("Tail Color", trail.tail_color);
    track(entities, entity, colors_before, "Trail.Colors", "Edit Trail Colors", transaction);

    namespace Policies = InspectorPropertyPolicies;
    const TrailRenderer2DComponent shape_before = trail;
    InspectorWidgets::property_float("Lifetime", &trail.lifetime,
        Policies::bounded_float(0.01f, 0.01f, 10.0f, "%.2f s"));
    InspectorWidgets::property_float("Point Distance", &trail.min_vertex_distance,
        Policies::bounded_float(0.1f, 0.1f, 256.0f, "%.1f px"));
    InspectorWidgets::property_float("Head Width", &trail.head_width,
        Policies::bounded_float(0.25f, 0.0f, 512.0f, "%.1f px"));
    InspectorWidgets::property_float("Tail Width", &trail.tail_width,
        Policies::bounded_float(0.25f, 0.0f, 512.0f, "%.1f px"));
    InspectorWidgets::property_float("Texture Scale", &trail.texture_scale,
        Policies::bounded_float(0.05f, 0.05f, 32.0f, "%.2f"));
    InspectorWidgets::property_float("Flow Speed", &trail.flow_speed,
        Policies::bounded_float(0.05f, -20.0f, 20.0f, "%.2f"));
    int max_points = trail.max_points;
    if (InspectorWidgets::property_int("Max Points", &max_points,
            Policies::bounded_int(1.0f, 2, 2048))) {
        trail.max_points = static_cast<std::uint16_t>(max_points);
    }
    track(entities, entity, shape_before, "Trail.Shape", "Edit Trail Shape", transaction);

    const TrailRenderer2DComponent sorting_before = trail;
    int sorting_layer = trail.sorting_layer;
    int order = trail.order_in_layer;
    int render_layer = trail.render_layer;
    InspectorWidgets::property_float("Depth", &trail.z_layer, Policies::Transform::Depth);
    if (InspectorWidgets::property_int("Sorting Layer", &sorting_layer, Policies::Sprite::SortingOrder)) {
        trail.sorting_layer = static_cast<std::int16_t>(sorting_layer);
    }
    if (InspectorWidgets::property_int("Order In Layer", &order, Policies::Sprite::SortingOrder)) {
        trail.order_in_layer = static_cast<std::int16_t>(order);
    }
    if (InspectorWidgets::property_int("Render Layer", &render_layer, Policies::Sprite::RenderLayer)) {
        trail.render_layer = static_cast<RenderLayer>(render_layer);
    }
    const char* blend_labels[]{"Alpha", "Additive"};
    int blend = static_cast<int>(trail.blend_mode);
    if (InspectorWidgets::property_enum("Blend", &blend, blend_labels, 2)) {
        trail.blend_mode = static_cast<TrailBlendMode>(blend);
    }
    track(entities, entity, sorting_before, "Trail.Rendering", "Edit Trail Rendering", transaction);

    const TrailRenderer2DComponent flags_before = trail;
    const bool emitting_changed = InspectorWidgets::property_bool("Emitting", &trail.emitting);
    const bool enabled_changed = InspectorWidgets::property_bool("Enabled", &trail.enabled);
    track(entities, entity, flags_before, "Trail.Flags", "Edit Trail State", transaction,
          emitting_changed || enabled_changed);
    ImGui::TextDisabled("Emitting off lets the existing trail fade naturally.");
    ImGui::PopID();
}

} // namespace Engine
