#include "ViewportPanel.hpp"
#include "SceneHierarchyPanel.hpp"
#include "../../scene/GenericScene.hpp"
#include "../../core/Log.hpp"
#include "../../core/Time.hpp"
#include <imgui.h>
#include <algorithm>

namespace Engine {

ViewportPanel::ViewportPanel(SDL_Renderer* renderer, SceneManager* scene_manager, int internal_width, int internal_height)
    : m_renderer(renderer), m_scene_manager(scene_manager), m_internal_width(internal_width), m_internal_height(internal_height) {
    
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
        } else {
            ENGINE_LOG_ERROR("[VIEWPORT] Failed to create Render Target Texture: {}", SDL_GetError());
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
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | 
                            ImGuiWindowFlags_NoScrollWithMouse | 
                            ImGuiWindowFlags_MenuBar;

    if (ImGui::Begin(m_name.c_str(), &is_open, flags)) {
        // --- 1. Top Viewport Control Bar ---
        if (ImGui::BeginMenuBar()) {
            bool is_play = m_scene_manager ? m_scene_manager->is_playing() : false;
            bool is_pause = m_scene_manager ? m_scene_manager->is_paused() : false;
            bool is_edit = m_scene_manager ? m_scene_manager->is_editing() : true;

            // Play Button
            if (is_play) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
            }
            if (ImGui::Button(" > Play ")) {
                if (m_scene_manager) m_scene_manager->start_play();
            }
            if (is_play) ImGui::PopStyleColor();

            ImGui::SameLine();

            // Pause Button
            if (is_pause) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.15f, 1.0f));
            }
            if (ImGui::Button(" || Pause ")) {
                if (m_scene_manager) m_scene_manager->pause_play();
            }
            if (is_pause) ImGui::PopStyleColor();

            ImGui::SameLine();

            // Stop / Edit Button
            if (is_edit) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.25f, 0.25f, 1.0f));
            }
            if (ImGui::Button(" [] Stop ")) {
                if (m_scene_manager) m_scene_manager->stop_play();
            }
            if (is_edit) ImGui::PopStyleColor();

            ImGui::Separator();

            // Scene Switcher Dropdown
            if (m_scene_manager && m_scene_manager->get_scene_count() > 0) {
                int current_idx = m_scene_manager->get_active_index();
                std::string current_name = m_scene_manager->get_active_scene() ? m_scene_manager->get_active_scene()->get_name() : "None";
                
                ImGui::TextDisabled("Scene:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::BeginCombo("##scene_selector", current_name.c_str())) {
                    const auto& scenes = m_scene_manager->get_scenes();
                    for (size_t i = 0; i < scenes.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        bool is_selected = (current_idx == static_cast<int>(i));
                        if (ImGui::Selectable(scenes[i]->get_name().c_str(), is_selected)) {
                            m_scene_manager->set_active_scene(i);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("%dx%d", m_internal_width, m_internal_height);

            ImGui::EndMenuBar();
        }

        // --- 2. Render Game Texture in Letterboxed Aspect Ratio ---
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x > 0.0f && avail.y > 0.0f && m_viewport_texture) {
            float target_aspect = static_cast<float>(m_internal_width) / static_cast<float>(m_internal_height);
            float window_aspect = avail.x / avail.y;

            float draw_w = avail.x;
            float draw_h = avail.y;

            if (window_aspect > target_aspect) {
                draw_w = avail.y * target_aspect;
                draw_h = avail.y;
            } else {
                draw_w = avail.x;
                draw_h = avail.x / target_aspect;
            }

            float offset_x = (avail.x - draw_w) * 0.5f;
            float offset_y = (avail.y - draw_h) * 0.5f;

            ImVec2 cursor_pos = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursor_pos.x + offset_x, cursor_pos.y + offset_y));

            ImGui::Image(reinterpret_cast<ImTextureID>(m_viewport_texture), ImVec2(draw_w, draw_h));

            m_is_hovered = ImGui::IsItemHovered();
            m_is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

            // Auto-focus Viewport window upon clicking inside the canvas
            if (m_is_hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
                ImGui::SetWindowFocus();
                m_is_focused = true;
            }

            ImVec2 img_min = ImGui::GetItemRectMin();
            ImVec2 img_max = ImGui::GetItemRectMax();
            ImVec2 img_size = ImVec2(img_max.x - img_min.x, img_max.y - img_min.y);
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 mouse_pos = io.MousePos;

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
            if (is_editing && m_is_hovered && generic_scene) {
                // Pan (Right Drag or Middle Drag)
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    ImVec2 drag_delta = ImGui::GetIO().MouseDelta;
                    generic_scene->pan_editor_camera(glm::vec2(drag_delta.x, drag_delta.y));
                }

                // Zoom (Mouse Wheel)
                float wheel = io.MouseWheel;
                if (wheel != 0.0f) {
                    float factor = (wheel > 0.0f) ? 1.15f : (1.0f / 1.15f);
                    generic_scene->zoom_editor_camera(factor);
                }

                // Focus on entity ('F' key)
                if (ImGui::IsKeyPressed(ImGuiKey_F) && m_hierarchy_panel) {
                    EntityID sel = m_hierarchy_panel->get_selected_entity();
                    if (sel != 0 && generic_scene->get_entity_manager().is_alive(sel) &&
                        generic_scene->get_entity_manager().has_component<TransformComponent>(sel)) {
                        const auto& tx = generic_scene->get_entity_manager().get_component<TransformComponent>(sel);
                        generic_scene->focus_editor_camera(glm::vec2(tx.position.x, tx.position.y));
                    }
                }
            }

            // --- 4. Camera Frustum Gizmo (Wireframe Game Frame) ---
            if (is_editing && active_cam) {
                glm::vec3 p_min_world(-960.0f, -540.0f, 0.0f);
                glm::vec3 p_max_world(960.0f, 540.0f, 0.0f);
                glm::vec2 s_min = active_cam->world_to_screen(p_min_world);
                glm::vec2 s_max = active_cam->world_to_screen(p_max_world);

                float scale_x = img_size.x / active_cam->get_viewport_size().x;
                float scale_y = img_size.y / active_cam->get_viewport_size().y;

                ImVec2 screen_rect_min(
                    img_min.x + s_min.x * scale_x,
                    img_min.y + s_max.y * scale_y
                );
                ImVec2 screen_rect_max(
                    img_min.x + s_max.x * scale_x,
                    img_min.y + s_min.y * scale_y
                );

                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(screen_rect_min, screen_rect_max, IM_COL32(148, 163, 184, 60), 0.0f, 0, 1.0f);
                dl->AddText(ImVec2(screen_rect_min.x + 8.0f, screen_rect_min.y + 8.0f), IM_COL32(148, 163, 184, 110), "Game Camera Frame (1920x1080)");
            }

            // --- 5. Selection Bounding Boxes, Corner Handles & Tag Badges ---
            const auto& selected_entities = m_hierarchy_panel ? m_hierarchy_panel->get_selected_entities() : std::unordered_set<EntityID>{};
            bool gizmo_interacted = false;

            if (is_editing && active_scene && active_cam && !selected_entities.empty()) {
                EntityManager& em = active_scene->get_entity_manager();
                float scale_x = img_size.x / active_cam->get_viewport_size().x;
                float scale_y = img_size.y / active_cam->get_viewport_size().y;
                ImDrawList* dl = ImGui::GetWindowDrawList();

                for (EntityID id : selected_entities) {
                    if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;

                    const auto& tx = em.get_component<TransformComponent>(id);
                    glm::vec2 spr_size = glm::vec2(64.0f, 64.0f);
                    if (em.has_component<SpriteComponent>(id)) {
                        spr_size = em.get_component<SpriteComponent>(id).size;
                    }
                    glm::vec2 half = spr_size * tx.world_scale * 0.5f;

                    glm::vec3 w_tl(tx.position.x - half.x, tx.position.y + half.y, 0.0f);
                    glm::vec3 w_br(tx.position.x + half.x, tx.position.y - half.y, 0.0f);

                    glm::vec2 s_tl = active_cam->world_to_screen(w_tl);
                    glm::vec2 s_br = active_cam->world_to_screen(w_br);

                    ImVec2 p_min(img_min.x + s_tl.x * scale_x, img_min.y + s_tl.y * scale_y);
                    ImVec2 p_max(img_min.x + s_br.x * scale_x, img_min.y + s_br.y * scale_y);

                    // Clean, subtle outline and tint matching the box selection styling
                    dl->AddRectFilled(p_min, p_max, IM_COL32(99, 102, 241, 30), 2.0f);
                    dl->AddRect(p_min, p_max, IM_COL32(129, 140, 248, 220), 2.0f, 0, 1.2f);
                }

                // Draw Centroid Transform Gizmo
                gizmo_interacted = m_gizmo.draw_gizmo(
                    em,
                    selected_entities,
                    *active_cam,
                    img_min,
                    img_size,
                    m_is_hovered
                );
            }

            // --- 6. Marquee Box Selection & Raycast Picking ---
            if (is_editing && m_is_hovered) {
                // Start Box Selection
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_gizmo.is_using() && !m_gizmo.is_hovered()) {
                    m_is_box_selecting = true;
                    m_box_select_start = mouse_pos;
                }

                // Render Active Box Selection Rectangle (Subtle elegant styling)
                if (m_is_box_selecting && io.MouseDown[ImGuiMouseButton_Left]) {
                    ImVec2 b_min(std::min(m_box_select_start.x, mouse_pos.x), std::min(m_box_select_start.y, mouse_pos.y));
                    ImVec2 b_max(std::max(m_box_select_start.x, mouse_pos.x), std::max(m_box_select_start.y, mouse_pos.y));

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(b_min, b_max, IM_COL32(99, 102, 241, 24), 2.0f);
                    dl->AddRect(b_min, b_max, IM_COL32(129, 140, 248, 150), 2.0f, 0, 1.0f);
                }

                // Finalize Box Selection or Single Click
                if (m_is_box_selecting && !io.MouseDown[ImGuiMouseButton_Left]) {
                    m_is_box_selecting = false;
                    float drag_dist = std::hypot(mouse_pos.x - m_box_select_start.x, mouse_pos.y - m_box_select_start.y);
                    bool is_multi_key = io.KeyShift || io.KeyCtrl;

                    float scale_x = img_size.x / active_cam->get_viewport_size().x;
                    float scale_y = img_size.y / active_cam->get_viewport_size().y;
                    EntityManager& em = active_scene->get_entity_manager();

                    if (drag_dist > 6.0f) {
                        // Box Selection AABB calculation
                        ImVec2 b_min(std::min(m_box_select_start.x, mouse_pos.x), std::min(m_box_select_start.y, mouse_pos.y));
                        ImVec2 b_max(std::max(m_box_select_start.x, mouse_pos.x), std::max(m_box_select_start.y, mouse_pos.y));

                        glm::vec2 w_p1 = active_cam->screen_to_world(glm::vec2((b_min.x - img_min.x) / scale_x, (b_min.y - img_min.y) / scale_y));
                        glm::vec2 w_p2 = active_cam->screen_to_world(glm::vec2((b_max.x - img_min.x) / scale_x, (b_max.y - img_min.y) / scale_y));

                        float w_min_x = std::min(w_p1.x, w_p2.x);
                        float w_max_x = std::max(w_p1.x, w_p2.x);
                        float w_min_y = std::min(w_p1.y, w_p2.y);
                        float w_max_y = std::max(w_p1.y, w_p2.y);

                        bool is_shift_strict = io.KeyShift;

                        std::unordered_set<EntityID> found_in_box;
                        em.for_each<TransformComponent, SpriteComponent>([&](EntityID id, const TransformComponent& tx, const SpriteComponent& sp) {
                            if (!tx.world_visible || !sp.visible) return;
                            glm::vec2 half = sp.size * tx.world_scale * 0.5f;
                            glm::vec2 pos = glm::vec2(tx.position.x, tx.position.y);

                            if (is_shift_strict) {
                                // Strict Inclusion: Entity must be 100% COMPLETELY contained within the box
                                if (pos.x - half.x >= w_min_x && pos.x + half.x <= w_max_x &&
                                    pos.y - half.y >= w_min_y && pos.y + half.y <= w_max_y) {
                                    found_in_box.insert(id);
                                }
                            } else {
                                // Standard Crossing: Entity touches or overlaps the box
                                if (pos.x + half.x >= w_min_x && pos.x - half.x <= w_max_x &&
                                    pos.y + half.y >= w_min_y && pos.y - half.y <= w_max_y) {
                                    found_in_box.insert(id);
                                }
                            }
                        });

                        bool is_additive = io.KeyCtrl || io.KeySuper;
                        if (is_additive && m_hierarchy_panel) {
                            auto current_set = m_hierarchy_panel->get_selected_entities();
                            for (EntityID fid : found_in_box) current_set.insert(fid);
                            m_hierarchy_panel->set_selected_entities(current_set);
                        } else if (m_hierarchy_panel) {
                            m_hierarchy_panel->set_selected_entities(found_in_box);
                        }
                    } else {
                        // Single point raycast picking
                        glm::vec2 mouse_in_viewport((mouse_pos.x - img_min.x) / scale_x, (mouse_pos.y - img_min.y) / scale_y);
                        glm::vec2 mouse_world = active_cam->screen_to_world(mouse_in_viewport);

                        EntityID hit_entity = 0;
                        float closest_dist = 999999.0f;

                        em.for_each<TransformComponent, SpriteComponent>([&](EntityID id, const TransformComponent& tx, const SpriteComponent& sp) {
                            if (!tx.world_visible || !sp.visible) return;
                            glm::vec2 half = sp.size * tx.world_scale * 0.5f;
                            glm::vec2 pos = glm::vec2(tx.position.x, tx.position.y);

                            if (mouse_world.x >= pos.x - half.x && mouse_world.x <= pos.x + half.x &&
                                mouse_world.y >= pos.y - half.y && mouse_world.y <= pos.y + half.y) {
                                float dist = glm::distance(mouse_world, pos);
                                if (dist < closest_dist) {
                                    closest_dist = dist;
                                    hit_entity = id;
                                }
                            }
                        });

                        if (m_hierarchy_panel) {
                            m_hierarchy_panel->select_entity(hit_entity, is_multi_key);
                        }
                    }
                }
            }

            // --- 7. Duplicate Selected Entities (Ctrl + D) ---
            if (is_editing && m_is_hovered && (io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_D) && m_hierarchy_panel && active_scene) {
                const auto& cur_selected = m_hierarchy_panel->get_selected_entities();
                if (!cur_selected.empty()) {
                    EntityManager& em = active_scene->get_entity_manager();
                    std::unordered_set<EntityID> cloned_ids;

                    for (EntityID src_id : cur_selected) {
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
                        m_hierarchy_panel->set_selected_entities(cloned_ids);
                    }
                }
            }

            // Deselect with Escape key inside Viewport
            if (m_is_hovered && ImGui::IsKeyPressed(ImGuiKey_Escape) && m_hierarchy_panel) {
                m_hierarchy_panel->clear_selection();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
