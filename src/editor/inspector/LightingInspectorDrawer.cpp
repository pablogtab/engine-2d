#include "LightingInspectorDrawer.hpp"

#include "InspectorPropertyPolicies.hpp"
#include "InspectorWidgets.hpp"
#include "AssetDropTarget.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"
#include <imgui.h>
#include <cstdint>
#include <string>

namespace Engine {
namespace {

bool draw_texture_asset(
    const char* label,
    const char* id,
    AssetHandle& handle,
    Texture*& texture,
    SDL_Renderer* renderer,
    AssetManager* asset_manager
) {
    InspectorWidgets::begin_property_row(label);
    AssetDatabase& database = AssetDatabase::get();
    const AssetRecord* selected = handle.is_valid() ? database.find(handle) : nullptr;
    bool changed = false;
    if (ImGui::BeginCombo(id, selected ? selected->source_path.c_str() : "None")) {
        if (ImGui::Selectable("None", !handle.is_valid())) {
            handle = {};
            texture = nullptr;
            changed = true;
        }
        for (const AssetRecord& record : database.records()) {
            if (record.type != AssetType::Texture || record.status != AssetStatus::Ready) continue;
            const bool active = record.handle == handle;
            if (ImGui::Selectable(record.source_path.c_str(), active)) {
                handle = record.handle;
                texture = renderer && asset_manager
                    ? asset_manager->get_texture(renderer, record.source_path) : nullptr;
                changed = true;
            }
            if (active) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (const auto dropped = accept_asset_drop_target(database, AssetType::Texture)) {
        const AssetRecord* record = database.find(*dropped);
        if (record && *dropped != handle) {
            handle = *dropped;
            texture = renderer && asset_manager
                ? asset_manager->get_texture(renderer, record->source_path) : nullptr;
            changed = true;
        }
    }
    return changed;
}

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

    const char* const type_labels[] = {"Ambient", "Point", "Spot", "Directional", "Area"};
    int type = static_cast<int>(light.type);
    const Light2DComponent type_before = light;
    if (property_enum("Type", &type, type_labels, 5)) light.type = static_cast<Light2DType>(type);
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

    if (light.type != Light2DType::Ambient && light.type != Light2DType::Directional) {
        const Light2DComponent range_before = light;
        property_float("Range", &light.range, Policies::Lighting::Radius);
        track(em, entity, range_before, "Light2D.Range", "Edit Light Range", context);

        const Light2DComponent falloff_before = light;
        property_float("Falloff", &light.falloff, Policies::Lighting::Falloff);
        track(em, entity, falloff_before, "Light2D.Falloff", "Edit Light Falloff", context);
    }

    if (light.type == Light2DType::Spot) {
        const Light2DComponent before = light;
        property_float("Inner Angle", &light.spot_inner_angle, Policies::Lighting::Angle);
        property_float("Outer Angle", &light.spot_outer_angle, Policies::Lighting::Angle);
        track(em, entity, before, "Light2D.Spot", "Edit Spot Light", context);
    }
    if (light.type == Light2DType::Area) {
        const Light2DComponent before = light;
        float size[2] = {light.area_size.x, light.area_size.y};
        if (property_vec2("Area Size", size, Policies::Lighting::Coordinate)) light.area_size = {size[0], size[1]};
        track(em, entity, before, "Light2D.AreaSize", "Edit Area Light Size", context);
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

    const char* const mobility_labels[] = {"Realtime", "Baked"};
    int mobility = static_cast<int>(light.mobility);
    const Light2DComponent mobility_before = light;
    if (property_enum("Mobility", &mobility, mobility_labels, 2)) light.mobility = static_cast<Light2DMobility>(mobility);
    track(em, entity, mobility_before, "Light2D.Mobility", "Change Light Mobility", context);
    if (light.mobility == Light2DMobility::Baked) {
        ImGui::TextDisabled("Baked light: changes are used by the next bake, not the realtime preview.");
    } else {
        ImGui::TextDisabled("Realtime light: changes are visible immediately in the viewport and Play.");
    }

    const char* const shadow_labels[] = {"None", "Hard", "Soft Fast", "Contact Hardening"};
    int shadow_mode = static_cast<int>(light.shadow_mode);
    const Light2DComponent shadow_before = light;
    if (property_enum("Shadow Mode", &shadow_mode, shadow_labels, 4)) light.shadow_mode = static_cast<ShadowMode>(shadow_mode);
    if (light.shadow_mode != ShadowMode::None) {
        property_float("Shadow Strength", &light.shadow_strength, Policies::Lighting::Intensity);
        property_float("Shadow Softness", &light.shadow_softness, Policies::Lighting::Intensity);
        property_float("Shadow Bias", &light.shadow_bias, Policies::Lighting::Coordinate);
        property_float("Shadow Max Distance", &light.shadow_max_distance, Policies::Lighting::Radius);
        int priority = light.shadow_priority;
        if (property_int("Shadow Priority", &priority, Policies::bounded_int(1.0f, -32768, 32767))) {
            light.shadow_priority = static_cast<std::int16_t>(priority);
        }
    }
    track(em, entity, shadow_before, "Light2D.Shadows", "Edit Light Shadows", context);

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

    const char* const source_labels[] = {"Collider", "Static Polygon", "Sprite Alpha Contour"};
    int source = static_cast<int>(caster.source);
    const ShadowCaster2DComponent source_before = caster;
    if (property_enum("Geometry", &source, source_labels, 3)) {
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

    const char* const mobility_labels[] = {"Realtime", "Baked"};
    int mobility = static_cast<int>(caster.mobility);
    const ShadowCaster2DComponent settings_before = caster;
    if (property_enum("Mobility", &mobility, mobility_labels, 2)) caster.mobility = static_cast<ShadowCasterMobility>(mobility);
    property_float("Height", &caster.height, Policies::Lighting::Coordinate);
    property_float("Opacity", &caster.opacity, Policies::Lighting::Intensity);
    if (caster.source == ShadowGeometrySource::SpriteAlphaContour) {
        property_float("Alpha Threshold", &caster.alpha_threshold, Policies::Lighting::Intensity);
        property_float("Contour Tolerance", &caster.contour_tolerance, Policies::Lighting::Coordinate);
    }
    track(em, entity, settings_before, "ShadowCaster2D.Settings", "Edit Shadow Caster Settings", context);

    const ShadowCaster2DComponent enabled_before = caster;
    const bool enabled_changed = property_bool("Enabled", &caster.enabled);
    track(em, entity, enabled_before, "ShadowCaster2D.Enabled", "Toggle Shadow Caster", context, enabled_changed);

    if (caster.source == ShadowGeometrySource::Collider) {
        if (!em.has_component<ColliderComponent>(entity)) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Add Collider 2D to provide shadow geometry.");
        } else {
            ImGui::TextDisabled("Uses every authored collider shape.");
        }
    } else if (caster.source == ShadowGeometrySource::StaticPolygon) {
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
    } else ImGui::TextDisabled("Uses the sprite alpha contour cached by the renderer.");
    ImGui::PopID();
}

void LightingInspectorDrawer::draw_sprite_lighting(EntityManager& em, EntityID entity, const SpriteLightingContext& context) {
    if (!em.has_component<SpriteLighting2DComponent>(entity)) return;
    bool open = false;
    if (handle_header<SpriteLighting2DComponent>(em, entity, "sprite_lighting_2d", "Remove Sprite Lighting 2D", context.transaction, open) || !open) return;
    auto& value = em.get_component<SpriteLighting2DComponent>(entity);
    const auto before = value;
    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    const bool custom_material = em.has_component<SpriteComponent>(entity) &&
        static_cast<bool>(em.get_component<SpriteComponent>(entity).material);
    if (custom_material) {
        ImGui::TextDisabled("Realtime maps are disabled while the Sprite uses a custom material.");
        ImGui::BeginDisabled();
    }
    const bool normal_changed = draw_texture_asset(
        "Normal Map (Realtime)", "##normal_map", value.normal_map_handle, value.normal_map,
        context.renderer, context.asset_manager
    );
    const bool emissive_changed = draw_texture_asset(
        "Emissive Map (Realtime)", "##emissive_map", value.emissive_map_handle, value.emissive_map,
        context.renderer, context.asset_manager
    );
    const bool specular_changed = draw_texture_asset(
        "Specular Map (Realtime)", "##specular_map", value.specular_map_handle, value.specular_map,
        context.renderer, context.asset_manager
    );
    property_float("Normal Strength", &value.normal_strength, Policies::unbounded_float(0.02f));
    property_float("Emissive Intensity", &value.emissive_intensity, Policies::unbounded_float(0.02f));
    property_float("Surface Height", &value.surface_height, Policies::Lighting::Coordinate);
    property_float("Specular Strength", &value.specular_strength, Policies::unbounded_float(0.02f));
    property_float("Shininess", &value.shininess, Policies::unbounded_float(0.25f));
    property_bool("Receives Lighting", &value.receives_lighting);
    if (custom_material) ImGui::EndDisabled();
    property_bool("Receives Shadows", &value.receives_shadows);
    ImGui::TextDisabled("Receives Shadows uses the realtime per-layer receiver mask.");
    property_bool("Baked Receiver", &value.baked_receiver);
    ImGui::TextDisabled("Realtime maps are packed on demand. Baked Receiver affects the next valid lighting bake.");
    property_bool("Enabled", &value.enabled);
    track(em, entity, before, "SpriteLighting2D", "Edit Sprite Lighting 2D", context.transaction,
          normal_changed || emissive_changed || specular_changed);
}

void LightingInspectorDrawer::draw_sprite_shadow(EntityManager& em, EntityID entity, const ComponentTransactionContext& context) {
    if (!em.has_component<SpriteShadow2DComponent>(entity)) return;
    bool open = false;
    if (handle_header<SpriteShadow2DComponent>(em, entity, "sprite_shadow_2d", "Remove Sprite Shadow 2D", context, open) || !open) return;
    auto& value = em.get_component<SpriteShadow2DComponent>(entity);
    const auto before = value;
    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    const char* modes[] = {"Fixed Contact", "Dominant Light"}; int mode = static_cast<int>(value.mode);
    if (property_enum("Mode", &mode, modes, 2)) value.mode = static_cast<SpriteShadowMode>(mode);
    const char* shapes[] = {"Ellipse", "Texture"}; int shape = static_cast<int>(value.shape);
    if (property_enum("Shape", &shape, shapes, 2)) value.shape = static_cast<SpriteShadowShape>(shape);
    float offset[2] = {value.offset.x, value.offset.y}; if (property_vec2("Offset", offset, Policies::Lighting::Coordinate)) value.offset = {offset[0], offset[1]};
    float size[2] = {value.size.x, value.size.y}; if (property_vec2("Size", size, Policies::Lighting::Coordinate)) value.size = {size[0], size[1]};
    property_float("Opacity", &value.opacity, Policies::Lighting::Intensity);
    property_float("Softness", &value.softness, Policies::Lighting::Intensity);
    property_float("Elevation", &value.elevation, Policies::Lighting::Coordinate);
    property_float("Max Projection", &value.max_projection, Policies::Lighting::Radius);
    property_bool("Enabled", &value.enabled);
    track(em, entity, before, "SpriteShadow2D", "Edit Sprite Shadow 2D", context);
}

void LightingInspectorDrawer::draw_bake_volume(EntityManager& em, EntityID entity, const ComponentTransactionContext& context) {
    if (!em.has_component<LightingBakeVolume2DComponent>(entity)) return;
    bool open = false;
    if (handle_header<LightingBakeVolume2DComponent>(em, entity, "lighting_bake_volume_2d", "Remove Lighting Bake Volume", context, open) || !open) return;
    auto& value = em.get_component<LightingBakeVolume2DComponent>(entity);
    const auto before = value;
    using namespace InspectorWidgets;
    namespace Policies = InspectorPropertyPolicies;
    float size[2] = {value.size.x, value.size.y}; if (property_vec2("Size", size, Policies::Lighting::Coordinate)) value.size = {size[0], size[1]};
    property_float("Texels Per Unit", &value.texels_per_unit, Policies::unbounded_float(0.05f));
    int layers = static_cast<int>(value.affected_layers); if (property_int("Affected Layers", &layers, Policies::Lighting::RenderLayerMask)) value.affected_layers = static_cast<RenderLayerMask>(layers);
    property_bool("Enabled", &value.enabled);
    track(em, entity, before, "LightingBakeVolume2D", "Edit Lighting Bake Volume", context);
}

} // namespace Engine
