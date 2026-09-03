#include "SpriteAnimationStudioPanel.hpp"

#include "../../ecs/components/RenderingComponents.hpp"
#include "../../renderer/Texture.hpp"
#include "../EditorTheme.hpp"
#include "../inspector/InspectorWidgets.hpp"
#include "../ui/EditorUi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <string>

namespace Engine {

namespace {

ImVec4 with_alpha(const ImVec4& color, float alpha) {
    return ImVec4(color.x, color.y, color.z, alpha);
}

bool contains(const SDL_FRect& frame, ImVec2 point) {
    return point.x >= frame.x && point.y >= frame.y &&
        point.x <= frame.x + frame.w && point.y <= frame.y + frame.h;
}

SDL_FRect normalized_frame(ImVec2 start, ImVec2 end) {
    return SDL_FRect{
        std::min(start.x, end.x),
        std::min(start.y, end.y),
        std::abs(end.x - start.x),
        std::abs(end.y - start.y)
    };
}

void clamp_frame(SDL_FRect& frame, float texture_width, float texture_height) {
    frame.w = std::clamp(frame.w, 1.0f, texture_width);
    frame.h = std::clamp(frame.h, 1.0f, texture_height);
    frame.x = std::clamp(frame.x, 0.0f, texture_width - frame.w);
    frame.y = std::clamp(frame.y, 0.0f, texture_height - frame.h);
}

SDL_FRect fixed_frame_at(
    ImVec2 center,
    ImVec2 dimensions,
    float texture_width,
    float texture_height
) {
    SDL_FRect frame{
        center.x - dimensions.x * 0.5f,
        center.y - dimensions.y * 0.5f,
        dimensions.x,
        dimensions.y
    };
    clamp_frame(frame, texture_width, texture_height);
    return frame;
}

bool draw_timeline_frame(
    const char* id,
    Texture& texture,
    ImVec2 uv_min,
    ImVec2 uv_max,
    int display_index,
    bool selected
) {
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    const float image_size = 58.0f * metrics.scale;
    const float label_height = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 item_size(image_size, image_size + label_height);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, item_size);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 image_max(position.x + image_size, position.y + image_size);
    draw_list->AddRectFilled(
        position, image_max,
        ImGui::ColorConvertFloat4ToU32(hovered ? palette.header : palette.window),
        metrics.small_rounding
    );
    const float inset = 4.0f * metrics.scale;
    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(texture.get_sdl_texture()),
        ImVec2(position.x + inset, position.y + inset),
        ImVec2(image_max.x - inset, image_max.y - inset),
        uv_min,
        uv_max
    );
    draw_list->AddRect(
        position,
        image_max,
        ImGui::ColorConvertFloat4ToU32(selected ? palette.warning : palette.border),
        metrics.small_rounding,
        0,
        selected ? 2.0f : 1.0f
    );

    char label[20];
    std::snprintf(label, sizeof(label), "#%d", display_index);
    const ImVec2 label_size = ImGui::CalcTextSize(label);
    draw_list->AddText(
        ImVec2(
            position.x + (image_size - label_size.x) * 0.5f,
            image_max.y + 2.0f * metrics.scale
        ),
        ImGui::ColorConvertFloat4ToU32(
            selected ? palette.warning : palette.text_muted
        ),
        label
    );
    return pressed;
}

} // namespace

void SpriteAnimationStudioPanel::render_spritesheet_canvas(
    EntityManager& em,
    EntityID entity,
    float available_height
) {
    auto& animation = draft_animation(em, entity);
    const auto& sprite = em.get_component<SpriteComponent>(entity);
    Texture* texture = sprite.texture;
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    const float controls_start_y = ImGui::GetCursorPosY();

    if (!texture || !texture->get_sdl_texture()) {
        EditorUi::section_heading("SPRITESHEET");
        ImGui::TextDisabled("No texture is assigned to this sprite.");
        return;
    }

    const std::string sheet_detail = std::to_string(texture->get_width()) + " x " +
        std::to_string(texture->get_height()) + " px";
    AnimationClip* active_clip = animation.get_clip(m_preview_clip_index);
    EditorUi::section_heading("SPRITESHEET", sheet_detail.c_str());
    if (EditorUi::action_button(
            "animation_grid_mode", EditorIcon::Grid, "Grid",
            m_slicer_mode_freeform
                ? EditorUi::ButtonTone::Neutral
                : EditorUi::ButtonTone::Primary)) {
        if (active_clip && !active_clip->custom_frames.empty()) {
            ImGui::OpenPopup("Use Grid Slicing?");
        } else {
            m_slicer_mode_freeform = false;
        }
    }
    ImGui::SameLine();
    if (EditorUi::action_button(
            "animation_freeform_mode", EditorIcon::Edit, "Freeform",
            m_slicer_mode_freeform
                ? EditorUi::ButtonTone::Primary
                : EditorUi::ButtonTone::Neutral)) {
        m_slicer_mode_freeform = true;
        if (active_clip && active_clip->custom_frames.empty()) {
            m_freeform_tool = FreeformTool::Draw;
        }
    }

    if (ImGui::BeginPopupModal(
            "Use Grid Slicing?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This clip currently uses custom slices.");
        ImGui::TextDisabled("Converting it to the grid removes those slices from the draft.");
        ImGui::Spacing();
        if (EditorUi::action_button(
                "confirm_grid_slicing", EditorIcon::Grid, "Use Grid",
                EditorUi::ButtonTone::Danger, active_clip != nullptr)) {
            const AnimationComponent before = animation;
            active_clip->custom_frames.clear();
            m_selected_custom_frame = -1;
            m_freeform_drag = FreeformDrag::None;
            m_slicer_mode_freeform = false;
            track_edit(
                em, entity, before,
                "AnimationStudio.UseGridFrames", "Convert Animation Clip To Grid", true
            );
            reset_preview(entity, animation, m_preview_clip_index);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorUi::action_button(
                "cancel_grid_slicing", EditorIcon::Stop, "Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!m_slicer_mode_freeform) {
        const auto& grid_policy = InspectorPropertyPolicies::Animation::GridDimension;
        const auto& frame_policy = InspectorPropertyPolicies::Animation::FrameSize;
        if (ImGui::BeginTable(
                "AnimationGridPrimary", 4,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Columns");
            ImGui::SetNextItemWidth(-1.0f);
            const AnimationComponent columns_before = animation;
            InspectorWidgets::begin_property_interaction("AnimationStudio.Columns");
            if (ImGui::DragInt(
                    "##columns", &animation.columns,
                    grid_policy.speed, grid_policy.minimum, grid_policy.maximum)) {
                animation.columns = grid_policy.sanitize(animation.columns);
            }
            InspectorWidgets::capture_property_interaction();
            track_edit(
                em, entity, columns_before,
                "AnimationStudio.Columns", "Edit Animation Columns"
            );

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("Rows");
            ImGui::SetNextItemWidth(-1.0f);
            const AnimationComponent rows_before = animation;
            InspectorWidgets::begin_property_interaction("AnimationStudio.Rows");
            if (ImGui::DragInt(
                    "##rows", &animation.rows,
                    grid_policy.speed, grid_policy.minimum, grid_policy.maximum)) {
                animation.rows = grid_policy.sanitize(animation.rows);
            }
            InspectorWidgets::capture_property_interaction();
            track_edit(
                em, entity, rows_before,
                "AnimationStudio.Rows", "Edit Animation Rows"
            );

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("Frame Width");
            ImGui::SetNextItemWidth(-1.0f);
            const AnimationComponent width_before = animation;
            InspectorWidgets::begin_property_interaction("AnimationStudio.FrameWidth");
            if (ImGui::DragFloat(
                    "##frame_width", &animation.frame_width,
                    frame_policy.speed, frame_policy.minimum, frame_policy.maximum,
                    frame_policy.format)) {
                animation.frame_width = frame_policy.sanitize(animation.frame_width);
            }
            InspectorWidgets::capture_property_interaction();
            track_edit(
                em, entity, width_before,
                "AnimationStudio.FrameWidth", "Edit Animation Frame Width"
            );

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("Frame Height");
            ImGui::SetNextItemWidth(-1.0f);
            const AnimationComponent height_before = animation;
            InspectorWidgets::begin_property_interaction("AnimationStudio.FrameHeight");
            if (ImGui::DragFloat(
                    "##frame_height", &animation.frame_height,
                    frame_policy.speed, frame_policy.minimum, frame_policy.maximum,
                    frame_policy.format)) {
                animation.frame_height = frame_policy.sanitize(animation.frame_height);
            }
            InspectorWidgets::capture_property_interaction();
            track_edit(
                em, entity, height_before,
                "AnimationStudio.FrameHeight", "Edit Animation Frame Height"
            );
            ImGui::EndTable();
        }

        const auto& padding_policy = InspectorPropertyPolicies::Animation::GridPadding;
        if (ImGui::BeginTable(
                "AnimationGridPadding", 2,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Spacing X / Y");
            ImGui::SetNextItemWidth(-1.0f);
            float spacing[2]{animation.spacing_x, animation.spacing_y};
            const AnimationComponent spacing_before = animation;
            InspectorWidgets::begin_property_interaction("AnimationStudio.Spacing");
            if (ImGui::DragFloat2(
                    "##spacing", spacing,
                    padding_policy.speed, padding_policy.minimum, padding_policy.maximum,
                    padding_policy.format)) {
                animation.spacing_x = padding_policy.sanitize(spacing[0]);
                animation.spacing_y = padding_policy.sanitize(spacing[1]);
            }
            InspectorWidgets::capture_property_interaction();
            track_edit(
                em, entity, spacing_before,
                "AnimationStudio.Spacing", "Edit Animation Spacing"
            );

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("Margin X / Y");
            ImGui::SetNextItemWidth(-1.0f);
            float margin[2]{animation.margin_x, animation.margin_y};
            const AnimationComponent margin_before = animation;
            InspectorWidgets::begin_property_interaction("AnimationStudio.Margin");
            if (ImGui::DragFloat2(
                    "##margin", margin,
                    padding_policy.speed, padding_policy.minimum, padding_policy.maximum,
                    padding_policy.format)) {
                animation.margin_x = padding_policy.sanitize(margin[0]);
                animation.margin_y = padding_policy.sanitize(margin[1]);
            }
            InspectorWidgets::capture_property_interaction();
            track_edit(
                em, entity, margin_before,
                "AnimationStudio.Margin", "Edit Animation Margin"
            );
            ImGui::EndTable();
        }
    } else {
        ImGui::SameLine();
        if (EditorUi::toolbar_button(
                "draft_undo", EditorIcon::Undo, "Undo draft edit",
                false, !m_draft_undo.empty() || m_pending_history_snapshot.has_value() ||
                    m_nudge_history_snapshot.has_value())) {
            undo_draft_edit();
            active_clip = animation.get_clip(m_preview_clip_index);
        }
        ImGui::SameLine();
        if (EditorUi::toolbar_button(
                "draft_redo", EditorIcon::Redo, "Redo draft edit",
                false, !m_draft_redo.empty())) {
            redo_draft_edit();
            active_clip = animation.get_clip(m_preview_clip_index);
        }
        ImGui::SameLine();
        if (EditorUi::action_button(
                "freeform_select", EditorIcon::Move, "Select",
                m_freeform_tool == FreeformTool::Select
                    ? EditorUi::ButtonTone::Primary
                    : EditorUi::ButtonTone::Neutral)) {
            m_freeform_tool = FreeformTool::Select;
            m_freeform_drag = FreeformDrag::None;
        }
        ImGui::SameLine();
        if (EditorUi::action_button(
                "freeform_draw", EditorIcon::Plus, "Draw Slice",
                m_freeform_tool == FreeformTool::Draw
                    ? EditorUi::ButtonTone::Primary
                    : EditorUi::ButtonTone::Neutral)) {
            m_freeform_tool = FreeformTool::Draw;
            m_freeform_drag = FreeformDrag::None;
        }
        ImGui::SameLine();
        bool has_selection = active_clip && m_selected_custom_frame >= 0 &&
            m_selected_custom_frame < static_cast<int>(active_clip->custom_frames.size());
        const ImGuiIO& input = ImGui::GetIO();
        const bool shortcut_modifier = input.KeySuper || input.KeyCtrl;
        const bool shortcut_context = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows
        ) && !input.WantTextInput;
        const bool copy_requested = EditorUi::toolbar_button(
                "freeform_copy", EditorIcon::Copy, "Copy selected slice",
                false, has_selection) ||
            (shortcut_context && shortcut_modifier &&
             ImGui::IsKeyPressed(ImGuiKey_C, false));
        if (copy_requested && has_selection) {
            m_freeform_clipboard = active_clip->custom_frames[m_selected_custom_frame];
            if (m_fixed_draw_size) {
                m_fixed_draw_dimensions = ImVec2(
                    m_freeform_clipboard->w, m_freeform_clipboard->h
                );
            }
        }
        ImGui::SameLine();
        const bool paste_requested = EditorUi::toolbar_button(
                "freeform_paste", EditorIcon::Paste,
                "Paste slice with the copied size", false,
                active_clip != nullptr && m_freeform_clipboard.has_value()) ||
            (shortcut_context && shortcut_modifier &&
             ImGui::IsKeyPressed(ImGuiKey_V, false));
        if (paste_requested && active_clip && m_freeform_clipboard) {
            const AnimationComponent before = animation;
            SDL_FRect pasted = *m_freeform_clipboard;
            pasted.x += 8.0f;
            pasted.y += 8.0f;
            clamp_frame(
                pasted,
                static_cast<float>(texture->get_width()),
                static_cast<float>(texture->get_height())
            );
            active_clip->custom_frames.push_back(pasted);
            m_selected_custom_frame =
                static_cast<int>(active_clip->custom_frames.size()) - 1;
            m_preview_frame = m_selected_custom_frame;
            m_preview_playing = false;
            m_freeform_tool = FreeformTool::Select;
            m_freeform_clipboard = pasted;
            track_edit(
                em, entity, before,
                "AnimationStudio.PasteCustomFrame",
                "Paste Custom Animation Frame", true
            );
        }
        ImGui::SameLine();
        const std::string fixed_size_tooltip = m_fixed_draw_size
            ? "Fixed-size drawing: " +
                std::to_string(static_cast<int>(m_fixed_draw_dimensions.x)) + " x " +
                std::to_string(static_cast<int>(m_fixed_draw_dimensions.y))
            : "Use the selected slice size while drawing";
        if (EditorUi::toolbar_button(
                "freeform_fixed_size",
                m_fixed_draw_size ? EditorIcon::Lock : EditorIcon::Unlock,
                fixed_size_tooltip.c_str(),
                m_fixed_draw_size, active_clip != nullptr)) {
            m_fixed_draw_size = !m_fixed_draw_size;
            if (m_fixed_draw_size) {
                if (has_selection) {
                    const SDL_FRect& selected =
                        active_clip->custom_frames[m_selected_custom_frame];
                    m_fixed_draw_dimensions = ImVec2(selected.w, selected.h);
                } else if (m_freeform_clipboard) {
                    m_fixed_draw_dimensions = ImVec2(
                        m_freeform_clipboard->w, m_freeform_clipboard->h
                    );
                } else {
                    m_fixed_draw_dimensions = ImVec2(
                        std::max(1.0f, animation.frame_width),
                        std::max(1.0f, animation.frame_height)
                    );
                }
                m_freeform_tool = FreeformTool::Draw;
                m_freeform_drag = FreeformDrag::None;
            }
        }
        ImGui::SameLine();
        if (EditorUi::toolbar_button(
                "freeform_center_guides", EditorIcon::Crosshair,
                m_show_center_guides ? "Hide center guides" : "Show center guides",
                m_show_center_guides)) {
            m_show_center_guides = !m_show_center_guides;
        }
        ImGui::SameLine();
        has_selection = active_clip && m_selected_custom_frame >= 0 &&
            m_selected_custom_frame < static_cast<int>(active_clip->custom_frames.size());
        if (EditorUi::action_button(
                "freeform_delete", EditorIcon::Trash, "Delete Slice",
                EditorUi::ButtonTone::Danger, has_selection)) {
            const AnimationComponent before = animation;
            active_clip->custom_frames.erase(
                active_clip->custom_frames.begin() + m_selected_custom_frame
            );
            m_selected_custom_frame = std::min(
                m_selected_custom_frame,
                static_cast<int>(active_clip->custom_frames.size()) - 1
            );
            m_preview_frame = std::max(0, m_selected_custom_frame);
            track_edit(
                em, entity, before,
                "AnimationStudio.DeleteCustomFrame", "Delete Custom Animation Frame", true
            );
            has_selection = active_clip && m_selected_custom_frame >= 0 &&
                m_selected_custom_frame <
                    static_cast<int>(active_clip->custom_frames.size());
        }
        ImGui::SameLine();
        const float slice_badge_x = ImGui::GetCursorPosX();
        const std::string slice_label = has_selection
            ? "#" + std::to_string(m_selected_custom_frame)
            : "NO SLICE";
        EditorUi::badge(
            slice_label.c_str(),
            has_selection ? EditorUi::BadgeTone::Accent : EditorUi::BadgeTone::Neutral
        );

        SDL_FRect* selected = has_selection
            ? &active_clip->custom_frames[m_selected_custom_frame]
            : nullptr;
        const char* labels[]{"X", "Y", "W", "H"};
        float placeholder = 0.0f;
        float* values[]{
            selected ? &selected->x : &placeholder,
            selected ? &selected->y : &placeholder,
            selected ? &selected->w : &placeholder,
            selected ? &selected->h : &placeholder
        };
        for (int field = 0; field < 4; ++field) {
            if (field == 0) {
                ImGui::SameLine(slice_badge_x + 74.0f * metrics.scale);
            } else {
                ImGui::SameLine();
            }
            ImGui::TextDisabled("%s", labels[field]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(58.0f * metrics.scale);
            ImGui::BeginDisabled(!has_selection);
            const AnimationComponent before = animation;
            ImGui::PushID(field);
            const bool bounds_changed = ImGui::DragFloat(
                    "##freeform_value", values[field], 1.0f, 0.0f, 0.0f,
                    has_selection ? "%.0f" : "--");
            if (bounds_changed) {
                clamp_frame(
                    *selected,
                    static_cast<float>(texture->get_width()),
                    static_cast<float>(texture->get_height())
                );
                if (m_fixed_draw_size && (field == 2 || field == 3)) {
                    m_fixed_draw_dimensions = ImVec2(selected->w, selected->h);
                }
            }
            if (has_selection) {
                track_edit(
                    em, entity, before,
                    "AnimationStudio.CustomFrameBounds", "Edit Custom Animation Frame"
                );
            }
            ImGui::PopID();
            ImGui::EndDisabled();
        }
    }

    const float controls_height = ImGui::GetCursorPosY() - controls_start_y;
    const float canvas_height = std::max(
        120.0f * metrics.scale,
        available_height - controls_height - metrics.compact_spacing
    );
    if (m_sheet_zoom_fit_pending) {
        const ImVec2 canvas_available = ImGui::GetContentRegionAvail();
        const float horizontal_fit =
            (canvas_available.x - ImGui::GetStyle().ScrollbarSize) /
            static_cast<float>(texture->get_width());
        const float vertical_fit =
            (canvas_height - ImGui::GetStyle().ScrollbarSize) /
            static_cast<float>(texture->get_height());
        m_sheet_zoom = std::clamp(
            std::min(horizontal_fit, vertical_fit) * 0.98f,
            0.05f,
            8.0f
        );
        m_sheet_zoom_fit_pending = false;
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.window);
    ImGui::BeginChild(
        "SpritesheetCanvas",
        ImVec2(0.0f, canvas_height),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );

    const float texture_width = static_cast<float>(texture->get_width());
    const float texture_height = static_cast<float>(texture->get_height());
    const float canvas_width = texture_width * m_sheet_zoom;
    const float scaled_height = texture_height * m_sheet_zoom;
    const ImVec2 canvas_position = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(texture->get_sdl_texture()),
        canvas_position,
        ImVec2(canvas_position.x + canvas_width, canvas_position.y + scaled_height)
    );

    if (!m_slicer_mode_freeform && animation.columns > 0 && animation.rows > 0) {
        const int total_grid_frames = animation.columns * animation.rows;
        for (int index = 0; index < total_grid_frames; ++index) {
            const SDL_FRect frame = animation.get_frame_rect(index, nullptr);
            const ImVec2 minimum(
                canvas_position.x + frame.x * m_sheet_zoom,
                canvas_position.y + frame.y * m_sheet_zoom
            );
            const ImVec2 maximum(
                minimum.x + frame.w * m_sheet_zoom,
                minimum.y + frame.h * m_sheet_zoom
            );
            const bool in_clip = active_clip && active_clip->custom_frames.empty() &&
                index >= active_clip->start_frame &&
                index < active_clip->start_frame + active_clip->frame_count;
            const bool current = in_clip && m_preview_frame == index;
            if (in_clip) {
                draw_list->AddRectFilled(
                    minimum, maximum,
                    ImGui::ColorConvertFloat4ToU32(with_alpha(palette.accent, 0.18f))
                );
            }
            draw_list->AddRect(
                minimum,
                maximum,
                ImGui::ColorConvertFloat4ToU32(
                    current
                        ? palette.warning
                        : (in_clip ? with_alpha(palette.accent_hovered, 0.82f)
                                   : with_alpha(palette.text_muted, 0.32f))
                ),
                0.0f,
                0,
                current ? 2.5f : (in_clip ? 1.5f : 1.0f)
            );
            char label[16];
            std::snprintf(label, sizeof(label), "#%d", index);
            draw_list->AddText(
                ImVec2(minimum.x + 3.0f, minimum.y + 3.0f),
                ImGui::ColorConvertFloat4ToU32(
                    in_clip ? palette.accent_hovered : with_alpha(palette.text_muted, 0.65f)
                ),
                label
            );
        }
    }

    if (active_clip && !active_clip->custom_frames.empty()) {
        for (std::size_t index = 0; index < active_clip->custom_frames.size(); ++index) {
            const SDL_FRect& frame = active_clip->custom_frames[index];
            const ImVec2 minimum(
                canvas_position.x + frame.x * m_sheet_zoom,
                canvas_position.y + frame.y * m_sheet_zoom
            );
            const ImVec2 maximum(
                minimum.x + frame.w * m_sheet_zoom,
                minimum.y + frame.h * m_sheet_zoom
            );
            const bool current = m_preview_frame == static_cast<int>(index);
            const bool selected = m_slicer_mode_freeform &&
                m_selected_custom_frame == static_cast<int>(index);
            draw_list->AddRectFilled(
                minimum, maximum,
                ImGui::ColorConvertFloat4ToU32(with_alpha(
                    selected ? palette.warning : palette.success,
                    selected ? 0.24f : 0.14f
                ))
            );
            draw_list->AddRect(
                minimum, maximum,
                ImGui::ColorConvertFloat4ToU32(
                    selected ? palette.warning : (current ? palette.accent_hovered : palette.success)
                ),
                0.0f, 0, selected ? 2.5f : (current ? 2.0f : 1.5f)
            );
            char label[16];
            std::snprintf(label, sizeof(label), "#%zu", index);
            draw_list->AddText(
                ImVec2(minimum.x + 3.0f, minimum.y + 3.0f),
                ImGui::ColorConvertFloat4ToU32(palette.success), label
            );
            if (selected) {
                if (m_show_center_guides) {
                    const float center_x = (minimum.x + maximum.x) * 0.5f;
                    const float center_y = (minimum.y + maximum.y) * 0.5f;
                    const ImU32 guide_color = ImGui::ColorConvertFloat4ToU32(
                        with_alpha(palette.accent_hovered, 0.52f)
                    );
                    draw_list->AddLine(
                        ImVec2(center_x, canvas_position.y),
                        ImVec2(center_x, canvas_position.y + scaled_height),
                        guide_color
                    );
                    draw_list->AddLine(
                        ImVec2(canvas_position.x, center_y),
                        ImVec2(canvas_position.x + canvas_width, center_y),
                        guide_color
                    );
                }
                const float handle_size = 7.0f * metrics.scale;
                draw_list->AddRectFilled(
                    ImVec2(maximum.x - handle_size, maximum.y - handle_size),
                    maximum,
                    ImGui::ColorConvertFloat4ToU32(palette.warning)
                );
            }
        }
    }

    ImGui::InvisibleButton(
        "CanvasHitbox", ImVec2(canvas_width, scaled_height),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle
    );
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 local_mouse(
        std::clamp((mouse.x - canvas_position.x) / m_sheet_zoom, 0.0f, texture_width),
        std::clamp((mouse.y - canvas_position.y) / m_sheet_zoom, 0.0f, texture_height)
    );
    if (hovered && m_slicer_mode_freeform &&
        m_freeform_tool == FreeformTool::Draw && m_fixed_draw_size &&
        m_freeform_drag == FreeformDrag::None &&
        !ImGui::IsKeyDown(ImGuiKey_Space)) {
        const SDL_FRect pending = fixed_frame_at(
            local_mouse, m_fixed_draw_dimensions, texture_width, texture_height
        );
        const ImVec2 minimum(
            canvas_position.x + pending.x * m_sheet_zoom,
            canvas_position.y + pending.y * m_sheet_zoom
        );
        const ImVec2 maximum(
            minimum.x + pending.w * m_sheet_zoom,
            minimum.y + pending.h * m_sheet_zoom
        );
        draw_list->AddRectFilled(
            minimum, maximum,
            ImGui::ColorConvertFloat4ToU32(with_alpha(palette.accent_hovered, 0.16f))
        );
        draw_list->AddRect(
            minimum, maximum,
            ImGui::ColorConvertFloat4ToU32(with_alpha(palette.accent_hovered, 0.86f)),
            0.0f, 0, 1.5f
        );
    }
    if (hovered) {
        ImGui::SetMouseCursor(m_slicer_mode_freeform
            ? (m_freeform_tool == FreeformTool::Draw
                ? ImGuiMouseCursor_ResizeAll
                : ImGuiMouseCursor_Hand)
            : ImGuiMouseCursor_Hand);
        const float relative_x = local_mouse.x;
        const float relative_y = local_mouse.y;
        if (!m_slicer_mode_freeform && animation.frame_width > 0.0f &&
            animation.frame_height > 0.0f) {
            const float grid_x = relative_x - animation.margin_x;
            const float grid_y = relative_y - animation.margin_y;
            const float stride_x = animation.frame_width + animation.spacing_x;
            const float stride_y = animation.frame_height + animation.spacing_y;
            const int column = grid_x >= 0.0f && stride_x > 0.0f
                ? static_cast<int>(grid_x / stride_x)
                : -1;
            const int row = grid_y >= 0.0f && stride_y > 0.0f
                ? static_cast<int>(grid_y / stride_y)
                : -1;
            const bool inside_frame = column >= 0 && column < animation.columns &&
                row >= 0 && row < animation.rows &&
                grid_x - static_cast<float>(column) * stride_x <= animation.frame_width &&
                grid_y - static_cast<float>(row) * stride_y <= animation.frame_height;
            if (inside_frame && !ImGui::IsKeyDown(ImGuiKey_Space) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (AnimationClip* clip = animation.get_clip(m_preview_clip_index);
                    clip && clip->custom_frames.empty()) {
                    const AnimationComponent before = animation;
                    const int clicked_frame = row * animation.columns + column;
                    clip->start_frame = clicked_frame;
                    track_edit(
                        em, entity, before,
                        "AnimationStudio.ClipStartFromCanvas",
                        "Set Animation Clip Start Frame", true
                    );
                    m_preview_frame = clicked_frame;
                    m_preview_playing = false;
                }
            }
        } else if (m_slicer_mode_freeform && active_clip &&
                   !ImGui::IsKeyDown(ImGuiKey_Space) &&
                   ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_drag_start_uv = local_mouse;
            m_drag_end_uv = local_mouse;
            if (m_freeform_tool == FreeformTool::Draw) {
                m_freeform_drag = FreeformDrag::Create;
            } else {
                commit_nudge_edit();
                int hit = -1;
                for (int index = static_cast<int>(active_clip->custom_frames.size()) - 1;
                     index >= 0; --index) {
                    if (contains(active_clip->custom_frames[index], local_mouse)) {
                        hit = index;
                        break;
                    }
                }
                m_selected_custom_frame = hit;
                if (hit >= 0) {
                    m_preview_frame = hit;
                    m_preview_playing = false;
                    m_drag_origin_rect = active_clip->custom_frames[hit];
                    m_freeform_drag_before = animation;
                    const float handle_radius = 9.0f * metrics.scale / m_sheet_zoom;
                    const ImVec2 bottom_right(
                        m_drag_origin_rect.x + m_drag_origin_rect.w,
                        m_drag_origin_rect.y + m_drag_origin_rect.h
                    );
                    const bool on_resize_handle =
                        std::abs(local_mouse.x - bottom_right.x) <= handle_radius &&
                        std::abs(local_mouse.y - bottom_right.y) <= handle_radius;
                    m_freeform_drag = on_resize_handle
                        ? FreeformDrag::Resize
                        : FreeformDrag::Move;
                }
            }
        }
    }

    if (m_slicer_mode_freeform && active_clip && m_freeform_drag != FreeformDrag::None) {
        m_drag_end_uv = local_mouse;
        if (m_freeform_drag == FreeformDrag::Create) {
            const SDL_FRect pending = m_fixed_draw_size
                ? fixed_frame_at(
                    m_drag_end_uv, m_fixed_draw_dimensions,
                    texture_width, texture_height
                )
                : normalized_frame(m_drag_start_uv, m_drag_end_uv);
            const ImVec2 minimum(
                canvas_position.x + pending.x * m_sheet_zoom,
                canvas_position.y + pending.y * m_sheet_zoom
            );
            const ImVec2 maximum(
                minimum.x + pending.w * m_sheet_zoom,
                minimum.y + pending.h * m_sheet_zoom
            );
            draw_list->AddRectFilled(
                minimum, maximum,
                ImGui::ColorConvertFloat4ToU32(with_alpha(palette.accent_hovered, 0.22f))
            );
            draw_list->AddRect(
                minimum, maximum,
                ImGui::ColorConvertFloat4ToU32(palette.accent_hovered), 0.0f, 0, 2.0f
            );
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
                   m_selected_custom_frame >= 0 &&
                   m_selected_custom_frame < static_cast<int>(active_clip->custom_frames.size())) {
            SDL_FRect& selected = active_clip->custom_frames[m_selected_custom_frame];
            if (m_freeform_drag == FreeformDrag::Move) {
                selected = m_drag_origin_rect;
                selected.x += local_mouse.x - m_drag_start_uv.x;
                selected.y += local_mouse.y - m_drag_start_uv.y;
            } else if (m_freeform_drag == FreeformDrag::Resize) {
                selected = m_drag_origin_rect;
                selected.w = std::max(1.0f, local_mouse.x - selected.x);
                selected.h = std::max(1.0f, local_mouse.y - selected.y);
            }
            clamp_frame(selected, texture_width, texture_height);
            refresh_draft_dirty();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (m_freeform_drag != FreeformDrag::Create && m_selected_custom_frame >= 0 &&
                m_selected_custom_frame < static_cast<int>(active_clip->custom_frames.size())) {
                active_clip->custom_frames[m_selected_custom_frame] = m_drag_origin_rect;
            }
            m_freeform_drag = FreeformDrag::None;
            m_freeform_drag_before.reset();
            refresh_draft_dirty();
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (m_freeform_drag == FreeformDrag::Create) {
                SDL_FRect created = m_fixed_draw_size
                    ? fixed_frame_at(
                        m_drag_end_uv, m_fixed_draw_dimensions,
                        texture_width, texture_height
                    )
                    : normalized_frame(m_drag_start_uv, m_drag_end_uv);
                clamp_frame(created, texture_width, texture_height);
                if (created.w >= 2.0f && created.h >= 2.0f) {
                    const AnimationComponent before = animation;
                    active_clip->custom_frames.push_back(created);
                    m_selected_custom_frame =
                        static_cast<int>(active_clip->custom_frames.size()) - 1;
                    m_preview_frame = m_selected_custom_frame;
                    m_preview_playing = false;
                    m_freeform_tool = FreeformTool::Select;
                    track_edit(
                        em, entity, before,
                        "AnimationStudio.AddCustomFrame",
                        "Add Custom Animation Frame", true
                    );
                }
            } else if (m_freeform_drag_before) {
                record_draft_edit(*m_freeform_drag_before);
            }
            m_freeform_drag = FreeformDrag::None;
            m_freeform_drag_before.reset();
        }
    }

    const bool delete_pressed = ImGui::IsKeyPressed(ImGuiKey_Delete) ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace);
    if (m_slicer_mode_freeform && hovered && !ImGui::GetIO().WantTextInput &&
        delete_pressed && active_clip && m_selected_custom_frame >= 0 &&
        m_selected_custom_frame < static_cast<int>(active_clip->custom_frames.size())) {
        const AnimationComponent before = animation;
        active_clip->custom_frames.erase(
            active_clip->custom_frames.begin() + m_selected_custom_frame
        );
        m_selected_custom_frame = std::min(
            m_selected_custom_frame,
            static_cast<int>(active_clip->custom_frames.size()) - 1
        );
        m_preview_frame = std::max(0, m_selected_custom_frame);
        track_edit(
            em, entity, before,
            "AnimationStudio.DeleteCustomFrame", "Delete Custom Animation Frame", true
        );
    }

    if (m_slicer_mode_freeform && active_clip && ImGui::IsWindowFocused() &&
        !ImGui::GetIO().WantTextInput && m_selected_custom_frame >= 0 &&
        m_selected_custom_frame < static_cast<int>(active_clip->custom_frames.size())) {
        const bool move_left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);
        const bool move_right = ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);
        const bool move_up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
        const bool move_down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);
        if (move_left || move_right || move_up || move_down) {
            if (!m_nudge_history_snapshot) m_nudge_history_snapshot = animation;
            SDL_FRect& selected = active_clip->custom_frames[m_selected_custom_frame];
            const float step = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;
            if (move_left) selected.x -= step;
            if (move_right) selected.x += step;
            if (move_up) selected.y -= step;
            if (move_down) selected.y += step;
            clamp_frame(selected, texture_width, texture_height);
            m_preview_frame = m_selected_custom_frame;
            m_preview_playing = false;
            m_nudge_last_input_time = ImGui::GetTime();
            refresh_draft_dirty();
        }
    }
    const bool arrow_held = ImGui::IsKeyDown(ImGuiKey_LeftArrow) ||
        ImGui::IsKeyDown(ImGuiKey_RightArrow) ||
        ImGui::IsKeyDown(ImGuiKey_UpArrow) ||
        ImGui::IsKeyDown(ImGuiKey_DownArrow);
    if (m_nudge_history_snapshot && !arrow_held &&
        ImGui::GetTime() - m_nudge_last_input_time >= 0.35) {
        commit_nudge_edit();
    }

    const float mouse_wheel = ImGui::GetIO().MouseWheel;
    if (ImGui::IsWindowHovered() && mouse_wheel != 0.0f) {
        if (ImGui::GetIO().KeyCtrl && hovered) {
            m_sheet_zoom = std::clamp(
                m_sheet_zoom * std::pow(1.15f, mouse_wheel),
                0.05f,
                8.0f
            );
            m_sheet_zoom_fit_pending = false;
        } else if (!ImGui::GetIO().KeyCtrl) {
            const float scroll_step = 42.0f * metrics.scale * mouse_wheel;
            if (ImGui::GetIO().KeyShift) {
                ImGui::SetScrollX(ImGui::GetScrollX() - scroll_step);
            } else {
                ImGui::SetScrollY(ImGui::GetScrollY() - scroll_step);
            }
        }
    }
    const bool sheet_panning = ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
        (ImGui::IsKeyDown(ImGuiKey_Space) &&
         ImGui::IsMouseDragging(ImGuiMouseButton_Left));
    if (ImGui::IsItemActive() && sheet_panning) {
        ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseDelta.x);
        ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void SpriteAnimationStudioPanel::render_timeline(EntityManager& em, EntityID entity) {
    auto& animation = draft_animation(em, entity);
    const auto& sprite = em.get_component<SpriteComponent>(entity);
    Texture* texture = sprite.texture;
    AnimationClip* clip = animation.get_clip(m_preview_clip_index);
    if (!clip || !texture || !texture->get_sdl_texture()) return;

    const int total_frames = clip->get_total_frames();
    const std::string detail = clip->name + "  |  " + std::to_string(total_frames) +
        (clip->custom_frames.empty() ? " frames" : " frames  |  Drag to reorder");
    EditorUi::section_heading("TIMELINE", detail.c_str());
    if (total_frames <= 0) {
        ImGui::TextDisabled("This clip has no frames.");
        return;
    }

    const float texture_width = static_cast<float>(texture->get_width());
    const float texture_height = static_cast<float>(texture->get_height());
    ImGui::BeginChild(
        "TimelineStrip", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    for (int index = 0; index < total_frames; ++index) {
        ImGui::PushID(index);
        const int frame_index = clip->custom_frames.empty()
            ? clip->start_frame + index
            : index;
        const SDL_FRect frame = animation.get_frame_rect(frame_index, clip);
        const ImVec2 uv_min(frame.x / texture_width, frame.y / texture_height);
        const ImVec2 uv_max(
            (frame.x + frame.w) / texture_width,
            (frame.y + frame.h) / texture_height
        );
        const bool frame_pressed = draw_timeline_frame(
                "##timeline_frame", *texture, uv_min, uv_max,
                index, m_preview_frame == frame_index);
        if (frame_pressed) {
            m_preview_frame = frame_index;
            if (!clip->custom_frames.empty()) m_selected_custom_frame = frame_index;
            m_preview_playing = false;
            m_preview_finished = false;
        }
        if (!clip->custom_frames.empty() && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(
                "ANIMATION_CUSTOM_FRAME", &index, sizeof(index)
            );
            ImGui::Text("Move frame #%d", index);
            ImGui::EndDragDropSource();
        }
        if (!clip->custom_frames.empty() && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                    "ANIMATION_CUSTOM_FRAME")) {
                const int source = *static_cast<const int*>(payload->Data);
                if (source >= 0 && source < total_frames && source != index) {
                    const AnimationComponent before = animation;
                    const SDL_FRect moved = clip->custom_frames[source];
                    clip->custom_frames.erase(clip->custom_frames.begin() + source);
                    clip->custom_frames.insert(clip->custom_frames.begin() + index, moved);
                    m_selected_custom_frame = index;
                    m_preview_frame = index;
                    m_preview_playing = false;
                    track_edit(
                        em, entity, before,
                        "AnimationStudio.ReorderCustomFrame",
                        "Reorder Custom Animation Frame", true
                    );
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
        if (index + 1 < total_frames) ImGui::SameLine();
    }
    ImGui::EndChild();
}

} // namespace Engine
