#include "PostProcessTunerPanel.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/PostProcessProfileCommands.hpp"
#include "../../renderer/postprocess/PostProcessPipeline.hpp"
#include "../../scene/Scene.hpp"
#include "../../scene/SceneManager.hpp"
#include <algorithm>
#include <filesystem>
#include <imgui.h>

namespace Engine {

namespace {

constexpr const char* CUSTOM_PROFILE_PATH = "assets/presets/custom_postfx.json";

float channel_to_float(uint8_t channel) {
    return static_cast<float>(channel) / 255.0f;
}

uint8_t float_to_channel(float channel) {
    return static_cast<uint8_t>(std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f);
}

} // namespace

PostProcessTunerPanel::PostProcessTunerPanel(
    SceneManager* scene_manager,
    std::shared_ptr<EditorCommandHistory> command_history
)
    : m_scene_manager(scene_manager),
      m_command_history(std::move(command_history)) {}

void PostProcessTunerPanel::apply_immediate(
    PostProcessProfile* profile,
    PostProcessPipeline* pipeline,
    PostProcessProfile next
) {
    if (!profile || !pipeline || !m_command_history) return;
    m_command_history->execute_command(std::make_unique<ModifyPostProcessProfileCommand>(
        profile, pipeline, *profile, std::move(next)
    ));
}

void PostProcessTunerPanel::track_property_interaction(
    PostProcessProfile* profile,
    PostProcessPipeline* pipeline,
    const PostProcessProfile& before,
    bool changed
) {
    if (!profile || !pipeline || !m_command_history) return;

    if (ImGui::IsItemActivated()) {
        m_interaction_target = profile;
        m_interaction_before = before;
    }

    if (changed) {
        profile->apply_to_pipeline(*pipeline);
    }

    if (ImGui::IsItemDeactivatedAfterEdit() &&
        m_interaction_target == profile &&
        m_interaction_before.has_value()) {
        m_command_history->push_executed_command(std::make_unique<ModifyPostProcessProfileCommand>(
            profile, pipeline, *m_interaction_before, *profile
        ));
        m_interaction_target = nullptr;
        m_interaction_before.reset();
    }
}

void PostProcessTunerPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(360, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    Scene* scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    PostProcessPipeline* pipeline = scene ? scene->get_post_process_pipeline() : nullptr;
    PostProcessProfile* profile = scene ? scene->get_editable_post_process_profile() : nullptr;

    if (!pipeline || !profile) {
        ImGui::TextDisabled("The active scene does not expose editable post-processing.");
        ImGui::TextWrapped("Scenes opt in through the common Scene interface; this panel is not tied to a specific demo type.");
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "%s", profile->name.c_str());
    ImGui::TextDisabled("Base profile. Runtime volumes blend over these values.");
    ImGui::Separator();

    if (ImGui::Button("Default")) apply_immediate(profile, pipeline, PostProcessProfile::PresetDefault());
    ImGui::SameLine();
    if (ImGui::Button("Cinematic")) apply_immediate(profile, pipeline, PostProcessProfile::PresetCinematic());
    ImGui::SameLine();
    if (ImGui::Button("Retro CRT")) apply_immediate(profile, pipeline, PostProcessProfile::PresetRetroCRT());
    if (ImGui::Button("Underwater")) apply_immediate(profile, pipeline, PostProcessProfile::PresetUnderwater());
    ImGui::SameLine();
    if (ImGui::Button("Night Vision")) apply_immediate(profile, pipeline, PostProcessProfile::PresetNightVision());
    ImGui::SameLine();
    if (ImGui::Button("Dreamy")) apply_immediate(profile, pipeline, PostProcessProfile::PresetDreamyBloom());

    if (ImGui::CollapsingHeader("Effect Reference", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Pixel-based effects are authored at this resolution.");

        PostProcessProfile before = *profile;
        bool changed = ImGui::DragInt("Width##effect_reference", &profile->effect_reference_width, 1.0f, 1, 16384);
        profile->effect_reference_width = std::max(1, profile->effect_reference_width);
        track_property_interaction(profile, pipeline, before, changed);

        before = *profile;
        changed = ImGui::DragInt("Height##effect_reference", &profile->effect_reference_height, 1.0f, 1, 16384);
        profile->effect_reference_height = std::max(1, profile->effect_reference_height);
        track_property_interaction(profile, pipeline, before, changed);

        const PostProcessContext& context = pipeline->get_context();
        ImGui::TextDisabled(
            "Output %d x %d | Effect scale %.2fx",
            context.output_width,
            context.output_height,
            context.pixel_scale()
        );
    }

    if (ImGui::CollapsingHeader("Vignette", ImGuiTreeNodeFlags_DefaultOpen)) {
        PostProcessProfile before = *profile;
        bool changed = ImGui::Checkbox("Enabled##vignette", &profile->vignette_enabled);
        track_property_interaction(profile, pipeline, before, changed);

        before = *profile;
        changed = ImGui::SliderFloat("Intensity##vignette", &profile->vignette_intensity, 0.0f, 1.0f, "%.2f");
        track_property_interaction(profile, pipeline, before, changed);

        float color[4] = {
            channel_to_float(profile->vignette_color.r), channel_to_float(profile->vignette_color.g),
            channel_to_float(profile->vignette_color.b), channel_to_float(profile->vignette_color.a)
        };
        before = *profile;
        changed = ImGui::ColorEdit4("Color##vignette", color);
        if (changed) {
            profile->vignette_color = SDL_Color{
                float_to_channel(color[0]), float_to_channel(color[1]),
                float_to_channel(color[2]), float_to_channel(color[3])
            };
        }
        track_property_interaction(profile, pipeline, before, changed);
    }

    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
        PostProcessProfile before = *profile;
        bool changed = ImGui::Checkbox("Enabled##bloom", &profile->bloom_enabled);
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::SliderFloat("Intensity##bloom", &profile->bloom_intensity, 0.0f, 2.0f, "%.2f");
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::SliderFloat("Radius##bloom", &profile->bloom_radius, 1.0f, 12.0f, "%.1f px");
        track_property_interaction(profile, pipeline, before, changed);
    }

    if (ImGui::CollapsingHeader("Chromatic Aberration")) {
        PostProcessProfile before = *profile;
        bool changed = ImGui::Checkbox("Enabled##chromatic", &profile->chromatic_enabled);
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::SliderFloat("Offset##chromatic", &profile->chromatic_offset, 0.0f, 15.0f, "%.1f px");
        track_property_interaction(profile, pipeline, before, changed);
    }

    if (ImGui::CollapsingHeader("Gaussian Blur")) {
        PostProcessProfile before = *profile;
        bool changed = ImGui::Checkbox("Enabled##blur", &profile->blur_enabled);
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::SliderFloat("Radius##blur", &profile->blur_radius, 0.0f, 6.0f, "%.1f px");
        track_property_interaction(profile, pipeline, before, changed);
    }

    if (ImGui::CollapsingHeader("Color Grading")) {
        PostProcessProfile before = *profile;
        bool changed = ImGui::Checkbox("Enabled##color_grading", &profile->color_grading_enabled);
        track_property_interaction(profile, pipeline, before, changed);

        float tint[4] = {
            channel_to_float(profile->color_tint.r), channel_to_float(profile->color_tint.g),
            channel_to_float(profile->color_tint.b), channel_to_float(profile->color_tint.a)
        };
        before = *profile;
        changed = ImGui::ColorEdit4("Tint", tint);
        if (changed) {
            profile->color_tint = SDL_Color{
                float_to_channel(tint[0]), float_to_channel(tint[1]),
                float_to_channel(tint[2]), float_to_channel(tint[3])
            };
        }
        track_property_interaction(profile, pipeline, before, changed);

        float shadow_tint[4] = {
            channel_to_float(profile->shadow_tint.r), channel_to_float(profile->shadow_tint.g),
            channel_to_float(profile->shadow_tint.b), channel_to_float(profile->shadow_tint.a)
        };
        before = *profile;
        changed = ImGui::ColorEdit4("Shadow Tint", shadow_tint);
        if (changed) {
            profile->shadow_tint = SDL_Color{
                float_to_channel(shadow_tint[0]), float_to_channel(shadow_tint[1]),
                float_to_channel(shadow_tint[2]), float_to_channel(shadow_tint[3])
            };
        }
        track_property_interaction(profile, pipeline, before, changed);
    }

    if (ImGui::CollapsingHeader("Film Grain and Scanlines")) {
        PostProcessProfile before = *profile;
        bool changed = ImGui::Checkbox("Film Grain##enabled", &profile->film_grain_enabled);
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::SliderFloat("Film Grain##intensity", &profile->film_grain_intensity, 0.0f, 1.0f, "%.2f");
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::Checkbox("CRT##enabled", &profile->crt_enabled);
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::SliderFloat("Scanline Intensity", &profile->crt_scanline_intensity, 0.0f, 1.0f, "%.2f");
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::SliderFloat("Scanline Spacing", &profile->crt_scanline_spacing, 2.0f, 8.0f, "%.1f px");
        track_property_interaction(profile, pipeline, before, changed);
        before = *profile;
        changed = ImGui::SliderFloat("CRT Flicker", &profile->crt_flicker, 0.0f, 0.2f, "%.3f");
        track_property_interaction(profile, pipeline, before, changed);
    }

    ImGui::Separator();
    if (ImGui::Button("Save Profile")) {
        std::filesystem::create_directories("assets/presets");
        profile->save_to_file(CUSTOM_PROFILE_PATH);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Profile")) {
        PostProcessProfile loaded = *profile;
        if (loaded.load_from_file(CUSTOM_PROFILE_PATH)) {
            apply_immediate(profile, pipeline, std::move(loaded));
        }
    }

    ImGui::End();
}

} // namespace Engine
