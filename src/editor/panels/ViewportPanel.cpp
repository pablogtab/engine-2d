#include "ViewportPanel.hpp"
#include "TilemapPalettePanel.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include "../../scene/GenericScene.hpp"
#include "../../core/Log.hpp"
#include "../../core/Time.hpp"
#include "../../ecs/Components.hpp"
#include <imgui.h>
#include <algorithm>

namespace Engine {

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
            if (is_editing && active_cam) {
                glm::vec3 p_min_world(-960.0f, -540.0f, 0.0f);
                glm::vec3 p_max_world(960.0f, 540.0f, 0.0f);
                glm::vec2 s_min = active_cam->world_to_screen(p_min_world);
                glm::vec2 s_max = active_cam->world_to_screen(p_max_world);

                float scale_x = img_size.x / active_cam->get_viewport_size().x;
                float scale_y = img_size.y / active_cam->get_viewport_size().y;

                ImVec2 screen_rect_min(img_min.x + s_min.x * scale_x, img_min.y + s_max.y * scale_y);
                ImVec2 screen_rect_max(img_min.x + s_max.x * scale_x, img_min.y + s_min.y * scale_y);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(screen_rect_min, screen_rect_max, IM_COL32(148, 163, 184, 60), 0.0f, 0, 1.0f);
                dl->AddText(ImVec2(screen_rect_min.x + 8.0f, screen_rect_min.y + 8.0f), IM_COL32(148, 163, 184, 110), "Game Camera Frame (1920x1080)");
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
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
