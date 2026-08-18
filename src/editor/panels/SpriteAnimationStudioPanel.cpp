#include "SpriteAnimationStudioPanel.hpp"
#include "../../ecs/Components.hpp"
#include "../../renderer/Texture.hpp"
#include "../../core/Log.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../commands/EntityCommands.hpp"
#include "../inspector/ComponentInspectorTransaction.hpp"
#include "../inspector/InspectorWidgets.hpp"
#include <imgui.h>
#include <string>
#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

void reset_animation_runtime(EntityManager& em, EntityID entity) {
    SpriteAnimationSystem::reset_playback(em, entity);
}

} // namespace

SpriteAnimationStudioPanel::SpriteAnimationStudioPanel(
    SceneManager* scene_manager,
    std::shared_ptr<EditorSelection> selection
)
    : m_scene_manager(scene_manager),
      m_selection(selection) {
    is_open = false; // Closed by default
}

void SpriteAnimationStudioPanel::reset_preview(
    EntityID entity,
    const AnimationComponent& animation,
    int clip_index
) {
    m_preview_entity = entity;
    m_preview_clip_index = clip_index >= 0 ? clip_index : animation.default_clip_index;
    m_preview_clip_index = std::clamp(
        m_preview_clip_index,
        0,
        std::max(0, static_cast<int>(animation.clips.size()) - 1)
    );
    const AnimationClip* clip = animation.get_clip(m_preview_clip_index);
    m_preview_frame = clip ? (clip->custom_frames.empty() ? clip->start_frame : 0) : 0;
    m_preview_timer = 0.0f;
    m_preview_playing = false;
    m_preview_finished = false;
}

void SpriteAnimationStudioPanel::track_edit(
    EntityManager& em,
    EntityID entity,
    const AnimationComponent& before,
    const std::string& property,
    const char* description,
    bool force_commit
) {
    track_component_edit<AnimationComponent>(
        em,
        entity,
        before,
        property.c_str(),
        description,
        {m_command_history.get(), &m_property_transactions},
        std::equal_to<AnimationComponent>{},
        reset_animation_runtime,
        force_commit
    );
}

void SpriteAnimationStudioPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(960, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("🎬 Sprite Animation Studio###SpriteAnimationStudio", &is_open, ImGuiWindowFlags_MenuBar)) {

        if (!m_scene_manager) {
            ImGui::TextDisabled("No active SceneManager.");
            ImGui::End();
            return;
        }

        Scene* active_scene = m_scene_manager->get_active_scene();
        if (!active_scene) {
            ImGui::TextDisabled("No active scene loaded.");
            ImGui::End();
            return;
        }

        EntityManager& em = active_scene->get_entity_manager();

        // Determine target entity (either selection or current target)
        if (m_selection && !m_selection->empty()) {
            EntityID sel_primary = m_selection->get_primary();
            if (em.is_alive(sel_primary) && em.has_component<SpriteComponent>(sel_primary)) {
                if (m_target_entity != sel_primary) m_preview_entity = 0;
                m_target_entity = sel_primary;
            }
        }

        if (m_target_entity == 0 || !em.is_alive(m_target_entity)) {
            // Find any entity with AnimationComponent or SpriteComponent
            em.for_each<AnimationComponent, SpriteComponent>([&](EntityID id, AnimationComponent&, SpriteComponent&) {
                if (m_target_entity == 0) m_target_entity = id;
            });
        }

        if (m_target_entity == 0 || !em.is_alive(m_target_entity) || !em.has_component<SpriteComponent>(m_target_entity)) {
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "⚠️ Please select an entity with a Sprite / Texture in the Scene Hierarchy.");
            ImGui::End();
            return;
        }

        // Opening the studio may author a default animator, so it follows the
        // same reversible component lifecycle as the Inspector.
        if (!em.has_component<AnimationComponent>(m_target_entity)) {
            AnimationComponent new_anim;
            new_anim.columns = 8;
            new_anim.rows = 3;
            new_anim.frame_width = 96.0f;
            new_anim.frame_height = 96.0f;
            new_anim.add_clip("Idle", 0, 8, 8.0f, true);
            if (!m_command_history) {
                ImGui::TextDisabled("Animation editing requires command history.");
                ImGui::End();
                return;
            }
            m_command_history->execute_command(std::make_unique<SetComponentCommand<AnimationComponent>>(
                &em,
                m_target_entity,
                std::nullopt,
                std::move(new_anim),
                "Add Sprite Animator 2D",
                reset_animation_runtime
            ));
        }

        if (m_preview_entity != m_target_entity) {
            reset_preview(m_target_entity, em.get_component<AnimationComponent>(m_target_entity));
        }

        render_top_bar(em, m_target_entity);

        ImGui::Separator();

        // --- Main Workspace Columns (Left: Live Preview & Clips | Right: Spritesheet Slicer) ---
        float left_col_width = 300.0f;
        float avail_width = ImGui::GetContentRegionAvail().x;
        float avail_height = ImGui::GetContentRegionAvail().y - 140.0f; // Leave space for bottom timeline

        ImGui::Columns(2, "StudioColumns", true);
        ImGui::SetColumnWidth(0, left_col_width);

        // 1. Left: Live Preview Viewport & Clip Manager
        render_preview_viewport(em, m_target_entity, left_col_width);
        render_clip_tabs(em, m_target_entity);

        ImGui::NextColumn();

        // 2. Right: Spritesheet Interactive Canvas & Slicer
        render_spritesheet_canvas(em, m_target_entity);

        ImGui::Columns(1);
        ImGui::Separator();

        // 3. Bottom: Timeline / Frame Strip
        render_timeline(em, m_target_entity);
    }
    ImGui::End();
}

void SpriteAnimationStudioPanel::render_top_bar(EntityManager& em, EntityID entity) {
    auto& sprite = em.get_component<SpriteComponent>(entity);
    std::string tag_name = em.has_component<TagComponent>(entity) ? em.get_component<TagComponent>(entity).tag : "Entity";

    ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "EDITING TARGET:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%s (ID #%u)", tag_name.c_str(), entity);

    if (sprite.texture) {
        ImGui::SameLine();
        ImGui::TextDisabled("| Texture: %dx%d px", sprite.texture->get_width(), sprite.texture->get_height());
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220.0f);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("Sheet Zoom", &m_sheet_zoom, 0.5f, 4.0f, "%.1fx");
}

void SpriteAnimationStudioPanel::render_preview_viewport(EntityManager& em, EntityID entity, float avail_width) {
    auto& anim = em.get_component<AnimationComponent>(entity);
    auto& sprite = em.get_component<SpriteComponent>(entity);
    Texture* tex = sprite.texture;

    ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "📺 LIVE PREVIEW");

    float preview_box_size = 180.0f;
    ImVec2 box_pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Dark Checkerboard Background
    dl->AddRectFilled(box_pos, ImVec2(box_pos.x + preview_box_size, box_pos.y + preview_box_size), IM_COL32(24, 24, 27, 255), 6.0f);

    float checker_size = 12.0f;
    for (float cy = 0; cy < preview_box_size; cy += checker_size) {
        for (float cx = 0; cx < preview_box_size; cx += checker_size) {
            int ix = static_cast<int>(cx / checker_size);
            int iy = static_cast<int>(cy / checker_size);
            if ((ix + iy) % 2 == 1) {
                float w = std::min(checker_size, preview_box_size - cx);
                float h = std::min(checker_size, preview_box_size - cy);
                dl->AddRectFilled(
                    ImVec2(box_pos.x + cx, box_pos.y + cy),
                    ImVec2(box_pos.x + cx + w, box_pos.y + cy + h),
                    IM_COL32(34, 34, 39, 255)
                );
            }
        }
    }
    dl->AddRect(box_pos, ImVec2(box_pos.x + preview_box_size, box_pos.y + preview_box_size), IM_COL32(63, 63, 70, 255), 6.0f, 0, 1.5f);

    // Live frame advancement inside Animation Studio
    if (!anim.get_clip(m_preview_clip_index)) reset_preview(entity, anim);
    const AnimationClip* clip = anim.get_clip(m_preview_clip_index);
    if (m_preview_playing && clip) {
        float dt = ImGui::GetIO().DeltaTime;
        m_preview_timer += dt;
        float frame_duration = 1.0f / (clip->fps > 0.1f ? clip->fps : 10.0f);

        while (m_preview_timer >= frame_duration && m_preview_playing) {
            m_preview_timer -= frame_duration;
            int total = clip->get_total_frames();
            if (total > 0) {
                int rel_frame = clip->custom_frames.empty() ? (m_preview_frame - clip->start_frame) : m_preview_frame;
                rel_frame++;

                if (rel_frame >= total) {
                    if (clip->loop) {
                        rel_frame = 0;
                    } else {
                        rel_frame = total - 1;
                        m_preview_playing = false;
                        m_preview_finished = true;
                    }
                }
                m_preview_frame = clip->custom_frames.empty() ? (clip->start_frame + rel_frame) : rel_frame;
            }
        }
    }

    // Draw active animated frame
    if (tex && tex->get_sdl_texture()) {
        SDL_FRect frame_rect = anim.get_frame_rect(m_preview_frame, clip);

        float tex_w = static_cast<float>(tex->get_width());
        float tex_h = static_cast<float>(tex->get_height());

        ImVec2 uv0(frame_rect.x / tex_w, frame_rect.y / tex_h);
        ImVec2 uv1((frame_rect.x + frame_rect.w) / tex_w, (frame_rect.y + frame_rect.h) / tex_h);

        // Center frame in preview box with zoom
        float draw_w = frame_rect.w * m_preview_zoom;
        float draw_h = frame_rect.h * m_preview_zoom;

        // Fit inside box if zoomed too large
        if (draw_w > preview_box_size * 0.9f || draw_h > preview_box_size * 0.9f) {
            float scale = std::min((preview_box_size * 0.9f) / draw_w, (preview_box_size * 0.9f) / draw_h);
            draw_w *= scale;
            draw_h *= scale;
        }

        ImVec2 draw_min(
            box_pos.x + (preview_box_size - draw_w) * 0.5f,
            box_pos.y + (preview_box_size - draw_h) * 0.5f
        );
        ImVec2 draw_max(draw_min.x + draw_w, draw_min.y + draw_h);

        dl->AddImage(
            (ImTextureID)(intptr_t)tex->get_sdl_texture(),
            draw_min,
            draw_max,
            uv0,
            uv1
        );
    }

    ImGui::Dummy(ImVec2(preview_box_size, preview_box_size));

    // Playback Transport Bar
    const AnimationClip* active_clip = anim.get_clip(m_preview_clip_index);
    int total_frames = active_clip ? std::max(1, active_clip->get_total_frames()) : 1;

    ImGui::Spacing();
    if (m_preview_playing) {
        if (ImGui::Button("⏸", ImVec2(32, 24))) m_preview_playing = false;
    } else {
        if (ImGui::Button("▶", ImVec2(32, 24))) m_preview_playing = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("⏮", ImVec2(32, 24))) {
        m_preview_playing = false;
        if (active_clip) {
            const int relative = active_clip->custom_frames.empty()
                ? m_preview_frame - active_clip->start_frame
                : m_preview_frame;
            const int previous = (relative - 1 + total_frames) % total_frames;
            m_preview_frame = active_clip->custom_frames.empty()
                ? active_clip->start_frame + previous
                : previous;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("⏭", ImVec2(32, 24))) {
        m_preview_playing = false;
        if (active_clip) {
            const int relative = active_clip->custom_frames.empty()
                ? m_preview_frame - active_clip->start_frame
                : m_preview_frame;
            const int next = (relative + 1) % total_frames;
            m_preview_frame = active_clip->custom_frames.empty()
                ? active_clip->start_frame + next
                : next;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("⏹", ImVec2(32, 24))) {
        m_preview_playing = false;
        m_preview_frame = active_clip ? (active_clip->custom_frames.empty() ? active_clip->start_frame : 0) : 0;
        m_preview_timer = 0.0f;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::SliderFloat("##PZoom", &m_preview_zoom, 0.5f, 4.0f, "%.1fx");

    if (active_clip) {
        const int relative = active_clip->custom_frames.empty()
            ? m_preview_frame - active_clip->start_frame
            : m_preview_frame;
        ImGui::Text("Frame: %d / %d", relative + 1, total_frames);
    }
}

void SpriteAnimationStudioPanel::render_clip_tabs(EntityManager& em, EntityID entity) {
    auto& anim = em.get_component<AnimationComponent>(entity);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "🎞️ ANIMATION CLIPS");

    int clip_to_remove = -1;

    for (size_t i = 0; i < anim.clips.size(); ++i) {
        auto& c = anim.clips[i];
        bool is_selected = (m_preview_clip_index == static_cast<int>(i));

        ImGui::PushID(static_cast<int>(i));
        if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.40f, 0.95f, 0.8f));
        }

        std::string btn_label = c.name + " (" + std::to_string(c.get_total_frames()) + "f)";
        if (ImGui::Button(btn_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 30.0f, 24))) {
            reset_preview(entity, anim, static_cast<int>(i));
        }

        if (is_selected) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        if (ImGui::Button("🗑", ImVec2(24, 24))) {
            clip_to_remove = static_cast<int>(i);
        }
        ImGui::PopID();
    }

    if (clip_to_remove >= 0 && clip_to_remove < static_cast<int>(anim.clips.size())) {
        const AnimationComponent before = anim;
        anim.clips.erase(anim.clips.begin() + clip_to_remove);
        anim.default_clip_index = std::clamp(
            anim.default_clip_index,
            0,
            std::max(0, static_cast<int>(anim.clips.size()) - 1)
        );
        track_edit(em, entity, before, "AnimationStudio.RemoveClip", "Remove Animation Clip", true);
        reset_preview(entity, anim);
    }

    if (ImGui::Button("+ Add New Clip", ImVec2(ImGui::GetContentRegionAvail().x, 24))) {
        const AnimationComponent before = anim;
        anim.add_clip("NewClip", 0, 4, 10.0f, true);
        track_edit(em, entity, before, "AnimationStudio.AddClip", "Add Animation Clip", true);
        reset_preview(entity, anim, static_cast<int>(anim.clips.size()) - 1);
    }

    // Active Clip Properties
    auto* active_clip = anim.get_clip(m_preview_clip_index);
    if (active_clip) {
        ImGui::Spacing();
        ImGui::TextDisabled("Clip Settings: '%s'", active_clip->name.c_str());

        char name_buf[64];
        strncpy(name_buf, active_clip->name.c_str(), sizeof(name_buf));
        name_buf[sizeof(name_buf) - 1] = '\0';
        const AnimationComponent name_before = anim;
        InspectorWidgets::begin_property_interaction("AnimationStudio.ClipName");
        if (ImGui::InputText("Clip Name", name_buf, sizeof(name_buf))) active_clip->name = name_buf;
        InspectorWidgets::capture_property_interaction();
        track_edit(em, entity, name_before, "AnimationStudio.ClipName", "Rename Animation Clip");

        const AnimationComponent speed_before = anim;
        InspectorWidgets::begin_property_interaction("AnimationStudio.ClipSpeed");
        ImGui::SliderFloat("Speed", &active_clip->fps, 1.0f, 60.0f, "%.0f FPS");
        InspectorWidgets::capture_property_interaction();
        track_edit(em, entity, speed_before, "AnimationStudio.ClipSpeed", "Edit Animation Clip Speed");

        const AnimationComponent loop_before = anim;
        InspectorWidgets::begin_property_interaction("AnimationStudio.ClipLoop");
        ImGui::Checkbox("Looping", &active_clip->loop);
        InspectorWidgets::capture_property_interaction();
        track_edit(em, entity, loop_before, "AnimationStudio.ClipLoop", "Toggle Animation Clip Loop");
    }
}

void SpriteAnimationStudioPanel::render_spritesheet_canvas(EntityManager& em, EntityID entity) {
    auto& anim = em.get_component<AnimationComponent>(entity);
    auto& sprite = em.get_component<SpriteComponent>(entity);
    Texture* tex = sprite.texture;

    if (!tex || !tex->get_sdl_texture()) {
        ImGui::TextDisabled("No spritesheet texture assigned to this entity.");
        return;
    }

    // Slicer Settings Header
    ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "🗺️ SPRITESHEET INTERACTIVE SLICER");
    ImGui::SameLine();
    ImGui::Checkbox("Freeform Mode", &m_slicer_mode_freeform);

    if (!m_slicer_mode_freeform) {
        // Grid Slicer Controls
        const AnimationComponent columns_before = anim;
        ImGui::SetNextItemWidth(70.0f);
        InspectorWidgets::begin_property_interaction("AnimationStudio.Columns");
        ImGui::DragInt("Cols", &anim.columns, 1.0f, 1, 64);
        InspectorWidgets::capture_property_interaction();
        track_edit(em, entity, columns_before, "AnimationStudio.Columns", "Edit Animation Columns");
        ImGui::SameLine();
        const AnimationComponent rows_before = anim;
        ImGui::SetNextItemWidth(70.0f);
        InspectorWidgets::begin_property_interaction("AnimationStudio.Rows");
        ImGui::DragInt("Rows", &anim.rows, 1.0f, 1, 64);
        InspectorWidgets::capture_property_interaction();
        track_edit(em, entity, rows_before, "AnimationStudio.Rows", "Edit Animation Rows");
        ImGui::SameLine();
        const AnimationComponent width_before = anim;
        ImGui::SetNextItemWidth(80.0f);
        InspectorWidgets::begin_property_interaction("AnimationStudio.FrameWidth");
        ImGui::DragFloat("Width", &anim.frame_width, 1.0f, 4.0f, 1024.0f, "%.0fpx");
        InspectorWidgets::capture_property_interaction();
        track_edit(em, entity, width_before, "AnimationStudio.FrameWidth", "Edit Animation Frame Width");
        ImGui::SameLine();
        const AnimationComponent height_before = anim;
        ImGui::SetNextItemWidth(80.0f);
        InspectorWidgets::begin_property_interaction("AnimationStudio.FrameHeight");
        ImGui::DragFloat("Height", &anim.frame_height, 1.0f, 4.0f, 1024.0f, "%.0fpx");
        InspectorWidgets::capture_property_interaction();
        track_edit(em, entity, height_before, "AnimationStudio.FrameHeight", "Edit Animation Frame Height");
        ImGui::SameLine();
        const AnimationComponent spacing_before = anim;
        ImGui::SetNextItemWidth(60.0f);
        InspectorWidgets::begin_property_interaction("AnimationStudio.Spacing");
        if (ImGui::DragFloat("Spacing", &anim.spacing_x, 1.0f, 0.0f, 64.0f, "%.0fpx")) {
            anim.spacing_y = anim.spacing_x;
        }
        InspectorWidgets::capture_property_interaction();
        track_edit(em, entity, spacing_before, "AnimationStudio.Spacing", "Edit Animation Spacing");
    } else {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "💡 Click & drag a box over the texture to carve any custom frame!");
    }

    // Canvas Scroll Area
    ImGui::BeginChild("SpritesheetCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    float tex_w = static_cast<float>(tex->get_width());
    float tex_h = static_cast<float>(tex->get_height());
    float canvas_w = tex_w * m_sheet_zoom;
    float canvas_h = tex_h * m_sheet_zoom;

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Dark Background
    dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h), IM_COL32(18, 18, 22, 255));

    // Render Full Texture
    dl->AddImage(
        (ImTextureID)(intptr_t)tex->get_sdl_texture(),
        canvas_pos,
        ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h)
    );

    const AnimationClip* active_clip = anim.get_clip(m_preview_clip_index);

    // Draw Grid Slices Overlay
    if (!m_slicer_mode_freeform && anim.columns > 0 && anim.rows > 0) {
        int total_grid_frames = anim.columns * anim.rows;
        for (int i = 0; i < total_grid_frames; ++i) {
            SDL_FRect r = anim.get_frame_rect(i, nullptr);
            ImVec2 p_min(canvas_pos.x + r.x * m_sheet_zoom, canvas_pos.y + r.y * m_sheet_zoom);
            ImVec2 p_max(p_min.x + r.w * m_sheet_zoom, p_min.y + r.h * m_sheet_zoom);

            bool in_active_clip = false;
            if (active_clip && active_clip->custom_frames.empty()) {
                in_active_clip = (i >= active_clip->start_frame && i < active_clip->start_frame + active_clip->frame_count);
            }

            bool is_current_playing = (in_active_clip && m_preview_frame == i);

            // Fill & Border Colors
            ImU32 fill_col = in_active_clip ? IM_COL32(59, 130, 246, 45) : IM_COL32(0, 0, 0, 0);
            ImU32 border_col = is_current_playing ? IM_COL32(234, 179, 8, 255) : 
                               (in_active_clip ? IM_COL32(59, 130, 246, 200) : IM_COL32(100, 116, 139, 70));
            float thick = is_current_playing ? 2.5f : (in_active_clip ? 1.5f : 1.0f);

            dl->AddRectFilled(p_min, p_max, fill_col);
            dl->AddRect(p_min, p_max, border_col, 0.0f, 0, thick);

            // Frame Number Badge
            char num_buf[16];
            snprintf(num_buf, sizeof(num_buf), "#%d", i);
            dl->AddText(ImVec2(p_min.x + 3.0f, p_min.y + 3.0f), in_active_clip ? IM_COL32(147, 197, 253, 255) : IM_COL32(148, 163, 184, 130), num_buf);
        }
    }

    // Draw Custom Frame Rectangles (if any in active clip)
    if (active_clip && !active_clip->custom_frames.empty()) {
        for (size_t i = 0; i < active_clip->custom_frames.size(); ++i) {
            const auto& r = active_clip->custom_frames[i];
            ImVec2 p_min(canvas_pos.x + r.x * m_sheet_zoom, canvas_pos.y + r.y * m_sheet_zoom);
            ImVec2 p_max(p_min.x + r.w * m_sheet_zoom, p_min.y + r.h * m_sheet_zoom);

            bool is_current = (m_preview_frame == static_cast<int>(i));
            ImU32 border_col = is_current ? IM_COL32(234, 179, 8, 255) : IM_COL32(16, 185, 129, 220);

            dl->AddRectFilled(p_min, p_max, IM_COL32(16, 185, 129, 45));
            dl->AddRect(p_min, p_max, border_col, 0.0f, 0, is_current ? 2.5f : 1.5f);

            char badge[16];
            snprintf(badge, sizeof(badge), "#%zu", i);
            dl->AddText(ImVec2(p_min.x + 3.0f, p_min.y + 3.0f), IM_COL32(167, 243, 208, 255), badge);
        }
    }

    // Mouse Interaction for Slicing / Selecting
    ImGui::InvisibleButton("CanvasHitbox", ImVec2(canvas_w, canvas_h));
    bool is_hovered = ImGui::IsItemHovered();
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;

    if (is_hovered) {
        float rel_x = (mouse_pos.x - canvas_pos.x) / m_sheet_zoom;
        float rel_y = (mouse_pos.y - canvas_pos.y) / m_sheet_zoom;

        if (!m_slicer_mode_freeform && anim.frame_width > 0 && anim.frame_height > 0) {
            int col = static_cast<int>(rel_x / (anim.frame_width + anim.spacing_x));
            int row = static_cast<int>(rel_y / (anim.frame_height + anim.spacing_y));
            if (col >= 0 && col < anim.columns && row >= 0 && row < anim.rows) {
                int clicked_frame = row * anim.columns + col;

                // Click to assign to current clip
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (auto* c = anim.get_clip(m_preview_clip_index)) {
                        if (c->custom_frames.empty()) {
                            const AnimationComponent before = anim;
                            c->start_frame = clicked_frame;
                            track_edit(
                                em,
                                entity,
                                before,
                                "AnimationStudio.ClipStartFromCanvas",
                                "Set Animation Clip Start Frame",
                                true
                            );
                            m_preview_frame = clicked_frame;
                        }
                    }
                }
            }
        } else if (m_slicer_mode_freeform) {
            // Freeform Box Dragging
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_is_dragging_box = true;
                m_drag_start_uv = ImVec2(rel_x, rel_y);
                m_drag_end_uv = m_drag_start_uv;
            }
        }
    }

    if (m_is_dragging_box) {
        float rel_x = (mouse_pos.x - canvas_pos.x) / m_sheet_zoom;
        float rel_y = (mouse_pos.y - canvas_pos.y) / m_sheet_zoom;
        m_drag_end_uv = ImVec2(std::clamp(rel_x, 0.0f, tex_w), std::clamp(rel_y, 0.0f, tex_h));

        ImVec2 p0(canvas_pos.x + std::min(m_drag_start_uv.x, m_drag_end_uv.x) * m_sheet_zoom,
                  canvas_pos.y + std::min(m_drag_start_uv.y, m_drag_end_uv.y) * m_sheet_zoom);
        ImVec2 p1(canvas_pos.x + std::max(m_drag_start_uv.x, m_drag_end_uv.x) * m_sheet_zoom,
                  canvas_pos.y + std::max(m_drag_start_uv.y, m_drag_end_uv.y) * m_sheet_zoom);

        dl->AddRectFilled(p0, p1, IM_COL32(236, 72, 153, 50));
        dl->AddRect(p0, p1, IM_COL32(236, 72, 153, 255), 0.0f, 0, 2.0f);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_is_dragging_box = false;
            float rx = std::min(m_drag_start_uv.x, m_drag_end_uv.x);
            float ry = std::min(m_drag_start_uv.y, m_drag_end_uv.y);
            float rw = std::abs(m_drag_end_uv.x - m_drag_start_uv.x);
            float rh = std::abs(m_drag_end_uv.y - m_drag_start_uv.y);

            if (rw > 4.0f && rh > 4.0f) {
                if (auto* c = anim.get_clip(m_preview_clip_index)) {
                    const AnimationComponent before = anim;
                    c->custom_frames.push_back(SDL_FRect{rx, ry, rw, rh});
                    track_edit(
                        em,
                        entity,
                        before,
                        "AnimationStudio.AddCustomFrame",
                        "Add Custom Animation Frame",
                        true
                    );
                }
            }
        }
    }

    ImGui::EndChild();
}

void SpriteAnimationStudioPanel::render_timeline(EntityManager& em, EntityID entity) {
    auto& anim = em.get_component<AnimationComponent>(entity);
    auto& sprite = em.get_component<SpriteComponent>(entity);
    Texture* tex = sprite.texture;

    const AnimationClip* active_clip = anim.get_clip(m_preview_clip_index);
    if (!active_clip || !tex) return;

    int total_frames = active_clip->get_total_frames();

    ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "🎞️ TIMELINE & FRAME STRIP ('%s' | %d frames)", active_clip->name.c_str(), total_frames);

    ImGui::BeginChild("TimelineStrip", ImVec2(0, 85), false, ImGuiWindowFlags_HorizontalScrollbar);

    float thumb_size = 56.0f;
    float tex_w = static_cast<float>(tex->get_width());
    float tex_h = static_cast<float>(tex->get_height());

    for (int i = 0; i < total_frames; ++i) {
        ImGui::PushID(i);

        int frame_idx = active_clip->custom_frames.empty() ? (active_clip->start_frame + i) : i;
        SDL_FRect f_rect = anim.get_frame_rect(frame_idx, active_clip);

        ImVec2 uv0(f_rect.x / tex_w, f_rect.y / tex_h);
        ImVec2 uv1((f_rect.x + f_rect.w) / tex_w, (f_rect.y + f_rect.h) / tex_h);

        bool is_current = (m_preview_frame == frame_idx);

        ImVec2 btn_pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Background
        dl->AddRectFilled(btn_pos, ImVec2(btn_pos.x + thumb_size, btn_pos.y + thumb_size), IM_COL32(30, 30, 35, 255), 4.0f);
        dl->AddRect(btn_pos, ImVec2(btn_pos.x + thumb_size, btn_pos.y + thumb_size), 
                    is_current ? IM_COL32(234, 179, 8, 255) : IM_COL32(63, 63, 70, 200), 
                    4.0f, 0, is_current ? 2.5f : 1.0f);

        // Clickable Thumbnail Image Button
        if (ImGui::ImageButton("##frame_btn", (ImTextureID)(intptr_t)tex->get_sdl_texture(), ImVec2(thumb_size - 6.0f, thumb_size - 6.0f), uv0, uv1)) {
            m_preview_frame = frame_idx;
            m_preview_playing = false;
        }

        // Frame number label
        char f_label[16];
        snprintf(f_label, sizeof(f_label), "#%d", i);
        ImGui::TextDisabled("%s", f_label);

        ImGui::SameLine();
        ImGui::PopID();
    }

    ImGui::EndChild();
}

} // namespace Engine
