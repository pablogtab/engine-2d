#include "TransformInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "InspectorPropertyPolicies.hpp"
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
