#include "AnimationInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../panels/SpriteAnimationStudioPanel.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../../renderer/Texture.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Engine {

namespace {

void reset_runtime(EntityManager& em, EntityID entity) {
    SpriteAnimationSystem::reset_playback(em, entity);
}

void track(
    EntityManager& em,
    EntityID entity,
    const AnimationComponent& before,
    const std::string& property,
    const char* description,
    const AnimationInspectorDrawer::Context& context,
    bool force_commit = false
) {
    track_component_edit<AnimationComponent>(
        em,
        entity,
        before,
        property.c_str(),
        description,
        {context.command_history, context.transactions},
        std::equal_to<AnimationComponent>{},
        reset_runtime,
        force_commit
    );
}

std::string clip_property(std::size_t index, const char* field) {
    return "Animation.Clip." + std::to_string(index) + "." + field;
}

void advance_preview(AnimationPreviewState& preview, const AnimationClip& clip, float delta_time) {
    if (!preview.playing || preview.finished) return;
    preview.timer += delta_time;
    const float frame_duration = 1.0f / std::max(clip.fps, 0.1f);
    while (preview.timer >= frame_duration && preview.playing) {
        preview.timer -= frame_duration;
        int relative = clip.custom_frames.empty()
            ? preview.current_frame - clip.start_frame
            : preview.current_frame;
        ++relative;
        if (relative >= clip.get_total_frames()) {
            if (clip.loop) {
                relative = 0;
            } else {
                relative = std::max(0, clip.get_total_frames() - 1);
                preview.playing = false;
                preview.finished = true;
            }
        }
        preview.current_frame = clip.custom_frames.empty() ? clip.start_frame + relative : relative;
    }
}

void draw_preview(
    EntityManager& em,
    EntityID entity,
    const AnimationComponent& animation,
    AnimationPreviewState& preview
) {
    if (preview.entity != entity || !animation.get_clip(preview.clip_index)) {
        preview.reset(entity, animation);
    }
    const AnimationClip* clip = animation.get_clip(preview.clip_index);
    if (!clip) {
        ImGui::TextDisabled("Add a clip to preview the animation.");
        return;
    }

    advance_preview(preview, *clip, ImGui::GetIO().DeltaTime);
    if (em.has_component<SpriteComponent>(entity)) {
        Texture* texture = em.get_component<SpriteComponent>(entity).texture;
        if (texture && texture->get_sdl_texture()) {
            const SDL_FRect frame = animation.get_frame_rect(preview.current_frame, clip);
            const float width = static_cast<float>(std::max(1, texture->get_width()));
            const float height = static_cast<float>(std::max(1, texture->get_height()));
            const float aspect = frame.h > 0.0f ? frame.w / frame.h : 1.0f;
            const float preview_height = 72.0f;
            const float preview_width = std::clamp(preview_height * aspect, 32.0f, 144.0f);
            ImGui::Image(
                reinterpret_cast<ImTextureID>(texture->get_sdl_texture()),
                ImVec2(preview_width, preview_height),
                ImVec2(frame.x / width, frame.y / height),
                ImVec2((frame.x + frame.w) / width, (frame.y + frame.h) / height)
            );
        }
    }

    const std::string play_label = editor_icon_label(
        preview.playing ? EditorIcon::Pause : EditorIcon::Play,
        preview.playing ? "Pause" : "Preview"
    );
    if (ImGui::Button(play_label.c_str())) preview.playing = !preview.playing;
    ImGui::SameLine();
    const std::string reset_label = editor_icon_label(EditorIcon::Rotate, "Reset");
    if (ImGui::Button(reset_label.c_str())) preview.reset(entity, animation, preview.clip_index);
    ImGui::SameLine();
    ImGui::TextDisabled("Frame %d / %d", preview.current_frame, clip->get_total_frames());
}

} // namespace

void AnimationPreviewState::reset(EntityID target, const AnimationComponent& animation, int requested_clip) {
    entity = target;
    clip_index = requested_clip >= 0 ? requested_clip : animation.default_clip_index;
    clip_index = std::clamp(clip_index, 0, std::max(0, static_cast<int>(animation.clips.size()) - 1));
    const AnimationClip* clip = animation.get_clip(clip_index);
    current_frame = clip ? (clip->custom_frames.empty() ? clip->start_frame : 0) : 0;
    timer = 0.0f;
    playing = false;
    finished = false;
}

void AnimationInspectorDrawer::draw(EntityManager& em, EntityID entity, const Context& context) {
    if (!em.has_component<AnimationComponent>(entity)) return;

    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, "animation");
    const auto header = InspectorWidgets::registered_component_header("animation", true, true, has_override);
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, "animation", nullptr
        ));
        return;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, "animation", nullptr
        ));
        return;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<AnimationComponent>>(
            &em,
            entity,
            em.get_component<AnimationComponent>(entity),
            std::nullopt,
            "Remove Sprite Animator 2D",
            reset_runtime
        ));
        return;
    }
    if (!header.open) return;


    using namespace InspectorWidgets;
    auto& animation = em.get_component<AnimationComponent>(entity);

    if (context.studio) {
        const std::string studio_label = editor_icon_label(EditorIcon::ExternalLink, "Open in Animation Studio");
        if (ImGui::Button(studio_label.c_str(), ImVec2(-1.0f, 0.0f))) context.studio->open_for_entity(entity);
    }

    ImGui::TextDisabled("SPRITESHEET GRID");
    const AnimationComponent columns_before = animation;
    property_int("Columns", &animation.columns, InspectorPropertyPolicies::Animation::GridDimension);
    track(em, entity, columns_before, "Animation.Columns", "Edit Animation Columns", context);

    const AnimationComponent rows_before = animation;
    property_int("Rows", &animation.rows, InspectorPropertyPolicies::Animation::GridDimension);
    track(em, entity, rows_before, "Animation.Rows", "Edit Animation Rows", context);

    const AnimationComponent width_before = animation;
    property_float(
        "Frame Width", &animation.frame_width, InspectorPropertyPolicies::Animation::FrameSize
    );
    track(em, entity, width_before, "Animation.FrameWidth", "Edit Animation Frame Width", context);

    const AnimationComponent height_before = animation;
    property_float(
        "Frame Height", &animation.frame_height, InspectorPropertyPolicies::Animation::FrameSize
    );
    track(em, entity, height_before, "Animation.FrameHeight", "Edit Animation Frame Height", context);

    float spacing[2] = {animation.spacing_x, animation.spacing_y};
    const AnimationComponent spacing_before = animation;
    if (property_vec2("Spacing", spacing, InspectorPropertyPolicies::Animation::GridPadding)) {
        animation.spacing_x = spacing[0];
        animation.spacing_y = spacing[1];
    }
    track(em, entity, spacing_before, "Animation.Spacing", "Edit Animation Spacing", context);

    float margin[2] = {animation.margin_x, animation.margin_y};
    const AnimationComponent margin_before = animation;
    if (property_vec2("Margin", margin, InspectorPropertyPolicies::Animation::GridPadding)) {
        animation.margin_x = margin[0];
        animation.margin_y = margin[1];
    }
    track(em, entity, margin_before, "Animation.Margin", "Edit Animation Margin", context);

    const AnimationComponent play_on_start_before = animation;
    property_bool("Play On Start", &animation.play_on_start);
    track(em, entity, play_on_start_before, "Animation.PlayOnStart", "Toggle Animation Play On Start", context);

    if (!animation.clips.empty()) {
        std::vector<const char*> clip_names;
        clip_names.reserve(animation.clips.size());
        for (const auto& clip : animation.clips) clip_names.push_back(clip.name.c_str());
        int default_clip = std::clamp(
            animation.default_clip_index,
            0,
            static_cast<int>(animation.clips.size()) - 1
        );
        const AnimationComponent default_before = animation;
        begin_property_row("Default Clip");
        begin_property_interaction("Animation.DefaultClip");
        const bool default_changed = ImGui::Combo(
            "##default_clip",
            &default_clip,
            clip_names.data(),
            static_cast<int>(clip_names.size())
        );
        capture_property_interaction();
        if (default_changed) animation.default_clip_index = default_clip;
        track(
            em,
            entity,
            default_before,
            "Animation.DefaultClip",
            "Change Default Animation Clip",
            context,
            default_changed
        );
    }

    ImGui::Separator();
    ImGui::TextDisabled("CLIPS");
    int clip_to_remove = -1;
    for (std::size_t index = 0; index < animation.clips.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        auto& clip = animation.clips[index];

        char name[96];
        std::strncpy(name, clip.name.c_str(), sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        const AnimationComponent name_before = animation;
        begin_property_row("Name");
        begin_property_interaction("Clip.Name");
        const bool name_changed = ImGui::InputText("##name", name, sizeof(name));
        capture_property_interaction();
        if (name_changed) clip.name = name;
        track(em, entity, name_before, clip_property(index, "Name"), "Rename Animation Clip", context);

        const AnimationComponent start_before = animation;
        property_int(
            "Start Frame",
            &clip.start_frame,
            InspectorPropertyPolicies::bounded_int(
                1.0f, 0, std::max(0, animation.columns * animation.rows - 1)
            )
        );
        track(em, entity, start_before, clip_property(index, "Start"), "Edit Clip Start Frame", context);

        const AnimationComponent count_before = animation;
        property_int(
            "Frame Count",
            &clip.frame_count,
            InspectorPropertyPolicies::bounded_int(
                1.0f, 1, std::max(1, animation.columns * animation.rows)
            )
        );
        track(em, entity, count_before, clip_property(index, "Count"), "Edit Clip Frame Count", context);

        const AnimationComponent fps_before = animation;
        property_float(
            "Speed", &clip.fps, InspectorPropertyPolicies::Animation::FramesPerSecond
        );
        track(em, entity, fps_before, clip_property(index, "FPS"), "Edit Clip Speed", context);

        const AnimationComponent loop_before = animation;
        property_bool("Loop", &clip.loop);
        track(em, entity, loop_before, clip_property(index, "Loop"), "Toggle Clip Loop", context);

        if (editor_icon_button("RemoveClip", EditorIcon::Trash, "Remove clip")) {
            clip_to_remove = static_cast<int>(index);
        }
        if (context.preview && context.preview->clip_index == static_cast<int>(index)) {
            ImGui::SameLine();
            ImGui::TextDisabled("Previewing");
        } else if (context.preview) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Preview")) context.preview->reset(entity, animation, static_cast<int>(index));
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    if (clip_to_remove >= 0) {
        const AnimationComponent before = animation;
        animation.clips.erase(animation.clips.begin() + clip_to_remove);
        animation.default_clip_index = std::clamp(
            animation.default_clip_index,
            0,
            std::max(0, static_cast<int>(animation.clips.size()) - 1)
        );
        track(em, entity, before, "Animation.RemoveClip", "Remove Animation Clip", context, true);
        if (context.preview) context.preview->reset(entity, animation);
    }

    const std::string add_label = editor_icon_label(EditorIcon::Plus, "Add Clip");
    if (ImGui::Button(add_label.c_str(), ImVec2(-1.0f, 0.0f))) {
        const AnimationComponent before = animation;
        animation.add_clip("New Clip", 0, 1, 10.0f, true);
        track(em, entity, before, "Animation.AddClip", "Add Animation Clip", context, true);
        if (context.preview) context.preview->reset(entity, animation, static_cast<int>(animation.clips.size()) - 1);
    }

    if (context.preview) {
        ImGui::Separator();
        ImGui::TextDisabled("EDITOR PREVIEW");
        draw_preview(em, entity, animation, *context.preview);
    }
}

} // namespace Engine
