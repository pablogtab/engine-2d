#include "RigidBodyInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "MultiComponentInspectorTransaction.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include <imgui.h>


namespace Engine {

namespace {

bool authored_rigidbody_equal(const RigidBodyComponent& lhs, const RigidBodyComponent& rhs) {
    return lhs.body_type == rhs.body_type &&
        lhs.mass == rhs.mass &&
        lhs.gravity_scale == rhs.gravity_scale &&
        lhs.friction == rhs.friction &&
        lhs.restitution == rhs.restitution &&
        lhs.fixed_rotation == rhs.fixed_rotation;
}

void rebuild_physics(EntityManager& em, EntityID entity) {
    PhysicsSystem::rebuild_body(em, entity);
}

void rebuild_selected_rigidbody_physics(EntityManager& em, const std::vector<EntityID>& entities) {
    for (const EntityID entity : entities) {
        if (em.is_alive(entity)) {
            PhysicsSystem::rebuild_body(em, entity);
        }
    }
}

void track(
    EntityManager& em,
    EntityID entity,
    const RigidBodyComponent& before,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context,
    bool force_commit = false
) {
    track_component_edit<RigidBodyComponent>(
        em,
        entity,
        before,
        property,
        description,
        context,
        authored_rigidbody_equal,
        rebuild_physics,
        force_commit
    );
}

} // namespace

void RigidBodyInspectorDrawer::draw(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<RigidBodyComponent>(entity)) return;

    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, "rigidbody");
    const auto header = InspectorWidgets::registered_component_header("rigidbody", true, true, has_override);
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, "rigidbody", nullptr
        ));
        return;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, "rigidbody", nullptr
        ));
        return;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<RigidBodyComponent>>(
            &em,
            entity,
            em.get_component<RigidBodyComponent>(entity),
            std::nullopt,
            "Remove RigidBody 2D",
            rebuild_physics
        ));
        return;
    }
    if (!header.open) return;
    ImGui::PushID("RigidBodyComponentDrawer");


    using namespace InspectorWidgets;
    auto& rigidbody = em.get_component<RigidBodyComponent>(entity);

    const char* const body_types[] = {"Dynamic", "Static", "Kinematic"};
    int body_type = static_cast<int>(rigidbody.body_type);
    const RigidBodyComponent body_type_before = rigidbody;
    if (property_enum("Body Type", &body_type, body_types, 3)) {
        rigidbody.body_type = static_cast<BodyType>(body_type);
    }
    track(em, entity, body_type_before, "RigidBody.BodyType", "Change RigidBody Type", context);

    const RigidBodyComponent mass_before = rigidbody;
    property_float("Mass", &rigidbody.mass, InspectorPropertyPolicies::RigidBody::Mass);
    track(em, entity, mass_before, "RigidBody.Mass", "Edit RigidBody Mass", context);

    const RigidBodyComponent gravity_before = rigidbody;
    property_float(
        "Gravity Scale", &rigidbody.gravity_scale, InspectorPropertyPolicies::RigidBody::GravityScale
    );
    track(em, entity, gravity_before, "RigidBody.GravityScale", "Edit RigidBody Gravity Scale", context);

    const RigidBodyComponent friction_before = rigidbody;
    property_float("Friction", &rigidbody.friction, InspectorPropertyPolicies::RigidBody::Friction);
    track(em, entity, friction_before, "RigidBody.Friction", "Edit RigidBody Friction", context);

    const RigidBodyComponent restitution_before = rigidbody;
    property_float(
        "Restitution", &rigidbody.restitution, InspectorPropertyPolicies::RigidBody::Restitution
    );
    track(em, entity, restitution_before, "RigidBody.Restitution", "Edit RigidBody Restitution", context);

    const RigidBodyComponent rotation_before = rigidbody;
    property_bool("Fixed Rotation", &rigidbody.fixed_rotation);
    track(em, entity, rotation_before, "RigidBody.FixedRotation", "Toggle RigidBody Fixed Rotation", context);

    ImGui::Text("Velocity: (%.1f, %.1f)", rigidbody.velocity.x, rigidbody.velocity.y);
    ImGui::PopID();
}

void RigidBodyInspectorDrawer::draw_multiple(
    EntityManager& em,
    const std::vector<EntityID>& entities,
    const ComponentTransactionContext& context
) {
    const auto before = capture_selected_components<RigidBodyComponent>(em, entities);
    if (before.empty()) return;

    const auto header = InspectorWidgets::registered_component_header("rigidbody", false);
    if (!header.open) return;
    ImGui::PushID("RigidBodyComponentDrawer");

    using namespace InspectorWidgets;

    // Body Type
    const char* const body_types[] = {"Dynamic", "Static", "Kinematic"};
    int body_type = static_cast<int>(before.front().second.body_type);
    const bool body_type_mixed = selected_values_mixed<RigidBodyComponent, BodyType>(
        before, [](const RigidBodyComponent& rb) { return rb.body_type; }
    );
    const bool body_type_changed = property_enum("Body Type", &body_type, body_types, 3, body_type_mixed);
    if (body_type_changed) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<RigidBodyComponent>(entity)) {
                em.get_component<RigidBodyComponent>(entity).body_type = static_cast<BodyType>(body_type);
            }
        }
    }
    track_selected_component_edit<RigidBodyComponent>(
        em, entities, before, "RigidBody.BodyType", "Change RigidBody Type",
        context, authored_rigidbody_equal, rebuild_selected_rigidbody_physics, body_type_changed
    );

    // Mass
    float mass = before.front().second.mass;
    const bool mass_mixed = selected_values_mixed<RigidBodyComponent, float>(
        before, [](const RigidBodyComponent& rb) { return rb.mass; }
    );
    if (property_float("Mass", &mass, InspectorPropertyPolicies::RigidBody::Mass, mass_mixed)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<RigidBodyComponent>(entity)) {
                em.get_component<RigidBodyComponent>(entity).mass = mass;
            }
        }
    }
    track_selected_component_edit<RigidBodyComponent>(
        em, entities, before, "RigidBody.Mass", "Edit RigidBody Mass",
        context, authored_rigidbody_equal, rebuild_selected_rigidbody_physics
    );

    // Gravity Scale
    float gravity_scale = before.front().second.gravity_scale;
    const bool gravity_mixed = selected_values_mixed<RigidBodyComponent, float>(
        before, [](const RigidBodyComponent& rb) { return rb.gravity_scale; }
    );
    if (property_float("Gravity Scale", &gravity_scale, InspectorPropertyPolicies::RigidBody::GravityScale, gravity_mixed)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<RigidBodyComponent>(entity)) {
                em.get_component<RigidBodyComponent>(entity).gravity_scale = gravity_scale;
            }
        }
    }
    track_selected_component_edit<RigidBodyComponent>(
        em, entities, before, "RigidBody.GravityScale", "Edit RigidBody Gravity Scale",
        context, authored_rigidbody_equal, rebuild_selected_rigidbody_physics
    );

    // Friction
    float friction = before.front().second.friction;
    const bool friction_mixed = selected_values_mixed<RigidBodyComponent, float>(
        before, [](const RigidBodyComponent& rb) { return rb.friction; }
    );
    if (property_float("Friction", &friction, InspectorPropertyPolicies::RigidBody::Friction, friction_mixed)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<RigidBodyComponent>(entity)) {
                em.get_component<RigidBodyComponent>(entity).friction = friction;
            }
        }
    }
    track_selected_component_edit<RigidBodyComponent>(
        em, entities, before, "RigidBody.Friction", "Edit RigidBody Friction",
        context, authored_rigidbody_equal, rebuild_selected_rigidbody_physics
    );

    // Restitution
    float restitution = before.front().second.restitution;
    const bool restitution_mixed = selected_values_mixed<RigidBodyComponent, float>(
        before, [](const RigidBodyComponent& rb) { return rb.restitution; }
    );
    if (property_float("Restitution", &restitution, InspectorPropertyPolicies::RigidBody::Restitution, restitution_mixed)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<RigidBodyComponent>(entity)) {
                em.get_component<RigidBodyComponent>(entity).restitution = restitution;
            }
        }
    }
    track_selected_component_edit<RigidBodyComponent>(
        em, entities, before, "RigidBody.Restitution", "Edit RigidBody Restitution",
        context, authored_rigidbody_equal, rebuild_selected_rigidbody_physics
    );

    // Fixed Rotation
    bool fixed_rotation = before.front().second.fixed_rotation;
    const bool fixed_rotation_mixed = selected_values_mixed<RigidBodyComponent, bool>(
        before, [](const RigidBodyComponent& rb) { return rb.fixed_rotation; }
    );
    const bool rotation_changed = property_bool("Fixed Rotation", &fixed_rotation, fixed_rotation_mixed);
    if (rotation_changed) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<RigidBodyComponent>(entity)) {
                em.get_component<RigidBodyComponent>(entity).fixed_rotation = fixed_rotation;
            }
        }
    }
    track_selected_component_edit<RigidBodyComponent>(
        em, entities, before, "RigidBody.FixedRotation", "Toggle RigidBody Fixed Rotation",
        context, authored_rigidbody_equal, rebuild_selected_rigidbody_physics, rotation_changed
    );
    ImGui::PopID();
}

} // namespace Engine
