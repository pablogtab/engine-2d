#include "LightingInspectorDrawer.hpp"

#include "InspectorPropertyPolicies.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include <imgui.h>
#include <cstdint>
#include <string>

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

template<typename Component>
bool handle_header(
    EntityManager& em,
    EntityID entity,
    const char* key,
    const char* remove_description,
    const ComponentTransactionContext& context,
    bool& open
) {
    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, key);
    const auto header = InspectorWidgets::registered_component_header(key, true, true, has_override);
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, key, nullptr
        ));
        return true;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, key, nullptr
        ));
        return true;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<Component>>(
            &em, entity, em.get_component<Component>(entity), std::nullopt, remove_description
        ));
        return true;
    }
    open = header.open;
    return false;
}

} // namespace

void LightingInspectorDrawer::draw_light(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<Light2DComponent>(entity)) return;
    bool open = false;
    if (handle_header<Light2DComponent>(
            em, entity, "light_2d", "Remove Light 2D", context, open
        ) || !open) return;

    ImGui::PushID("Light2DComponentDrawer");
    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    auto& light = em.get_component<Light2DComponent>(entity);

    const char* const type_labels[] = {"Ambient", "Point"};
    int type = static_cast<int>(light.type);
    const Light2DComponent type_before = light;
    if (property_enum("Type", &type, type_labels, 2)) light.type = static_cast<Light2DType>(type);
    track(em, entity, type_before, "Light2D.Type", "Change Light Type", context, type_before.type != light.type);

    const Light2DComponent color_before = light;
    float color[4] = {
        light.color.r / 255.0f, light.color.g / 255.0f,
        light.color.b / 255.0f, light.color.a / 255.0f
    };
    begin_property_interaction("Light2D.Color");
    const bool color_changed = ImGui::ColorEdit4("Color", color);
    capture_property_interaction();
    if (color_changed) {
        light.color = {
            static_cast<std::uint8_t>(color[0] * 255.0f),
            static_cast<std::uint8_t>(color[1] * 255.0f),
            static_cast<std::uint8_t>(color[2] * 255.0f),
            static_cast<std::uint8_t>(color[3] * 255.0f)
        };
    }
    track(em, entity, color_before, "Light2D.Color", "Edit Light Color", context);

    const Light2DComponent intensity_before = light;
    property_float("Intensity", &light.intensity, Policies::Lighting::Intensity);
    track(em, entity, intensity_before, "Light2D.Intensity", "Edit Light Intensity", context);

    if (light.type == Light2DType::Point) {
        const Light2DComponent radius_before = light;
        property_float("Radius", &light.radius, Policies::Lighting::Radius);
        track(em, entity, radius_before, "Light2D.Radius", "Edit Light Radius", context);

        const Light2DComponent falloff_before = light;
        property_float("Falloff", &light.falloff, Policies::Lighting::Falloff);
        track(em, entity, falloff_before, "Light2D.Falloff", "Edit Light Falloff", context);
    }

    const char* const blend_labels[] = {"Additive", "Multiply", "Screen"};
    int blend = static_cast<int>(light.blend_mode);
    const Light2DComponent blend_before = light;
    if (property_enum("Blend Mode", &blend, blend_labels, 3)) {
        light.blend_mode = static_cast<Light2DBlendMode>(blend);
    }
    track(em, entity, blend_before, "Light2D.BlendMode", "Change Light Blend Mode", context,
          blend_before.blend_mode != light.blend_mode);

    const Light2DComponent layers_before = light;
    int layers = static_cast<int>(light.affected_layers);
    if (property_int("Affected Layers", &layers, Policies::Lighting::RenderLayerMask)) {
        light.affected_layers = static_cast<RenderLayerMask>(layers);
    }
    track(em, entity, layers_before, "Light2D.AffectedLayers", "Edit Light Layers", context);

    if (light.type == Light2DType::Point) {
        const Light2DComponent shadows_before = light;
        const bool shadows_changed = property_bool("Cast Shadows", &light.casts_shadows);
        track(em, entity, shadows_before, "Light2D.CastShadows", "Toggle Light Shadows", context, shadows_changed);
    }

    const Light2DComponent enabled_before = light;
    const bool enabled_changed = property_bool("Enabled", &light.enabled);
    track(em, entity, enabled_before, "Light2D.Enabled", "Toggle Light", context, enabled_changed);
    ImGui::PopID();
}

void LightingInspectorDrawer::draw_shadow_caster(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<ShadowCaster2DComponent>(entity)) return;
    bool open = false;
    if (handle_header<ShadowCaster2DComponent>(
            em, entity, "shadow_caster_2d", "Remove Shadow Caster 2D", context, open
        ) || !open) return;

    ImGui::PushID("ShadowCaster2DComponentDrawer");
    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    auto& caster = em.get_component<ShadowCaster2DComponent>(entity);

    const char* const source_labels[] = {"Collider", "Static Polygon"};
    int source = static_cast<int>(caster.source);
    const ShadowCaster2DComponent source_before = caster;
    if (property_enum("Geometry", &source, source_labels, 2)) {
        caster.source = static_cast<ShadowGeometrySource>(source);
    }
    track(em, entity, source_before, "ShadowCaster2D.Source", "Change Shadow Geometry Source", context,
          source_before.source != caster.source);

    const ShadowCaster2DComponent layers_before = caster;
    int layers = static_cast<int>(caster.affected_layers);
    if (property_int("Affected Layers", &layers, Policies::Lighting::RenderLayerMask)) {
        caster.affected_layers = static_cast<RenderLayerMask>(layers);
    }
    track(em, entity, layers_before, "ShadowCaster2D.AffectedLayers", "Edit Shadow Caster Layers", context);

    const ShadowCaster2DComponent enabled_before = caster;
    const bool enabled_changed = property_bool("Enabled", &caster.enabled);
    track(em, entity, enabled_before, "ShadowCaster2D.Enabled", "Toggle Shadow Caster", context, enabled_changed);

    if (caster.source == ShadowGeometrySource::Collider) {
        if (!em.has_component<ColliderComponent>(entity)) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Add Collider 2D to provide shadow geometry.");
        } else {
            ImGui::TextDisabled("Uses every authored collider shape.");
        }
    } else {
        ImGui::SeparatorText("Local Polygon");
        for (std::size_t index = 0; index < caster.vertices.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            const ShadowCaster2DComponent vertex_before = caster;
            float value[2] = {caster.vertices[index].x, caster.vertices[index].y};
            if (property_vec2("Vertex", value, Policies::Lighting::Coordinate)) {
                caster.vertices[index] = {value[0], value[1]};
            }
            const std::string key = "ShadowCaster2D.Vertex." + std::to_string(index);
            track(em, entity, vertex_before, key.c_str(), "Edit Shadow Vertex", context);
            ImGui::PopID();
        }

        if (ImGui::Button("Add Vertex") && context.command_history) {
            auto next = caster;
            next.vertices.push_back(next.vertices.empty() ? glm::vec2(0.0f) : next.vertices.back());
            context.command_history->execute_command(std::make_unique<SetComponentCommand<ShadowCaster2DComponent>>(
                &em, entity, caster, next, "Add Shadow Vertex"
            ));
            ImGui::PopID();
            return;
        }
        ImGui::SameLine();
        const bool can_remove = caster.vertices.size() > 3;
        if (!can_remove) ImGui::BeginDisabled();
        if (ImGui::Button("Remove Last") && context.command_history) {
            auto next = caster;
            next.vertices.pop_back();
            context.command_history->execute_command(std::make_unique<SetComponentCommand<ShadowCaster2DComponent>>(
                &em, entity, caster, next, "Remove Shadow Vertex"
            ));
            if (!can_remove) ImGui::EndDisabled();
            ImGui::PopID();
            return;
        }
        if (!can_remove) ImGui::EndDisabled();
    }
    ImGui::PopID();
}

} // namespace Engine
