#include "SpriteAnimationStudioPanel.hpp"

#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../renderer/Texture.hpp"
#include "../EditorTheme.hpp"
#include "../inspector/InspectorWidgets.hpp"
#include "../ui/EditorUi.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <string>
#include <vector>

namespace Engine {

void SpriteAnimationStudioPanel::render_preview_viewport(
    EntityManager& em,
    EntityID entity
) {
    auto& animation = draft_animation(em, entity);
    const auto& sprite = em.get_component<SpriteComponent>(entity);
    Texture* texture = sprite.texture;
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();

    const AnimationClip* clip = animation.get_clip(m_preview_clip_index);
    if (!clip) {
        reset_preview(entity, animation);
        clip = animation.get_clip(m_preview_clip_index);
    }
    const int total_frames = clip ? std::max(1, clip->get_total_frames()) : 1;
    const std::string preview_detail = clip
        ? clip->name + "  |  " + std::to_string(total_frames) + " frames"
        : "No active clip";
    EditorUi::section_heading("LIVE PREVIEW", preview_detail.c_str());

    if (m_preview_playing && clip) {
        m_preview_timer += ImGui::GetIO().DeltaTime;
        const float frame_duration = 1.0f / std::max(0.1f, clip->fps);
        while (m_preview_timer >= frame_duration && m_preview_playing) {
            m_preview_timer -= frame_duration;
            int relative = clip->custom_frames.empty()
                ? m_preview_frame - clip->start_frame
                : m_preview_frame;
            if (clip->advance(relative, m_preview_direction)) {
                m_preview_playing = false;
                m_preview_finished = true;
            }
            m_preview_frame = clip->custom_frames.empty()
                ? clip->start_frame + relative
                : relative;
        }
    }

    const float preview_size = std::clamp(
        ImGui::GetContentRegionAvail().x,
        120.0f * metrics.scale,
        180.0f * metrics.scale
    );
    const float preview_offset = std::max(
        0.0f, (ImGui::GetContentRegionAvail().x - preview_size) * 0.5f
    );
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + preview_offset);
    const ImVec2 box_position = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 box_max(box_position.x + preview_size, box_position.y + preview_size);
    draw_list->AddRectFilled(
        box_position, box_max,
        ImGui::ColorConvertFloat4ToU32(palette.window), metrics.small_rounding
    );

    const float checker_size = 12.0f * metrics.scale;
    for (float y = 0.0f; y < preview_size; y += checker_size) {
        for (float x = 0.0f; x < preview_size; x += checker_size) {
            const int column = static_cast<int>(x / checker_size);
            const int row = static_cast<int>(y / checker_size);
            if ((column + row) % 2 == 0) continue;
            draw_list->AddRectFilled(
                ImVec2(box_position.x + x, box_position.y + y),
                ImVec2(
                    box_position.x + std::min(preview_size, x + checker_size),
                    box_position.y + std::min(preview_size, y + checker_size)
                ),
                ImGui::ColorConvertFloat4ToU32(palette.panel_raised)
            );
        }
    }

    if (texture && texture->get_sdl_texture() && clip) {
        const SDL_FRect frame = animation.get_frame_rect(m_preview_frame, clip);
        const float texture_width = static_cast<float>(texture->get_width());
        const float texture_height = static_cast<float>(texture->get_height());
        const ImVec2 uv_min(frame.x / texture_width, frame.y / texture_height);
        const ImVec2 uv_max(
            (frame.x + frame.w) / texture_width,
            (frame.y + frame.h) / texture_height
        );
        const float fit = std::min(
            preview_size * 0.88f / std::max(1.0f, frame.w),
            preview_size * 0.88f / std::max(1.0f, frame.h)
        );
        const float width = frame.w * fit * m_preview_zoom;
        const float height = frame.h * fit * m_preview_zoom;
        const float maximum_pan_x = std::max(0.0f, (width - preview_size) * 0.5f);
        const float maximum_pan_y = std::max(0.0f, (height - preview_size) * 0.5f);
        m_preview_pan.x = std::clamp(m_preview_pan.x, -maximum_pan_x, maximum_pan_x);
        m_preview_pan.y = std::clamp(m_preview_pan.y, -maximum_pan_y, maximum_pan_y);
        const ImVec2 image_min(
            box_position.x + (preview_size - width) * 0.5f + m_preview_pan.x,
            box_position.y + (preview_size - height) * 0.5f + m_preview_pan.y
        );
        draw_list->PushClipRect(box_position, box_max, true);
        draw_list->AddImage(
            reinterpret_cast<ImTextureID>(texture->get_sdl_texture()),
            image_min,
            ImVec2(image_min.x + width, image_min.y + height),
            uv_min,
            uv_max
        );
        draw_list->PopClipRect();
    }
    if (m_slicer_mode_freeform && m_show_center_guides) {
        const float center_x = (box_position.x + box_max.x) * 0.5f;
        const float center_y = (box_position.y + box_max.y) * 0.5f;
        const ImU32 guide_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(
                palette.accent_hovered.x,
                palette.accent_hovered.y,
                palette.accent_hovered.z,
                0.38f
            )
        );
        draw_list->AddLine(
            ImVec2(center_x, box_position.y), ImVec2(center_x, box_max.y), guide_color
        );
        draw_list->AddLine(
            ImVec2(box_position.x, center_y), ImVec2(box_max.x, center_y), guide_color
        );
    }
    draw_list->AddRect(
        box_position, box_max,
        ImGui::ColorConvertFloat4ToU32(palette.border), metrics.small_rounding
    );
    ImGui::InvisibleButton(
        "PreviewCanvas", ImVec2(preview_size, preview_size),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle
    );
    const bool preview_hovered = ImGui::IsItemHovered();
    const bool preview_panning = ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
        (ImGui::IsKeyDown(ImGuiKey_Space) &&
         ImGui::IsMouseDragging(ImGuiMouseButton_Left));
    if (ImGui::IsItemActive() && preview_panning) {
        m_preview_pan.x += ImGui::GetIO().MouseDelta.x;
        m_preview_pan.y += ImGui::GetIO().MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (preview_hovered && !ImGui::GetIO().KeyCtrl &&
        ImGui::GetIO().MouseWheel != 0.0f) {
        m_preview_zoom = std::clamp(
            m_preview_zoom * std::pow(1.15f, ImGui::GetIO().MouseWheel),
            0.25f,
            8.0f
        );
    }

    const float transport_width = (metrics.toolbar_height - 6.0f * metrics.scale) * 4.0f +
        ImGui::GetStyle().ItemSpacing.x * 3.0f;
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() +
        std::max(0.0f, (ImGui::GetContentRegionAvail().x - transport_width) * 0.5f)
    );
    if (EditorUi::toolbar_button(
            "animation_previous", EditorIcon::ArrowLeft, "Previous frame",
            false, clip != nullptr)) {
        m_preview_playing = false;
        const int relative = clip->custom_frames.empty()
            ? m_preview_frame - clip->start_frame
            : m_preview_frame;
        const int previous = (relative - 1 + total_frames) % total_frames;
        m_preview_frame = clip->custom_frames.empty()
            ? clip->start_frame + previous
            : previous;
        m_preview_direction = clip->initial_direction();
    }
    ImGui::SameLine();
    if (EditorUi::toolbar_button(
            "animation_play_pause",
            m_preview_playing ? EditorIcon::Pause : EditorIcon::Play,
            m_preview_playing ? "Pause preview" : "Play preview",
            m_preview_playing, clip != nullptr)) {
        m_preview_playing = !m_preview_playing;
        m_preview_finished = false;
    }
    ImGui::SameLine();
    if (EditorUi::toolbar_button(
            "animation_next", EditorIcon::StepForward, "Next frame",
            false, clip != nullptr)) {
        m_preview_playing = false;
        const int relative = clip->custom_frames.empty()
            ? m_preview_frame - clip->start_frame
            : m_preview_frame;
        const int next = (relative + 1) % total_frames;
        m_preview_frame = clip->custom_frames.empty()
            ? clip->start_frame + next
            : next;
        m_preview_direction = clip->initial_direction();
    }
    ImGui::SameLine();
    if (EditorUi::toolbar_button(
            "animation_stop", EditorIcon::Stop, "Stop and rewind",
            false, clip != nullptr)) {
        m_preview_playing = false;
        const int stopped_frame = clip->first_relative_frame();
        m_preview_frame = clip->custom_frames.empty()
            ? clip->start_frame + stopped_frame
            : stopped_frame;
        m_preview_direction = clip->initial_direction();
        m_preview_timer = 0.0f;
        m_preview_finished = false;
    }

    const int relative_frame = clip
        ? (clip->custom_frames.empty() ? m_preview_frame - clip->start_frame : m_preview_frame)
        : 0;
    ImGui::TextDisabled(
        "Frame %d / %d", std::clamp(relative_frame + 1, 1, total_frames), total_frames
    );
    ImGui::SameLine();
    ImGui::TextDisabled("Preview");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mouse wheel: zoom | Middle drag or Space + drag: pan");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##preview_zoom", &m_preview_zoom, 0.25f, 8.0f, "%.1fx");
}

void SpriteAnimationStudioPanel::render_animation_settings(
    EntityManager& em,
    EntityID entity
) {
    auto& animation = draft_animation(em, entity);
    ImGui::Spacing();
    EditorUi::section_heading("ANIMATION");

    const AnimationComponent play_before = animation;
    InspectorWidgets::property_bool("Play On Start", &animation.play_on_start);
    track_edit(
        em, entity, play_before,
        "AnimationStudio.PlayOnStart", "Toggle Animation Play On Start"
    );

    if (animation.clips.empty()) {
        ImGui::TextDisabled("Add a clip to choose the default animation.");
        return;
    }

    std::vector<const char*> clip_names;
    clip_names.reserve(animation.clips.size());
    for (const AnimationClip& item : animation.clips) clip_names.push_back(item.name.c_str());
    int default_clip = std::clamp(
        animation.default_clip_index, 0, static_cast<int>(animation.clips.size()) - 1
    );
    const AnimationComponent default_before = animation;
    const bool changed = InspectorWidgets::property_enum(
        "Default Clip", &default_clip, clip_names.data(), static_cast<int>(clip_names.size())
    );
    if (changed) animation.default_clip_index = default_clip;
    track_edit(
        em, entity, default_before,
        "AnimationStudio.DefaultClip", "Change Default Animation Clip", changed
    );
}

void SpriteAnimationStudioPanel::render_clip_tabs(EntityManager& em, EntityID entity) {
    auto& animation = draft_animation(em, entity);
    ImGui::Spacing();
    const std::string clip_count = std::to_string(animation.clips.size()) + " CLIPS";
    EditorUi::section_heading("CLIPS", clip_count.c_str());

    int remove_index = -1;
    if (ImGui::BeginTable(
            "AnimationClips", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Clip", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        for (std::size_t index = 0; index < animation.clips.size(); ++index) {
            const AnimationClip& item = animation.clips[index];
            const bool selected = m_preview_clip_index == static_cast<int>(index);
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string label = item.name + "  " +
                std::to_string(item.get_total_frames()) + "f";
            if (ImGui::Selectable(
                    label.c_str(), selected, ImGuiSelectableFlags_None,
                    ImVec2(0.0f, EditorTheme::metrics().row_height))) {
                reset_preview(entity, animation, static_cast<int>(index));
            }
            ImGui::TableSetColumnIndex(1);
            if (EditorUi::toolbar_button(
                    "remove_clip", EditorIcon::Trash, "Remove clip")) {
                remove_index = static_cast<int>(index);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (remove_index >= 0 && remove_index < static_cast<int>(animation.clips.size())) {
        const AnimationComponent before = animation;
        animation.clips.erase(animation.clips.begin() + remove_index);
        animation.default_clip_index = std::clamp(
            animation.default_clip_index,
            0,
            std::max(0, static_cast<int>(animation.clips.size()) - 1)
        );
        track_edit(
            em, entity, before,
            "AnimationStudio.RemoveClip", "Remove Animation Clip", true
        );
        reset_preview(entity, animation);
    }

    if (EditorUi::action_button(
            "add_animation_clip", EditorIcon::Plus, "Add Clip",
            EditorUi::ButtonTone::Neutral, true, ImVec2(-1.0f, 0.0f))) {
        const AnimationComponent before = animation;
        animation.add_clip("NewClip", 0, 4, 10.0f, true);
        track_edit(
            em, entity, before,
            "AnimationStudio.AddClip", "Add Animation Clip", true
        );
        reset_preview(entity, animation, static_cast<int>(animation.clips.size()) - 1);
    }

    AnimationClip* active_clip = animation.get_clip(m_preview_clip_index);
    if (!active_clip) return;

    ImGui::Spacing();
    EditorUi::section_heading("CLIP SETTINGS", active_clip->name.c_str());
    char name[64];
    std::strncpy(name, active_clip->name.c_str(), sizeof(name));
    name[sizeof(name) - 1] = '\0';
    const AnimationComponent name_before = animation;
    InspectorWidgets::begin_property_interaction("AnimationStudio.ClipName");
    if (InspectorWidgets::property_text("Name", name, sizeof(name))) active_clip->name = name;
    InspectorWidgets::capture_property_interaction();
    track_edit(
        em, entity, name_before,
        "AnimationStudio.ClipName", "Rename Animation Clip"
    );

    if (active_clip->custom_frames.empty()) {
        const int maximum_frame = std::max(0, animation.columns * animation.rows - 1);
        const AnimationComponent start_before = animation;
        const bool start_changed = InspectorWidgets::property_int(
            "Start Frame", &active_clip->start_frame,
            InspectorPropertyPolicies::bounded_int(1.0f, 0, maximum_frame)
        );
        track_edit(
            em, entity, start_before,
            "AnimationStudio.ClipStart", "Edit Animation Clip Start Frame"
        );
        if (start_changed) m_preview_frame = active_clip->start_frame;

        const AnimationComponent count_before = animation;
        InspectorWidgets::property_int(
            "Frame Count", &active_clip->frame_count,
            InspectorPropertyPolicies::bounded_int(
                1.0f, 1, std::max(1, animation.columns * animation.rows)
            )
        );
        track_edit(
            em, entity, count_before,
            "AnimationStudio.ClipCount", "Edit Animation Clip Frame Count"
        );
    } else {
        ImGui::TextDisabled("%zu custom frames", active_clip->custom_frames.size());
    }

    const AnimationComponent speed_before = animation;
    InspectorWidgets::property_float(
        "Speed", &active_clip->fps,
        InspectorPropertyPolicies::Animation::FramesPerSecond
    );
    track_edit(
        em, entity, speed_before,
        "AnimationStudio.ClipSpeed", "Edit Animation Clip Speed"
    );

    const char* playback_modes[]{"Once", "Loop", "Ping-Pong"};
    int playback_mode = active_clip->ping_pong ? 2 : (active_clip->loop ? 1 : 0);
    const AnimationComponent loop_before = animation;
    const bool mode_changed = InspectorWidgets::property_enum(
        "Playback", &playback_mode, playback_modes, 3
    );
    if (mode_changed) {
        active_clip->loop = playback_mode != 0;
        active_clip->ping_pong = playback_mode == 2;
    }
    track_edit(
        em, entity, loop_before,
        "AnimationStudio.ClipPlayback", "Change Animation Clip Playback", mode_changed
    );

    const AnimationComponent reverse_before = animation;
    const bool reverse_changed = InspectorWidgets::property_bool(
        "Reverse", &active_clip->reverse
    );
    track_edit(
        em, entity, reverse_before,
        "AnimationStudio.ClipReverse", "Toggle Reverse Animation", reverse_changed
    );
    if (mode_changed || reverse_changed) reset_preview(entity, animation, m_preview_clip_index);
}

} // namespace Engine
