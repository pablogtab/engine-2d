#include "TransformInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "InspectorPropertyPolicies.hpp"
#include "MultiComponentInspectorTransaction.hpp"
#include "../../ecs/Components.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include <imgui.h>
#include <string>

namespace Engine {

namespace {

bool authored_transform_equal(const TransformComponent& lhs, const TransformComponent& rhs) {
    return lhs.position == rhs.position &&
        lhs.scale == rhs.scale &&
        lhs.rotation == rhs.rotation &&
        lhs.pivot == rhs.pivot &&
        lhs.skew == rhs.skew &&
        lhs.visible == rhs.visible;
}

void preview_transform(EntityManager& em, EntityID entity) {
    TransformSystem::update(em);
    PhysicsSystem::sync_body_hierarchy(em, entity);
}

void rebuild_transform_physics(EntityManager& em, EntityID entity) {
    TransformSystem::update(em);
    PhysicsSystem::rebuild_body_hierarchy(em, entity);
}

void preview_selected_transforms(EntityManager& em, const std::vector<EntityID>& entities) {
    TransformSystem::update(em);
    for (const EntityID entity : entities) PhysicsSystem::sync_body_hierarchy(em, entity);
}

void rebuild_selected_transform_physics(EntityManager& em, const std::vector<EntityID>& entities) {
    TransformSystem::update(em);
    for (const EntityID entity : entities) PhysicsSystem::rebuild_body_hierarchy(em, entity);
}

void track(
    EntityManager& em,
    EntityID entity,
    const TransformComponent& before,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context,
    bool rebuild_physics = false,
    bool force_commit = false
) {
    track_component_edit<TransformComponent>(
        em,
        entity,
        before,
        property,
        description,
        context,
        authored_transform_equal,
        rebuild_physics ? rebuild_transform_physics : preview_transform,
        force_commit
    );
}

} // namespace

void TransformInspectorDrawer::draw_entities_active(
    EntityManager& em,
    const std::vector<EntityID>& entities,
    const ComponentTransactionContext& context
) {
    const auto before = capture_selected_components<TransformComponent>(em, entities);
    if (before.empty()) return;
    bool visible = before.front().second.visible;
    const bool mixed = selected_values_mixed<TransformComponent, bool>(
        before, [](const TransformComponent& transform) { return transform.visible; }
    );
    const bool changed = InspectorWidgets::property_bool("Active", &visible, mixed);
    if (changed) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<TransformComponent>(entity)) {
                em.get_component<TransformComponent>(entity).visible = visible;
            }
        }
        preview_selected_transforms(em, entities);
    }
    track_selected_component_edit<TransformComponent>(
        em, entities, before, "Transform.EntityActive", "Edit Entity Active State",
        context, authored_transform_equal, rebuild_selected_transform_physics
    );
}

void TransformInspectorDrawer::draw_multiple(
    EntityManager& em,
    const std::vector<EntityID>& entities,
    bool locked,
    const ComponentTransactionContext& context
) {
    auto states = capture_selected_components<TransformComponent>(em, entities);
    if (states.size() != entities.size()) return;
    if (!InspectorWidgets::component_header(
            "Transform", "TransformHeader", false, true, EditorIcon::Move
        ).open) return;

    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    if (locked) ImGui::TextDisabled("One or more selected transforms are managed and cannot be edited.");
    ImGui::BeginDisabled(locked);

    const auto apply_all = [&](auto setter) {
        for (const EntityID entity : entities) setter(em.get_component<TransformComponent>(entity));
    };

    {
        const auto before = capture_selected_components<TransformComponent>(em, entities);
        float values[2] = {before.front().second.position.x, before.front().second.position.y};
        const bool mixed_x = selected_values_mixed<TransformComponent, float>(before, [](const auto& value) { return value.position.x; });
        const bool mixed_y = selected_values_mixed<TransformComponent, float>(before, [](const auto& value) { return value.position.y; });
        bool changed_x = false;
        bool changed_y = false;
        if (property_vec2("Position", values, Policies::Transform::Position, true, mixed_x, mixed_y, &changed_x, &changed_y)) {
            apply_all([&](auto& value) {
                if (changed_x) value.position.x = values[0];
                if (changed_y) value.position.y = values[1];
            });
            preview_selected_transforms(em, entities);
        }
        track_selected_component_edit<TransformComponent>(
            em, entities, before, "Transform.Position", "Edit Transform Position",
            context, authored_transform_equal, preview_selected_transforms
        );
        if (property_reset_button("R##multi_position")) {
            const auto reset_before = capture_selected_components<TransformComponent>(em, entities);
            apply_all([](auto& value) { value.position.x = 0.0f; value.position.y = 0.0f; });
            preview_selected_transforms(em, entities);
            track_selected_component_edit<TransformComponent>(
                em, entities, reset_before, "Transform.ResetPosition", "Reset Transform Position",
                context, authored_transform_equal, preview_selected_transforms, true
            );
        }
    }

    {
        const auto before = capture_selected_components<TransformComponent>(em, entities);
        float depth = before.front().second.position.z;
        const bool mixed = selected_values_mixed<TransformComponent, float>(before, [](const auto& value) { return value.position.z; });
        if (property_axis_float("Z Depth", "Z", &depth, Policies::Transform::Depth, false, mixed)) {
            apply_all([&](auto& value) { value.position.z = depth; });
            preview_selected_transforms(em, entities);
        }
        track_selected_component_edit<TransformComponent>(
            em, entities, before, "Transform.Depth", "Edit Transform Z Depth",
            context, authored_transform_equal, preview_selected_transforms
        );
    }

    {
        const auto before = capture_selected_components<TransformComponent>(em, entities);
        float values[2] = {before.front().second.scale.x, before.front().second.scale.y};
        const bool mixed_x = selected_values_mixed<TransformComponent, float>(before, [](const auto& value) { return value.scale.x; });
        const bool mixed_y = selected_values_mixed<TransformComponent, float>(before, [](const auto& value) { return value.scale.y; });
        bool changed_x = false;
        bool changed_y = false;
        if (property_vec2("Scale", values, Policies::Transform::Scale, true, mixed_x, mixed_y, &changed_x, &changed_y)) {
            apply_all([&](auto& value) {
                if (changed_x) value.scale.x = values[0];
                if (changed_y) value.scale.y = values[1];
            });
            preview_selected_transforms(em, entities);
        }
        track_selected_component_edit<TransformComponent>(
            em, entities, before, "Transform.Scale", "Edit Transform Scale",
            context, authored_transform_equal, rebuild_selected_transform_physics
        );
        if (property_reset_button("R##multi_scale")) {
            const auto reset_before = capture_selected_components<TransformComponent>(em, entities);
            apply_all([](auto& value) { value.scale = {1.0f, 1.0f}; });
            preview_selected_transforms(em, entities);
            track_selected_component_edit<TransformComponent>(
                em, entities, reset_before, "Transform.ResetScale", "Reset Transform Scale",
                context, authored_transform_equal, rebuild_selected_transform_physics, true
            );
        }
    }

    {
        const auto before = capture_selected_components<TransformComponent>(em, entities);
        float rotation = before.front().second.rotation;
        const bool mixed = selected_values_mixed<TransformComponent, float>(before, [](const auto& value) { return value.rotation; });
        if (property_axis_float("Rotation", "Z", &rotation, Policies::Transform::Rotation, true, mixed)) {
            apply_all([&](auto& value) { value.rotation = rotation; });
            preview_selected_transforms(em, entities);
        }
        track_selected_component_edit<TransformComponent>(
            em, entities, before, "Transform.Rotation", "Edit Transform Rotation",
            context, authored_transform_equal, preview_selected_transforms
        );
        if (property_reset_button("R##multi_rotation")) {
            const auto reset_before = capture_selected_components<TransformComponent>(em, entities);
            apply_all([](auto& value) { value.rotation = 0.0f; });
            preview_selected_transforms(em, entities);
            track_selected_component_edit<TransformComponent>(
                em, entities, reset_before, "Transform.ResetRotation", "Reset Transform Rotation",
                context, authored_transform_equal, preview_selected_transforms, true
            );
        }
    }

    {
        const auto before = capture_selected_components<TransformComponent>(em, entities);
        float values[2] = {before.front().second.pivot.x, before.front().second.pivot.y};
        const bool mixed_x = selected_values_mixed<TransformComponent, float>(before, [](const auto& value) { return value.pivot.x; });
        const bool mixed_y = selected_values_mixed<TransformComponent, float>(before, [](const auto& value) { return value.pivot.y; });
        bool changed_x = false;
        bool changed_y = false;
        if (property_vec2("Pivot", values, Policies::Transform::Pivot, false, mixed_x, mixed_y, &changed_x, &changed_y)) {
            apply_all([&](auto& value) {
                if (changed_x) value.pivot.x = values[0];
                if (changed_y) value.pivot.y = values[1];
            });
            preview_selected_transforms(em, entities);
        }
        track_selected_component_edit<TransformComponent>(
            em, entities, before, "Transform.Pivot", "Edit Transform Pivot",
            context, authored_transform_equal, preview_selected_transforms
        );
    }

    {
        const auto before = capture_selected_components<TransformComponent>(em, entities);
        bool visible = before.front().second.visible;
        const bool mixed = selected_values_mixed<TransformComponent, bool>(before, [](const auto& value) { return value.visible; });
        if (property_bool("Visible", &visible, mixed)) {
            apply_all([&](auto& value) { value.visible = visible; });
            preview_selected_transforms(em, entities);
        }
        track_selected_component_edit<TransformComponent>(
            em, entities, before, "Transform.Visible", "Edit Transform Visibility",
            context, authored_transform_equal, rebuild_selected_transform_physics
        );
    }

    ImGui::EndDisabled();
}

void TransformInspectorDrawer::draw_entity_active(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<TransformComponent>(entity)) return;
    auto& transform = em.get_component<TransformComponent>(entity);
    const TransformComponent before = transform;
    InspectorWidgets::begin_property_interaction("Transform.EntityActive");
    const bool changed = ImGui::Checkbox("Active", &transform.visible);
    InspectorWidgets::capture_property_interaction();
    if (changed) preview_transform(em, entity);
    track(em, entity, before, "Transform.EntityActive", "Edit Entity Active State", context, true);
}

void TransformInspectorDrawer::draw(
    EntityManager& em,
    EntityID entity,
    bool locked,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<TransformComponent>(entity)) return;
    if (!InspectorWidgets::component_header(
            "Transform", "TransformHeader", false, true, EditorIcon::Move
        ).open) return;

    if (locked) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.75f, 1.0f, 1.0f));
        const std::string label = editor_icon_label(
            EditorIcon::Lock, "Auto-generated Tilemap Collider (Managed by Tilemap)"
        );
        ImGui::TextWrapped("%s", label.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Transform is locked to Tilemap grid geometry.");
        ImGui::Spacing();
    }

    ImGui::BeginDisabled(locked);
    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    auto& transform = em.get_component<TransformComponent>(entity);

    const bool has_parent = em.has_component<HierarchyComponent>(entity) &&
        em.get_component<HierarchyComponent>(entity).parent != 0 &&
        em.is_alive(em.get_component<HierarchyComponent>(entity).parent);
    if (has_parent) {
        const EntityID parent = em.get_component<HierarchyComponent>(entity).parent;
        const char* parent_name = em.has_component<TagComponent>(parent)
            ? em.get_component<TagComponent>(parent).tag.c_str()
            : "Entity";
        ImGui::TextDisabled("Local transform relative to: %s", parent_name);
    }

    float position[2] = {transform.position.x, transform.position.y};
    const TransformComponent position_before = transform;
    if (property_vec2("Position", position, Policies::Transform::Position, true)) {
        transform.position.x = position[0];
        transform.position.y = position[1];
        preview_transform(em, entity);
    }
    track(em, entity, position_before, "Transform.Position", "Edit Transform Position", context);
    if (property_reset_button("R##position")) {
        const TransformComponent before = transform;
        transform.position.x = 0.0f;
        transform.position.y = 0.0f;
        preview_transform(em, entity);
        track(
            em, entity, before, "Transform.ResetPosition", "Reset Transform Position",
            context, false, true
        );
    }

    const TransformComponent depth_before = transform;
    if (property_axis_float("Z Depth", "Z", &transform.position.z, Policies::Transform::Depth)) {
        preview_transform(em, entity);
    }
    track(em, entity, depth_before, "Transform.Depth", "Edit Transform Z Depth", context);

    float scale[2] = {transform.scale.x, transform.scale.y};
    const TransformComponent scale_before = transform;
    if (property_vec2("Scale", scale, Policies::Transform::Scale, true)) {
        transform.scale = {scale[0], scale[1]};
        preview_transform(em, entity);
    }
    track(em, entity, scale_before, "Transform.Scale", "Edit Transform Scale", context, true);
    if (property_reset_button("R##scale")) {
        const TransformComponent before = transform;
        transform.scale = {1.0f, 1.0f};
        preview_transform(em, entity);
        track(
            em, entity, before, "Transform.ResetScale", "Reset Transform Scale",
            context, true, true
        );
    }

    const TransformComponent rotation_before = transform;
    if (property_axis_float(
            "Rotation", "Z", &transform.rotation, Policies::Transform::Rotation, true
        )) {
        preview_transform(em, entity);
    }
    track(em, entity, rotation_before, "Transform.Rotation", "Edit Transform Rotation", context);
    if (property_reset_button("R##rotation")) {
        const TransformComponent before = transform;
        transform.rotation = 0.0f;
        preview_transform(em, entity);
        track(
            em, entity, before, "Transform.ResetRotation", "Reset Transform Rotation",
            context, false, true
        );
    }

    float pivot[2] = {transform.pivot.x, transform.pivot.y};
    const TransformComponent pivot_before = transform;
    if (property_vec2("Pivot", pivot, Policies::Transform::Pivot)) {
        transform.pivot = {pivot[0], pivot[1]};
        preview_transform(em, entity);
    }
    track(em, entity, pivot_before, "Transform.Pivot", "Edit Transform Pivot", context);

    const TransformComponent visibility_before = transform;
    if (property_bool("Visible", &transform.visible)) preview_transform(em, entity);
    track(
        em, entity, visibility_before, "Transform.Visible", "Edit Transform Visibility", context, true
    );

    ImGui::EndDisabled();
}

} // namespace Engine
