#include "ViewportPanel.hpp"
#include "TilemapPalettePanel.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../scene/GenericScene.hpp"
#include "../../core/Log.hpp"
#include "../../core/Time.hpp"
#include "../../ecs/Components.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

enum class ToolbarIcon {
    Move,
    Rotate,
    Scale,
    Space,
    Snap,
    Settings
};

void draw_toolbar_icon(ImDrawList* draw_list, ToolbarIcon icon, const ImVec2& center, ImU32 color, bool world_space = false) {
    const auto line = [draw_list, color](const ImVec2& from, const ImVec2& to, float thickness = 1.8f) {
        draw_list->AddLine(from, to, color, thickness);
    };
    const auto arrow = [draw_list, color](const ImVec2& tip, const ImVec2& direction) {
        const ImVec2 perpendicular(-direction.y, direction.x);
        const ImVec2 left(tip.x - direction.x * 5.0f + perpendicular.x * 3.0f, tip.y - direction.y * 5.0f + perpendicular.y * 3.0f);
        const ImVec2 right(tip.x - direction.x * 5.0f - perpendicular.x * 3.0f, tip.y - direction.y * 5.0f - perpendicular.y * 3.0f);
        draw_list->AddTriangleFilled(tip, left, right, color);
    };

    switch (icon) {
        case ToolbarIcon::Move: {
            line(ImVec2(center.x - 8.0f, center.y), ImVec2(center.x + 8.0f, center.y));
            line(ImVec2(center.x, center.y - 8.0f), ImVec2(center.x, center.y + 8.0f));
            arrow(ImVec2(center.x + 8.0f, center.y), ImVec2(1.0f, 0.0f));
            arrow(ImVec2(center.x - 8.0f, center.y), ImVec2(-1.0f, 0.0f));
            arrow(ImVec2(center.x, center.y + 8.0f), ImVec2(0.0f, 1.0f));
            arrow(ImVec2(center.x, center.y - 8.0f), ImVec2(0.0f, -1.0f));
            break;
        }
        case ToolbarIcon::Rotate: {
            draw_list->PathArcTo(center, 8.0f, 0.35f, 5.55f, 20);
            draw_list->PathStroke(color, 0, 1.8f);
            arrow(ImVec2(center.x + 7.5f, center.y - 3.0f), ImVec2(0.45f, -0.9f));
            break;
        }
        case ToolbarIcon::Scale: {
            line(ImVec2(center.x - 7.0f, center.y + 7.0f), ImVec2(center.x + 7.0f, center.y - 7.0f));
            draw_list->AddRectFilled(ImVec2(center.x - 10.0f, center.y + 4.0f), ImVec2(center.x - 4.0f, center.y + 10.0f), color, 1.0f);
            draw_list->AddRectFilled(ImVec2(center.x + 4.0f, center.y - 10.0f), ImVec2(center.x + 10.0f, center.y - 4.0f), color, 1.0f);
            break;
        }
        case ToolbarIcon::Space: {
            if (world_space) {
                draw_list->AddCircle(center, 8.0f, color, 16, 1.6f);
                draw_list->AddEllipse(ImVec2(center.x, center.y), ImVec2(3.5f, 8.0f), color, 0.0f, 16, 1.2f);
                line(ImVec2(center.x - 7.5f, center.y), ImVec2(center.x + 7.5f, center.y), 1.2f);
            } else {
                line(center, ImVec2(center.x + 9.0f, center.y), 1.8f);
                line(center, ImVec2(center.x, center.y - 9.0f), 1.8f);
                arrow(ImVec2(center.x + 9.0f, center.y), ImVec2(1.0f, 0.0f));
                arrow(ImVec2(center.x, center.y - 9.0f), ImVec2(0.0f, -1.0f));
            }
            break;
        }
        case ToolbarIcon::Snap: {
            draw_list->PathArcTo(ImVec2(center.x - 4.0f, center.y), 5.0f, 1.57f, 4.72f, 12);
            draw_list->PathStroke(color, 0, 2.4f);
            draw_list->AddLine(ImVec2(center.x - 4.0f, center.y + 5.0f), ImVec2(center.x + 5.0f, center.y + 5.0f), color, 2.4f);
            break;
        }
        case ToolbarIcon::Settings: {
            for (int row = -1; row <= 1; ++row) {
                const float y = center.y + static_cast<float>(row) * 5.0f;
                line(ImVec2(center.x - 8.0f, y), ImVec2(center.x + 8.0f, y), 1.4f);
                draw_list->AddCircleFilled(ImVec2(center.x + (row == 0 ? -2.0f : (row < 0 ? 4.0f : -5.0f)), y), 2.3f, color);
            }
            break;
        }
    }
}

bool toolbar_icon_button(const char* id, ToolbarIcon icon, const char* tooltip, bool active = false, bool world_space = false) {
    constexpr ImVec2 button_size(28.0f, 28.0f);
    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("##icon", button_size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 background = active ? IM_COL32(79, 69, 209, 255) : (hovered ? IM_COL32(56, 61, 82, 255) : IM_COL32(37, 41, 55, 235));
    const ImU32 icon_color = active || hovered ? IM_COL32(245, 246, 255, 255) : IM_COL32(187, 193, 215, 255);
    draw_list->AddRectFilled(rect_min, rect_max, background, 4.0f);
    draw_toolbar_icon(draw_list, icon, ImVec2((rect_min.x + rect_max.x) * 0.5f, (rect_min.y + rect_max.y) * 0.5f), icon_color, world_space);
    if (hovered) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return pressed;
}

} // namespace

ViewportPanel::ViewportPanel(
    SDL_Renderer* renderer, 
    SceneManager* scene_manager, 
    std::shared_ptr<EditorSelection> selection,
    std::shared_ptr<EditorCommandHistory> command_history,
    int internal_width, 
    int internal_height
)
    : m_renderer(renderer), 
      m_scene_manager(scene_manager), 
      m_selection(selection),
      m_command_history(command_history),
      m_internal_width(internal_width), 
      m_internal_height(internal_height) {
    
    if (!m_selection) {
        m_selection = std::make_shared<EditorSelection>();
    }

    if (m_renderer) {
        m_viewport_texture = SDL_CreateTexture(
            m_renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET,
            m_internal_width,
            m_internal_height
        );
        if (m_viewport_texture) {
            SDL_SetTextureScaleMode(m_viewport_texture, SDL_SCALEMODE_LINEAR);
            ENGINE_LOG_INFO("[VIEWPORT] Created Render Target Texture: {}x{}", m_internal_width, m_internal_height);
        }
    }

    Time::set_time_scale(0.0f); // Default to Edit Mode (paused) on launch
}

ViewportPanel::~ViewportPanel() {
    if (m_viewport_texture) {
        SDL_DestroyTexture(m_viewport_texture);
        m_viewport_texture = nullptr;
    }
}

void ViewportPanel::begin_render() {
    if (m_renderer && m_viewport_texture) {
        SDL_SetRenderTarget(m_renderer, m_viewport_texture);
        SDL_SetRenderDrawColor(m_renderer, 15, 18, 25, 255);
        SDL_RenderClear(m_renderer);
    }
}

void ViewportPanel::end_render() {
    if (m_renderer) {
        SDL_SetRenderTarget(m_renderer, nullptr);
    }
}

void ViewportPanel::on_gui() {
    if (!is_open) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(m_name.c_str(), &is_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        // --- 1. Top Transport Controls (Play / Pause / Stop) ---
        ImGui::SetCursorPos(ImVec2(10.0f, 32.0f));
        ScenePlayState play_state = get_play_state();

        if (play_state == ScenePlayState::Edit) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.38f, 0.85f));
            if (ImGui::Button(" > Play ", ImVec2(75, 24))) {
                if (m_scene_manager) m_scene_manager->start_play();
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.85f));
            if (ImGui::Button(" [] Stop ", ImVec2(75, 24))) {
                if (m_scene_manager) m_scene_manager->stop_play();
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (play_state == ScenePlayState::Play) {
                if (ImGui::Button(" || Pause ", ImVec2(75, 24))) {
                    if (m_scene_manager) m_scene_manager->pause_play();
                }
            } else {
                if (ImGui::Button(" > Resume ", ImVec2(75, 24))) {
                    if (m_scene_manager) m_scene_manager->resume_play();
                }
            }
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        if (m_scene_manager && m_scene_manager->get_scene_count() > 0) {
            ImGui::SetNextItemWidth(260.0f);
            Scene* current_sc = m_scene_manager->get_active_scene();
            std::string preview_name = current_sc ? ("🎬 " + current_sc->get_name()) : "Select Scene";
            if (ImGui::BeginCombo("##viewport_scene_select", preview_name.c_str())) {
                const auto& scenes = m_scene_manager->get_scenes();
                for (size_t i = 0; i < scenes.size(); ++i) {
                    if (!scenes[i]) continue;
                    bool is_sel = (m_scene_manager->get_active_index() == static_cast<int>(i));
                    std::string item_name = std::to_string(i + 1) + ". " + scenes[i]->get_name();
                    if (ImGui::Selectable(item_name.c_str(), is_sel)) {
                        m_scene_manager->set_active_scene(i);
                        m_scene_manager->resize(m_internal_width, m_internal_height, 1.0f);
                    }
                    if (is_sel) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 250.0f);
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "Mode: %s | FPS: %.0f", 
            (play_state == ScenePlayState::Edit ? "EDIT" : (play_state == ScenePlayState::Play ? "PLAY" : "PAUSED")),
            ImGui::GetIO().Framerate);

        ImGui::Spacing();

        // --- 2. Canvas Image Rendering (Aspect-Ratio Fitted) ---
        ImVec2 avail_size = ImGui::GetContentRegionAvail();
        if (avail_size.x > 0.0f && avail_size.y > 0.0f && m_viewport_texture) {
            float target_aspect = static_cast<float>(m_internal_width) / static_cast<float>(m_internal_height);
            float avail_aspect = avail_size.x / avail_size.y;

            ImVec2 render_size;
            if (avail_aspect > target_aspect) {
                render_size.y = avail_size.y;
                render_size.x = avail_size.y * target_aspect;
            } else {
                render_size.x = avail_size.x;
                render_size.y = avail_size.x / target_aspect;
            }

            ImVec2 offset = ImVec2((avail_size.x - render_size.x) * 0.5f, (avail_size.y - render_size.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset.x, ImGui::GetCursorPosY() + offset.y));

            ImGui::Image(reinterpret_cast<ImTextureID>(m_viewport_texture), render_size);

            m_is_hovered = ImGui::IsItemHovered();
            m_is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

            if (m_is_hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
                ImGui::SetWindowFocus();
                m_is_focused = true;
            }

            ImVec2 img_min = ImGui::GetItemRectMin();
            ImVec2 img_max = ImGui::GetItemRectMax();
            ImVec2 img_size = ImVec2(img_max.x - img_min.x, img_max.y - img_min.y);
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 mouse_pos = io.MousePos;

            // Transform toolbar: viewport-only shortcuts avoid stealing text input.
            if (m_is_focused && !io.WantTextInput && !m_gizmo.is_using() && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt) {
                if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmo.set_tool(TransformTool::Move);
                if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmo.set_tool(TransformTool::Rotate);
                if (ImGui::IsKeyPressed(ImGuiKey_S)) m_gizmo.set_tool(TransformTool::Scale);
            }

            // Compact, draggable transform toolbox. Its coordinates are screen-space
            // but clamped to the canvas, so it remains available after layout changes.
            constexpr ImVec2 toolbox_size(216.0f, 38.0f);
            const ImVec2 toolbox_min(img_min.x + 6.0f, img_min.y + 6.0f);
            const ImVec2 toolbox_max(
                std::max(toolbox_min.x, img_max.x - toolbox_size.x - 6.0f),
                std::max(toolbox_min.y, img_max.y - toolbox_size.y - 6.0f)
            );
            if (m_transform_toolbar_pos.x < 0.0f || m_transform_toolbar_pos.y < 0.0f) {
                m_transform_toolbar_pos = toolbox_min;
            }
            m_transform_toolbar_pos.x = std::clamp(m_transform_toolbar_pos.x, toolbox_min.x, toolbox_max.x);
            m_transform_toolbar_pos.y = std::clamp(m_transform_toolbar_pos.y, toolbox_min.y, toolbox_max.y);

            ImGui::SetCursorScreenPos(m_transform_toolbar_pos);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.085f, 0.12f, 0.96f));
            ImGui::BeginChild("TransformToolbox", toolbox_size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            bool is_toolbox_hovered = ImGui::IsWindowHovered();

            ImGui::InvisibleButton("##toolbox_grip", ImVec2(16.0f, 28.0f));
            const ImVec2 grip_min = ImGui::GetItemRectMin();
            const ImVec2 grip_max = ImGui::GetItemRectMax();
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                m_transform_toolbar_pos.x += io.MouseDelta.x;
                m_transform_toolbar_pos.y += io.MouseDelta.y;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag toolbox");
            ImDrawList* toolbox_draw_list = ImGui::GetWindowDrawList();
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 2; ++column) {
                    toolbox_draw_list->AddCircleFilled(
                        ImVec2(grip_min.x + 5.0f + static_cast<float>(column) * 5.0f, grip_min.y + 9.0f + static_cast<float>(row) * 5.0f),
                        1.0f,
                        IM_COL32(140, 148, 174, 210)
                    );
                }
            }
            ImGui::SameLine(0.0f, 2.0f);

            if (toolbar_icon_button("move", ToolbarIcon::Move, "Move (W)", m_gizmo.get_tool() == TransformTool::Move)) {
                m_gizmo.set_tool(TransformTool::Move);
            }
            ImGui::SameLine(0.0f, 2.0f);
            if (toolbar_icon_button("rotate", ToolbarIcon::Rotate, "Rotate (R)", m_gizmo.get_tool() == TransformTool::Rotate)) {
                m_gizmo.set_tool(TransformTool::Rotate);
            }
            ImGui::SameLine(0.0f, 2.0f);
            if (toolbar_icon_button("scale", ToolbarIcon::Scale, "Scale (S)", m_gizmo.get_tool() == TransformTool::Scale)) {
                m_gizmo.set_tool(TransformTool::Scale);
            }
            ImGui::SameLine(0.0f, 6.0f);

            const bool multiple_selection = m_selection && m_selection->get_selected().size() > 1;
            if (multiple_selection) m_gizmo.set_space(GizmoSpace::World);
            const GizmoSpace space = m_gizmo.get_space();
            const char* space_tooltip = multiple_selection
                ? "Space: World (multiple selections use World)"
                : (space == GizmoSpace::World ? "Space: World (click for Local)" : "Space: Local (click for World)");
            if (toolbar_icon_button("space", ToolbarIcon::Space, space_tooltip, true, space == GizmoSpace::World) && !multiple_selection) {
                m_gizmo.set_space(space == GizmoSpace::Local ? GizmoSpace::World : GizmoSpace::Local);
            }
            ImGui::SameLine(0.0f, 2.0f);

            auto& snap = m_gizmo.snap_settings();
            if (toolbar_icon_button("snap", ToolbarIcon::Snap, "Snap (Shift temporarily inverts it)", snap.enabled)) {
                snap.enabled = !snap.enabled;
            }
            ImGui::SameLine(0.0f, 2.0f);
            if (toolbar_icon_button("settings", ToolbarIcon::Settings, "Configure snap increments")) {
                ImGui::OpenPopup("Transform Snap Settings");
            }

            if (ImGui::BeginPopup("Transform Snap Settings")) {
                is_toolbox_hovered = true;
                ImGui::TextUnformatted("Snap increments");
                ImGui::Separator();
                ImGui::SetNextItemWidth(140.0f);
                ImGui::DragFloat("Position", &snap.position_step, 1.0f, 0.1f, 1024.0f, "%.0f");
                ImGui::SetNextItemWidth(140.0f);
                ImGui::DragFloat("Rotation", &snap.rotation_step, 1.0f, 1.0f, 180.0f, "%.0f deg");
                float scale_snap_percent = snap.scale_step * 100.0f;
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::DragFloat("Scale", &scale_snap_percent, 1.0f, 1.0f, 100.0f, "%.0f%%")) {
                    snap.scale_step = scale_snap_percent / 100.0f;
                }
                ImGui::EndPopup();
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            m_is_hovered = m_is_hovered && !is_toolbox_hovered;

            if (img_size.x > 0.0f && img_size.y > 0.0f) {
                float norm_x = (mouse_pos.x - img_min.x) / img_size.x;
                float norm_y = (mouse_pos.y - img_min.y) / img_size.y;
                m_viewport_mouse_pos = glm::vec2(norm_x * static_cast<float>(m_internal_width), 
                                                 norm_y * static_cast<float>(m_internal_height));
            }

            bool is_editing = m_scene_manager ? m_scene_manager->is_editing() : true;
            Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
            auto* generic_scene = dynamic_cast<GenericScene*>(active_scene);
            Camera2D* active_cam = generic_scene ? &generic_scene->get_editor_camera() : nullptr;

            // --- 3. Viewport Camera Navigation (Pan / Zoom / Focus 'F') ---
            if (is_editing && active_cam && generic_scene) {
                EntityID focus_target = m_selection ? m_selection->get_primary() : 0;
                m_camera_controller.handle_input(generic_scene, m_is_hovered, focus_target);
            }

            // --- 4. Camera Frustum Gizmo (Wireframe Game Frame) ---
            if (is_editing && active_cam && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                
                glm::vec2 cam_pos(0.0f, 0.0f);
                float cam_zoom = 1.0f;
                glm::vec2 vp_sz(1920.0f, 1080.0f);
                bool is_cam_selected = false;
                bool found_camera = false;

                em.for_each<CameraComponent, TransformComponent>([&](EntityID id, const CameraComponent& cc, const TransformComponent& tc) {
                    bool sel = m_selection && m_selection->is_selected(id);
                    if (!found_camera || sel || cc.is_primary) {
                        cam_pos = glm::vec2(tc.position.x, tc.position.y);
                        cam_zoom = cc.zoom;
                        vp_sz = cc.viewport_size;
                        is_cam_selected = sel;
                        found_camera = true;
                    }
                });

                float half_w = (vp_sz.x * 0.5f) / (cam_zoom > 0.001f ? cam_zoom : 1.0f);
                float half_h = (vp_sz.y * 0.5f) / (cam_zoom > 0.001f ? cam_zoom : 1.0f);

                glm::vec3 p_min_world(cam_pos.x - half_w, cam_pos.y - half_h, 0.0f);
                glm::vec3 p_max_world(cam_pos.x + half_w, cam_pos.y + half_h, 0.0f);
                glm::vec2 s_min = active_cam->world_to_screen(p_min_world);
                glm::vec2 s_max = active_cam->world_to_screen(p_max_world);

                float scale_x = img_size.x / active_cam->get_viewport_size().x;
                float scale_y = img_size.y / active_cam->get_viewport_size().y;

                ImVec2 screen_rect_min(img_min.x + s_min.x * scale_x, img_min.y + s_max.y * scale_y);
                ImVec2 screen_rect_max(img_min.x + s_max.x * scale_x, img_min.y + s_min.y * scale_y);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 frame_col = is_cam_selected ? IM_COL32(59, 130, 246, 220) : IM_COL32(148, 163, 184, 80);
                float frame_thick = is_cam_selected ? 2.0f : 1.0f;
                dl->AddRect(screen_rect_min, screen_rect_max, frame_col, 0.0f, 0, frame_thick);
                
                char cam_label[64];
                snprintf(cam_label, sizeof(cam_label), "🎥 Camera Frame (%.0fx%.0f | Zoom: %.2fx)", vp_sz.x, vp_sz.y, cam_zoom);
                dl->AddText(ImVec2(screen_rect_min.x + 8.0f, screen_rect_min.y + 8.0f), is_cam_selected ? IM_COL32(96, 165, 250, 255) : IM_COL32(148, 163, 184, 150), cam_label);
            }

            // --- 5. Selection Outlines & Centroid Transform Gizmo ---
            bool is_tilemap_painting = (m_palette_panel && m_palette_panel->is_painting_active());
            bool is_gizmo_active = false;

            // --- 5. Selection Outlines & Transform Gizmo (Only when NOT actively painting tiles) ---
            if (is_editing && active_scene && active_cam && m_selection && !m_selection->empty() && !is_tilemap_painting) {
                // A. Selection Outlines
                m_selection_tool.render_selection_outlines(
                    active_scene->get_entity_manager(),
                    *m_selection,
                    *active_cam,
                    img_min,
                    img_size
                );

                // B. Transform Gizmo (Records to Command History on Drag Release)
                is_gizmo_active = m_gizmo.draw_gizmo(
                    active_scene->get_entity_manager(),
                    m_selection->get_selected(),
                    *active_cam,
                    img_min,
                    img_size,
                    m_is_hovered,
                    m_command_history.get()
                );
            }

            // --- 6. Marquee Box Selection & Raycast Picking (Active when NOT painting) ---
            if (is_editing && active_cam && m_selection && !is_tilemap_painting) {
                m_selection_tool.handle_selection_input(
                    active_scene,
                    *active_cam,
                    *m_selection,
                    img_min,
                    img_size,
                    m_is_hovered,
                    is_gizmo_active || m_gizmo.is_hovered() || m_gizmo.is_using()
                );
            }

            // --- 6.5 Interactive Tilemap Painting Tool (Brush / Box Fill / Eraser) ---
            if (is_editing && active_scene && active_cam && is_tilemap_painting) {
                EntityManager& em = active_scene->get_entity_manager();
                EntityID tilemap_entity = 0;

                // 1. Check if currently selected entity has a TilemapComponent
                if (m_selection && !m_selection->empty()) {
                    EntityID primary = m_selection->get_primary();
                    if (em.is_alive(primary) && em.has_component<TilemapComponent>(primary)) {
                        tilemap_entity = primary;
                    }
                }

                // 2. Fallback: Find first entity in active scene with a TilemapComponent
                if (tilemap_entity == 0) {
                    em.for_each<TilemapComponent>([&](EntityID id, const TilemapComponent&) {
                        if (tilemap_entity == 0) tilemap_entity = id;
                    });
                }

                // 3. Fallback: If no Tilemap entity exists yet, create one automatically!
                if (tilemap_entity == 0) {
                    tilemap_entity = em.create_entity();
                    em.add_component<TagComponent>(tilemap_entity, TagComponent{"Tilemap"});
                    em.add_component<TransformComponent>(tilemap_entity, TransformComponent{glm::vec3(0.0f)});
                    em.add_component<TilemapComponent>(tilemap_entity, TilemapComponent{});
                }

                if (m_selection && m_selection->empty()) {
                    m_selection->select(tilemap_entity);
                }

                m_tilemap_painting_tool.handle_viewport_painting(
                    active_scene,
                    tilemap_entity,
                    *m_palette_panel,
                    *active_cam,
                    img_min,
                    img_size,
                    m_is_hovered,
                    m_command_history.get()
                );
            }

            // --- 7. Duplicate Selected Entities ---
            if (is_editing && m_is_hovered && ShortcutManager::get().is_triggered("editor.duplicate") && m_selection && !m_selection->empty() && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                std::unordered_set<EntityID> cloned_ids;

                for (EntityID src_id : m_selection->get_selected()) {
                    if (!em.is_alive(src_id)) continue;
                    EntityID clone_id = em.create_entity();

                    if (em.has_component<TagComponent>(src_id)) {
                        em.add_component<TagComponent>(clone_id, TagComponent{em.get_component<TagComponent>(src_id).tag + "_Copy"});
                    }
                    if (em.has_component<TransformComponent>(src_id)) {
                        auto tx = em.get_component<TransformComponent>(src_id);
                        tx.position += glm::vec3(30.0f, 30.0f, 0.0f);
                        tx.world_position = tx.position;
                        em.add_component<TransformComponent>(clone_id, tx);
                    }
                    if (em.has_component<SpriteComponent>(src_id)) {
                        em.add_component<SpriteComponent>(clone_id, em.get_component<SpriteComponent>(src_id));
                    }
                    if (em.has_component<RigidBodyComponent>(src_id)) {
                        em.add_component<RigidBodyComponent>(clone_id, em.get_component<RigidBodyComponent>(src_id));
                    }
                    if (em.has_component<ColliderComponent>(src_id)) {
                        em.add_component<ColliderComponent>(clone_id, em.get_component<ColliderComponent>(src_id));
                    }

                    cloned_ids.insert(clone_id);
                }

                if (!cloned_ids.empty()) {
                    m_selection->set_selection(cloned_ids);
                }
            }

            // --- 8. Delete Selected Entities ---
            if (is_editing && (m_is_focused || m_is_hovered) && !io.WantTextInput && ShortcutManager::get().is_triggered("editor.delete") && m_selection && !m_selection->empty() && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                std::vector<EntityID> to_delete;
                for (EntityID s_id : m_selection->get_selected()) {
                    if (em.is_alive(s_id)) to_delete.push_back(s_id);
                }

                if (!to_delete.empty()) {
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, to_delete));
                    } else {
                        for (EntityID s_id : to_delete) em.destroy_entity(s_id);
                        m_selection->clear();
                    }
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
