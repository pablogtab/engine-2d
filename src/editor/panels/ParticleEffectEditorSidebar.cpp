#include "ParticleEffectEditorPanel.hpp"

#include "../EditorTheme.hpp"
#include "../inspector/InspectorWidgets.hpp"
#include "../../core/AssetDatabase.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <imgui.h>

namespace Engine {
namespace {

float channel_to_float(std::uint8_t channel) {
    return static_cast<float>(channel) / 255.0f;
}

std::uint8_t float_to_channel(float channel) {
    return static_cast<std::uint8_t>(
        std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f
    );
}

bool color_property(const char* label, SDL_Color& color) {
    float channels[4] = {
        channel_to_float(color.r),
        channel_to_float(color.g),
        channel_to_float(color.b),
        channel_to_float(color.a)
    };
    InspectorWidgets::begin_property_row(label);
    InspectorWidgets::begin_property_interaction(label);
    ImGui::PushID(label);
    const bool changed = ImGui::ColorEdit4(
        "##value", channels,
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf
    );
    InspectorWidgets::capture_property_interaction();
    ImGui::PopID();
    if (changed) {
        color = {
            float_to_channel(channels[0]),
            float_to_channel(channels[1]),
            float_to_channel(channels[2]),
            float_to_channel(channels[3])
        };
    }
    return changed;
}

bool settings_header(const char* label, bool default_open = false) {
    const auto& palette = EditorTheme::palette();
    const auto& metrics = EditorTheme::metrics();
    ImGui::PushStyleColor(ImGuiCol_Header, palette.header);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, palette.selected);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, palette.accent_active);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(7.0f * metrics.scale, 5.0f * metrics.scale)
    );
    const ImGuiTreeNodeFlags flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return open;
}

} // namespace

void ParticleEffectEditorPanel::render_settings(
    ParticleEffectAsset& asset,
    AssetDatabase& database
) {
    ImGui::BeginChild("ParticleSettings", ImVec2(0.0f, 0.0f), false);

    ImGui::PushID("General");
    if (settings_header("General", true)) {
        char name[128];
        std::snprintf(name, sizeof(name), "%s", asset.name.c_str());
        if (InspectorWidgets::property_text("Name", name, sizeof(name))) asset.name = name;

        InspectorWidgets::begin_property_row("Texture");
        const AssetRecord* texture_record = asset.texture_handle.is_valid()
            ? database.find(asset.texture_handle)
            : nullptr;
        if (ImGui::BeginCombo(
                "##particle_texture",
                texture_record ? texture_record->source_path.c_str() : "Default Soft Circle")) {
            if (ImGui::Selectable(
                    "Default Soft Circle", !asset.texture_handle.is_valid())) {
                asset.texture_handle = {};
            }
            for (const auto& record : database.records()) {
                if (record.type != AssetType::Texture || record.status != AssetStatus::Ready) {
                    continue;
                }
                const bool selected = record.handle == asset.texture_handle;
                if (ImGui::Selectable(record.source_path.c_str(), selected)) {
                    asset.texture_handle = record.handle;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();
    }
    ImGui::PopID();

    ImGui::PushID("Appearance");
    if (settings_header("Appearance", true)) {
        color_property("Start Color", asset.appearance.start_color);
        color_property("End Color", asset.appearance.end_color);
        InspectorWidgets::property_float(
            "Start Size", &asset.appearance.start_size, 0.2f, 0.1f, 200.0f, "%.1f px"
        );
        InspectorWidgets::property_float(
            "End Size", &asset.appearance.end_size, 0.2f, 0.0f, 200.0f, "%.1f px"
        );
        InspectorWidgets::property_float(
            "Size Variation", &asset.appearance.size_variation,
            0.1f, 0.0f, 100.0f, "%.1f px"
        );
        InspectorWidgets::property_float(
            "Start Stretch", &asset.appearance.start_stretch,
            0.05f, 0.0f, 20.0f, "%.2fx"
        );
        InspectorWidgets::property_float(
            "End Stretch", &asset.appearance.end_stretch,
            0.05f, 0.0f, 20.0f, "%.2fx"
        );
        ImGui::Spacing();
    }
    ImGui::PopID();

    ImGui::PushID("Motion");
    if (settings_header("Motion", true)) {
        float velocity[2] = {
            asset.motion.initial_velocity.x, asset.motion.initial_velocity.y
        };
        if (InspectorWidgets::property_vec2(
                "Velocity", velocity, 1.0f, -2000.0f, 2000.0f, "%.1f")) {
            asset.motion.initial_velocity = {velocity[0], velocity[1]};
        }

        float variation[2] = {
            asset.motion.velocity_variation.x, asset.motion.velocity_variation.y
        };
        if (InspectorWidgets::property_vec2(
                "Variation", variation, 1.0f, 0.0f, 2000.0f, "%.1f")) {
            asset.motion.velocity_variation = {variation[0], variation[1]};
        }

        float acceleration[2] = {asset.motion.acceleration.x, asset.motion.acceleration.y};
        if (InspectorWidgets::property_vec2(
                "Acceleration", acceleration, 1.0f, -2000.0f, 2000.0f, "%.1f")) {
            asset.motion.acceleration = {acceleration[0], acceleration[1]};
        }

        float gravity[2] = {asset.motion.gravity.x, asset.motion.gravity.y};
        if (InspectorWidgets::property_vec2(
                "Gravity", gravity, 1.0f, -2000.0f, 2000.0f, "%.1f")) {
            asset.motion.gravity = {gravity[0], gravity[1]};
        }

        InspectorWidgets::property_float(
            "Rotation", &asset.motion.initial_rotation,
            1.0f, -360.0f, 360.0f, "%.1f deg"
        );
        InspectorWidgets::property_float(
            "Rotation Var.", &asset.motion.rotation_variation,
            1.0f, 0.0f, 360.0f, "%.1f deg"
        );
        InspectorWidgets::property_float(
            "Rotation Speed", &asset.motion.rotation_speed,
            1.0f, -720.0f, 720.0f, "%.1f deg/s"
        );
        InspectorWidgets::property_float(
            "Speed Var.", &asset.motion.rotation_speed_variation,
            1.0f, 0.0f, 720.0f, "%.1f deg/s"
        );
        InspectorWidgets::property_bool("Align To Velocity", &asset.motion.align_to_velocity);
        if (asset.motion.align_to_velocity) {
            InspectorWidgets::property_float(
                "Align Offset", &asset.motion.velocity_alignment_offset,
                1.0f, -360.0f, 360.0f, "%.1f deg"
            );
        }
        ImGui::Spacing();
    }
    ImGui::PopID();

    ImGui::PushID("Lifetime");
    if (settings_header("Lifetime")) {
        InspectorWidgets::property_float(
            "Duration", &asset.lifetime.duration, 0.05f, 0.05f, 60.0f, "%.2f s"
        );
        InspectorWidgets::property_float(
            "Variation", &asset.lifetime.variation, 0.05f, 0.0f, 30.0f, "%.2f s"
        );
        ImGui::Spacing();
    }
    ImGui::PopID();

    ImGui::PushID("Collision");
    if (settings_header("Collision")) {
        InspectorWidgets::property_bool("Enabled", &asset.collision.enabled);
        if (asset.collision.enabled) {
            InspectorWidgets::property_float(
                "Bounciness", &asset.collision.bounciness,
                0.01f, 0.0f, 1.0f, "%.2f"
            );
            int collision_mask = static_cast<int>(asset.collision.mask_bits);
            if (InspectorWidgets::property_int(
                    "Mask", &collision_mask, 1.0f, 0, 65535)) {
                asset.collision.mask_bits = static_cast<std::uint16_t>(collision_mask);
            }
        }
        ImGui::Spacing();
    }
    ImGui::PopID();

    ImGui::PushID("Emission");
    if (settings_header("Emission", true)) {
        InspectorWidgets::property_float(
            "Rate", &asset.emission.continuous_rate,
            0.5f, 0.0f, 500.0f, "%.1f /s"
        );
        InspectorWidgets::property_float(
            "Distance Rate", &asset.emission.distance_rate,
            0.01f, 0.0f, 10.0f, "%.2f /px"
        );
        InspectorWidgets::property_int(
            "Burst Count", &asset.emission.burst_count, 1.0f, 0, 1000
        );
        InspectorWidgets::property_float(
            "Duration", &asset.emission.duration,
            0.1f, 0.0f, 60.0f, "%.1f s"
        );
        InspectorWidgets::property_float(
            "Start Delay", &asset.emission.start_delay,
            0.05f, 0.0f, 10.0f, "%.2f s"
        );
        InspectorWidgets::property_bool("Loop", &asset.emission.loop);
        ImGui::Spacing();
    }
    ImGui::PopID();

    ImGui::PushID("Rendering");
    if (settings_header("Rendering")) {
        int layer = static_cast<int>(asset.rendering.render_layer);
        if (InspectorWidgets::property_int("Render Layer", &layer, 1.0f, 0, 31)) {
            asset.rendering.render_layer = static_cast<RenderLayer>(layer);
        }
        InspectorWidgets::property_float(
            "Z Layer", &asset.rendering.z_layer,
            0.01f, -1.0f, 1.0f, "%.3f"
        );
        int blend_mode = static_cast<int>(asset.rendering.blend_mode);
        const char* blend_modes[] = {"Alpha", "Additive"};
        if (InspectorWidgets::property_enum(
                "Blend Mode", &blend_mode, blend_modes, 2)) {
            asset.rendering.blend_mode = static_cast<ParticleBlendMode>(blend_mode);
        }
        ImGui::Spacing();
    }
    ImGui::PopID();

    ImGui::EndChild();
}

} // namespace Engine
