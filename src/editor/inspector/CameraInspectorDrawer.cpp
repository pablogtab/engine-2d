#include "CameraInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "InspectorPropertyPolicies.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../ecs/Components.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>

namespace Engine {

namespace {

void track(
    EntityManager& em,
    EntityID entity,
    const CameraComponent& before,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context,
    bool force_commit = false
) {
    track_component_edit<CameraComponent>(
        em,
        entity,
        before,
        property,
        description,
        context,
        std::equal_to<CameraComponent>{},
        {},
        force_commit
    );
}

} // namespace

void CameraInspectorDrawer::draw(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<CameraComponent>(entity)) return;

    const auto header = InspectorWidgets::component_header(
        "Camera 2D", "CameraHeader", true, true, EditorIcon::Camera
    );
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<CameraComponent>>(
            &em,
            entity,
            em.get_component<CameraComponent>(entity),
            std::nullopt,
            "Remove Camera 2D"
        ));
        return;
    }
    if (!header.open) return;

    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    auto& camera = em.get_component<CameraComponent>(entity);

    if (camera.is_primary) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Primary Scene Camera");
    } else if (ImGui::Button("Make Primary", ImVec2(-1.0f, 0.0f)) && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetPrimaryCameraCommand>(&em, entity));
    }

    const CameraComponent zoom_before = camera;
    property_float("Camera Zoom", &camera.zoom, Policies::Camera::Zoom);
    track(em, entity, zoom_before, "Camera.Zoom", "Edit Camera Zoom", context);

    const char* projection_labels[] = {"Fixed Height", "Fixed Width", "Expand", "Pixel Perfect"};
    int projection = static_cast<int>(camera.projection);
    const CameraComponent projection_before = camera;
    begin_property_row("Projection");
    begin_property_interaction("Camera.Projection");
    ImGui::PushID("camera_projection");
    ImGui::SetNextItemWidth(-1.0f);
    const bool projection_changed = ImGui::Combo(
        "##value", &projection, projection_labels, IM_ARRAYSIZE(projection_labels)
    );
    capture_property_interaction();
    ImGui::PopID();
    if (projection_changed) camera.projection = static_cast<CameraProjectionMode>(projection);
    track(
        em, entity, projection_before, "Camera.Projection", "Change Camera Projection",
        context, projection_changed
    );

    const char* target_preview = "None";
    if (camera.follow_target != 0 && em.is_alive(camera.follow_target)) {
        target_preview = em.has_component<TagComponent>(camera.follow_target)
            ? em.get_component<TagComponent>(camera.follow_target).tag.c_str()
            : "Entity";
    }
    begin_property_row("Follow Target");
    ImGui::PushID("camera_follow_target");
    if (ImGui::BeginCombo("##value", target_preview)) {
        if (ImGui::Selectable("None", camera.follow_target == 0)) {
            const CameraComponent before = camera;
            camera.follow_target = 0;
            track(em, entity, before, "Camera.FollowTarget", "Change Camera Follow Target", context, true);
        }
        for (const EntityID candidate : em.get_living_entities()) {
            if (candidate == entity || !em.has_component<TransformComponent>(candidate)) continue;
            const std::string name = em.has_component<TagComponent>(candidate)
                ? em.get_component<TagComponent>(candidate).tag
                : "Entity #" + std::to_string(candidate);
            const std::string item_label = name + "##" + std::to_string(candidate);
            if (ImGui::Selectable(item_label.c_str(), camera.follow_target == candidate)) {
                const CameraComponent before = camera;
                camera.follow_target = candidate;
                track(em, entity, before, "Camera.FollowTarget", "Change Camera Follow Target", context, true);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();

    if (camera.follow_target != 0) {
        const CameraComponent offset_before = camera;
        property_vec2("Follow Offset", &camera.follow_offset.x, Policies::Camera::FollowOffset);
        track(em, entity, offset_before, "Camera.FollowOffset", "Edit Camera Follow Offset", context);

        const CameraComponent speed_before = camera;
        property_float("Follow Speed", &camera.follow_speed, Policies::Camera::FollowSpeed);
        track(em, entity, speed_before, "Camera.FollowSpeed", "Edit Camera Follow Speed", context);
    }

    const CameraComponent bounds_enabled_before = camera;
    property_bool("Enable Bounds", &camera.enable_bounds);
    track(
        em, entity, bounds_enabled_before, "Camera.EnableBounds", "Toggle Camera Bounds", context
    );
    if (camera.enable_bounds) {
        const CameraComponent bounds_before = camera;
        begin_property_row("Bounds");
        begin_property_interaction("Camera.Bounds");
        ImGui::PushID("camera_bounds");
        const auto& policy = Policies::Camera::Bounds;
        ImGui::DragFloat4(
            "##value", &camera.bounds.x, policy.speed, policy.minimum, policy.maximum, policy.format
        );
        capture_property_interaction();
        ImGui::PopID();
        if (last_property_interaction().deactivated_after_edit) {
            if (camera.bounds.x > camera.bounds.z) std::swap(camera.bounds.x, camera.bounds.z);
            if (camera.bounds.y > camera.bounds.w) std::swap(camera.bounds.y, camera.bounds.w);
        }
        track(em, entity, bounds_before, "Camera.Bounds", "Edit Camera Bounds", context);
    }

    ImGui::TextDisabled("Output resolution is configured in Project Settings");
}

} // namespace Engine
