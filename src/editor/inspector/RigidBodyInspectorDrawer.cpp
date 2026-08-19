#include "RigidBodyInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../ecs/Components.hpp"
#include "../../physics/PhysicsSystem.hpp"
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

    const auto header = InspectorWidgets::component_header(
        "RigidBody 2D", "RigidBodyHeader", true, true, EditorIcon::Physics
    );
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

    using namespace InspectorWidgets;
    auto& rigidbody = em.get_component<RigidBodyComponent>(entity);

    const char* body_types[] = {"Dynamic", "Static", "Kinematic"};
    int body_type = static_cast<int>(rigidbody.body_type);
    const RigidBodyComponent body_type_before = rigidbody;
    begin_property_row("Body Type");
    begin_property_interaction("RigidBody.BodyType");
    ImGui::PushID("rigidbody_body_type");
    const bool body_type_changed = ImGui::Combo("##value", &body_type, body_types, IM_ARRAYSIZE(body_types));
    capture_property_interaction();
    ImGui::PopID();
    if (body_type_changed) rigidbody.body_type = static_cast<BodyType>(body_type);
    track(em, entity, body_type_before, "RigidBody.BodyType", "Change RigidBody Type", context, body_type_changed);

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
}

} // namespace Engine
