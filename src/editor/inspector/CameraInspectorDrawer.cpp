#include "CameraInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "InspectorPropertyPolicies.hpp"
#include "MultiComponentInspectorTransaction.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
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

    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, "camera");
    const auto header = InspectorWidgets::registered_component_header("camera", true, true, has_override);
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, "camera", nullptr
        ));
        return;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, "camera", nullptr
        ));
        return;
    }
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

    ImGui::PushID("CameraComponentDrawer");

    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    auto& camera = em.get_component<CameraComponent>(entity);

    if (camera.is_primary) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Primary Scene Camera");
    } else if (ImGui::Button("Make Primary", ImVec2(-1.0f, 0.0f)) && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetPrimaryCameraCommand>(&em, entity));
    }

    const CameraComponent clear_color_before = camera;
    float clear_color[4] = {
        camera.clear_color.r / 255.0f,
        camera.clear_color.g / 255.0f,
        camera.clear_color.b / 255.0f,
        camera.clear_color.a / 255.0f
    };
    begin_property_interaction("Camera.ClearColor");
    const bool clear_color_changed = ImGui::ColorEdit4("Clear Color", clear_color);
    capture_property_interaction();
    if (clear_color_changed) {
        camera.clear_color = {
            static_cast<std::uint8_t>(clear_color[0] * 255.0f),
            static_cast<std::uint8_t>(clear_color[1] * 255.0f),
            static_cast<std::uint8_t>(clear_color[2] * 255.0f),
            static_cast<std::uint8_t>(clear_color[3] * 255.0f)
        };
    }
    track(em, entity, clear_color_before, "Camera.ClearColor", "Edit Camera Clear Color", context);

    const CameraComponent zoom_before = camera;
    property_float("Camera Zoom", &camera.zoom, Policies::Camera::Zoom);
    track(em, entity, zoom_before, "Camera.Zoom", "Edit Camera Zoom", context);

    const char* const projection_labels[] = {"Fixed Height", "Fixed Width", "Expand", "Pixel Perfect"};
    int projection = static_cast<int>(camera.projection);
    const CameraComponent projection_before = camera;
    if (property_enum("Projection", &projection, projection_labels, 4)) {
        camera.projection = static_cast<CameraProjectionMode>(projection);
    }
    track(em, entity, projection_before, "Camera.Projection", "Change Camera Projection", context);

    const CameraComponent render_layers_before = camera;
    int render_layer_mask = static_cast<int>(camera.render_layer_mask);
    if (property_int("Render Layer Mask", &render_layer_mask, Policies::Camera::RenderLayerMask)) {
        camera.render_layer_mask = static_cast<RenderLayerMask>(render_layer_mask);
    }
    track(em, entity, render_layers_before, "Camera.RenderLayerMask", "Edit Camera Render Layer Mask", context);

    const auto single_target_filter = [&](EntityID candidate) {
        return candidate != entity && em.has_component<TransformComponent>(candidate);
    };
    const CameraComponent target_before = camera;
    const bool target_changed = property_entity_picker("Follow Target", &camera.follow_target, em, false, single_target_filter);
    track(em, entity, target_before, "Camera.FollowTarget", "Change Camera Follow Target", context, target_changed);

    if (camera.follow_target != 0) {
        const CameraComponent offset_before = camera;
        property_vec2("Follow Offset", &camera.follow_offset.x, Policies::Camera::FollowOffset);
        track(em, entity, offset_before, "Camera.FollowOffset", "Edit Camera Follow Offset", context);

        const CameraComponent speed_before = camera;
        property_float("Follow Speed", &camera.follow_speed, Policies::Camera::FollowSpeed);
        track(em, entity, speed_before, "Camera.FollowSpeed", "Edit Camera Follow Speed", context);
    }

    const CameraComponent bounds_enabled_before = camera;
    const bool bounds_toggled = property_bool("Enable Bounds", &camera.enable_bounds);
    track(
        em, entity, bounds_enabled_before, "Camera.EnableBounds", "Toggle Camera Bounds", context, bounds_toggled
    );
    if (camera.enable_bounds) {
        const CameraComponent bounds_before = camera;
        float bounds[4] = {camera.bounds.x, camera.bounds.y, camera.bounds.z, camera.bounds.w};
        const char* const bounds_labels[4] = {"MinX", "MinY", "MaxX", "MaxY"};
        if (property_vec4("Bounds", bounds, Policies::Camera::Bounds, false, false, false, false, false, nullptr, nullptr, nullptr, nullptr, bounds_labels)) {
            camera.bounds = glm::vec4(bounds[0], bounds[1], bounds[2], bounds[3]);
        }
        if (last_property_interaction().deactivated_after_edit) {
            if (camera.bounds.x > camera.bounds.z) std::swap(camera.bounds.x, camera.bounds.z);
            if (camera.bounds.y > camera.bounds.w) std::swap(camera.bounds.y, camera.bounds.w);
        }
        track(em, entity, bounds_before, "Camera.Bounds", "Edit Camera Bounds", context);
    }

    ImGui::TextDisabled("Output resolution is configured in Project Settings");
    ImGui::PopID();
}

void CameraInspectorDrawer::draw_multiple(
    EntityManager& em,
    const std::vector<EntityID>& entities,
    const ComponentTransactionContext& context
) {
    const auto before = capture_selected_components<CameraComponent>(em, entities);
    if (before.empty()) return;

    const auto header = InspectorWidgets::registered_component_header("camera", false);
    if (!header.open) return;
    ImGui::PushID("CameraComponentDrawer");

    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;

    // Primary status summary
    const bool all_primary = std::all_of(before.begin(), before.end(), [](const auto& p) { return p.second.is_primary; });
    const bool any_primary = std::any_of(before.begin(), before.end(), [](const auto& p) { return p.second.is_primary; });
    if (all_primary) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Primary Scene Cameras");
    } else if (any_primary) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "[Mixed Primary State]");
    }

    {
        const auto before = capture_selected_components<CameraComponent>(em, entities);
        const SDL_Color first = before.front().second.clear_color;
        const bool mixed = selected_values_mixed<CameraComponent, SDL_Color>(before,
            [](const CameraComponent& camera) { return camera.clear_color; },
            [](const SDL_Color& lhs, const SDL_Color& rhs) {
                return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
            }
        );
        float clear_color[4] = {
            first.r / 255.0f, first.g / 255.0f, first.b / 255.0f, first.a / 255.0f
        };
        begin_property_interaction("MultiCamera.ClearColor");
        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool changed = ImGui::ColorEdit4("Clear Color", clear_color);
        if (mixed) ImGui::PopItemFlag();
        capture_property_interaction();
        if (changed) {
            const SDL_Color value{
                static_cast<std::uint8_t>(clear_color[0] * 255.0f),
                static_cast<std::uint8_t>(clear_color[1] * 255.0f),
                static_cast<std::uint8_t>(clear_color[2] * 255.0f),
                static_cast<std::uint8_t>(clear_color[3] * 255.0f)
            };
            for (const EntityID entity : entities) {
                if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                    em.get_component<CameraComponent>(entity).clear_color = value;
                }
            }
        }
        track_selected_component_edit<CameraComponent>(
            em, entities, before, "Camera.ClearColor", "Edit Camera Clear Color",
            context, std::equal_to<CameraComponent>{}
        );
    }

    // Zoom
    float zoom = before.front().second.zoom;
    const bool zoom_mixed = selected_values_mixed<CameraComponent, float>(
        before, [](const CameraComponent& c) { return c.zoom; }
    );
    if (property_float("Camera Zoom", &zoom, Policies::Camera::Zoom, zoom_mixed)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                em.get_component<CameraComponent>(entity).zoom = zoom;
            }
        }
    }
    track_selected_component_edit<CameraComponent>(
        em, entities, before, "Camera.Zoom", "Edit Camera Zoom",
        context, std::equal_to<CameraComponent>{}
    );

    // Projection Mode
    const char* const projection_labels[] = {"Fixed Height", "Fixed Width", "Expand", "Pixel Perfect"};
    int projection = static_cast<int>(before.front().second.projection);
    const bool projection_mixed = selected_values_mixed<CameraComponent, CameraProjectionMode>(
        before, [](const CameraComponent& c) { return c.projection; }
    );
    const bool projection_changed = property_enum("Projection", &projection, projection_labels, 4, projection_mixed);
    if (projection_changed) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                em.get_component<CameraComponent>(entity).projection = static_cast<CameraProjectionMode>(projection);
            }
        }
    }
    track_selected_component_edit<CameraComponent>(
        em, entities, before, "Camera.Projection", "Change Camera Projection",
        context, std::equal_to<CameraComponent>{}, {}, projection_changed
    );

    // Follow Target
    const auto multi_target_filter = [&](EntityID candidate) {
        return std::find(entities.begin(), entities.end(), candidate) == entities.end() &&
               em.has_component<TransformComponent>(candidate);
    };
    EntityID target = before.front().second.follow_target;
    const bool target_mixed = selected_values_mixed<CameraComponent, EntityID>(
        before, [](const CameraComponent& c) { return c.follow_target; }
    );
    const bool target_changed = property_entity_picker("Follow Target", &target, em, target_mixed, multi_target_filter);
    if (target_changed) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                em.get_component<CameraComponent>(entity).follow_target = target;
            }
        }
    }
    track_selected_component_edit<CameraComponent>(
        em, entities, before, "Camera.FollowTarget", "Change Camera Follow Target",
        context, std::equal_to<CameraComponent>{}, {}, target_changed
    );

    // Follow Offset
    float offset[2] = {before.front().second.follow_offset.x, before.front().second.follow_offset.y};
    const bool offset_mixed_x = selected_values_mixed<CameraComponent, float>(
        before, [](const CameraComponent& c) { return c.follow_offset.x; }
    );
    const bool offset_mixed_y = selected_values_mixed<CameraComponent, float>(
        before, [](const CameraComponent& c) { return c.follow_offset.y; }
    );
    bool offset_changed_x = false, offset_changed_y = false;
    if (property_vec2("Follow Offset", offset, Policies::Camera::FollowOffset, false, offset_mixed_x, offset_mixed_y, &offset_changed_x, &offset_changed_y)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                auto& cam = em.get_component<CameraComponent>(entity);
                if (offset_changed_x) cam.follow_offset.x = offset[0];
                if (offset_changed_y) cam.follow_offset.y = offset[1];
            }
        }
    }
    track_selected_component_edit<CameraComponent>(
        em, entities, before, "Camera.FollowOffset", "Edit Camera Follow Offset",
        context, std::equal_to<CameraComponent>{}
    );

    // Follow Speed
    float speed = before.front().second.follow_speed;
    const bool speed_mixed = selected_values_mixed<CameraComponent, float>(
        before, [](const CameraComponent& c) { return c.follow_speed; }
    );
    if (property_float("Follow Speed", &speed, Policies::Camera::FollowSpeed, speed_mixed)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                em.get_component<CameraComponent>(entity).follow_speed = speed;
            }
        }
    }
    track_selected_component_edit<CameraComponent>(
        em, entities, before, "Camera.FollowSpeed", "Edit Camera Follow Speed",
        context, std::equal_to<CameraComponent>{}
    );

    // Enable Bounds
    bool enable_bounds = before.front().second.enable_bounds;
    const bool enable_bounds_mixed = selected_values_mixed<CameraComponent, bool>(
        before, [](const CameraComponent& c) { return c.enable_bounds; }
    );
    const bool bounds_toggled = property_bool("Enable Bounds", &enable_bounds, enable_bounds_mixed);
    if (bounds_toggled) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                em.get_component<CameraComponent>(entity).enable_bounds = enable_bounds;
            }
        }
    }
    track_selected_component_edit<CameraComponent>(
        em, entities, before, "Camera.EnableBounds", "Toggle Camera Bounds",
        context, std::equal_to<CameraComponent>{}, {}, bounds_toggled
    );

    // Bounds
    const bool any_bounds_enabled = std::any_of(before.begin(), before.end(), [](const auto& p) { return p.second.enable_bounds; });
    if (any_bounds_enabled) {
        float bounds[4] = {
            before.front().second.bounds.x,
            before.front().second.bounds.y,
            before.front().second.bounds.z,
            before.front().second.bounds.w
        };
        const bool mixed_x = selected_values_mixed<CameraComponent, float>(before, [](const auto& c) { return c.bounds.x; });
        const bool mixed_y = selected_values_mixed<CameraComponent, float>(before, [](const auto& c) { return c.bounds.y; });
        const bool mixed_z = selected_values_mixed<CameraComponent, float>(before, [](const auto& c) { return c.bounds.z; });
        const bool mixed_w = selected_values_mixed<CameraComponent, float>(before, [](const auto& c) { return c.bounds.w; });

        bool ch_x = false, ch_y = false, ch_z = false, ch_w = false;
        const char* const bounds_labels[4] = {"MinX", "MinY", "MaxX", "MaxY"};
        if (property_vec4("Bounds", bounds, Policies::Camera::Bounds, false, mixed_x, mixed_y, mixed_z, mixed_w, &ch_x, &ch_y, &ch_z, &ch_w, bounds_labels)) {
            for (const EntityID entity : entities) {
                if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                    auto& cam_bounds = em.get_component<CameraComponent>(entity).bounds;
                    if (ch_x) cam_bounds.x = bounds[0];
                    if (ch_y) cam_bounds.y = bounds[1];
                    if (ch_z) cam_bounds.z = bounds[2];
                    if (ch_w) cam_bounds.w = bounds[3];
                }
            }
        }
        if (last_property_interaction().deactivated_after_edit) {
            for (const EntityID entity : entities) {
                if (em.is_alive(entity) && em.has_component<CameraComponent>(entity)) {
                    auto& cam = em.get_component<CameraComponent>(entity);
                    if (cam.bounds.x > cam.bounds.z) std::swap(cam.bounds.x, cam.bounds.z);
                    if (cam.bounds.y > cam.bounds.w) std::swap(cam.bounds.y, cam.bounds.w);
                }
            }
        }
        track_selected_component_edit<CameraComponent>(
            em, entities, before, "Camera.Bounds", "Edit Camera Bounds",
            context, std::equal_to<CameraComponent>{}
        );
    }
    ImGui::PopID();
}

} // namespace Engine
