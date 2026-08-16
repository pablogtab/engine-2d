#include "ViewportSelectionTool.hpp"
#include "../../ecs/Components.hpp"
#include "../../tilemap/Tilemap.hpp"
#include <algorithm>
#include <cmath>

namespace Engine {

void ViewportSelectionTool::render_selection_outlines(
    EntityManager& em,
    const EditorSelection& selection,
    const Camera2D& camera,
    const ImVec2& img_min,
    const ImVec2& img_size
) {
    if (selection.empty()) return;

    float scale_x = img_size.x / camera.get_viewport_size().x;
    float scale_y = img_size.y / camera.get_viewport_size().y;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (EntityID id : selection.get_selected()) {
        if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;

        const auto& tx = em.get_component<TransformComponent>(id);
        glm::vec3 w_tl;
        glm::vec3 w_br;

        if (em.has_component<TilemapComponent>(id)) {
            const auto& tm = em.get_component<TilemapComponent>(id);
            if (tm.empty()) {
                glm::vec2 half = glm::vec2(tm.tile_size * tx.world_scale.x * 0.5f, tm.tile_size * tx.world_scale.y * 0.5f);
                w_tl = glm::vec3(tx.position.x - half.x, tx.position.y + half.y, tx.position.z);
                w_br = glm::vec3(tx.position.x + half.x, tx.position.y - half.y, tx.position.z);
            } else {
                int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
                tm.get_bounds(min_x, min_y, max_x, max_y);
                float tile_sz_x = tm.tile_size * tx.world_scale.x;
                float tile_sz_y = tm.tile_size * tx.world_scale.y;

                float left   = tx.position.x + (static_cast<float>(min_x) * tile_sz_x);
                float top    = tx.position.y - (static_cast<float>(min_y) * tile_sz_y);
                float right  = tx.position.x + (static_cast<float>(max_x + 1) * tile_sz_x);
                float bottom = tx.position.y - (static_cast<float>(max_y + 1) * tile_sz_y);

                w_tl = glm::vec3(left, top, tx.position.z);
                w_br = glm::vec3(right, bottom, tx.position.z);
            }
        } else {
            glm::vec2 spr_size = glm::vec2(64.0f, 64.0f);
            if (em.has_component<SpriteComponent>(id)) {
                spr_size = em.get_component<SpriteComponent>(id).size;
            } else if (em.has_component<ColliderComponent>(id)) {
                const auto& col = em.get_component<ColliderComponent>(id);
                if (!col.shapes.empty()) spr_size = col.shapes[0].size;
            }

            glm::vec2 half = spr_size * tx.world_scale * 0.5f;
            w_tl = glm::vec3(tx.position.x - half.x, tx.position.y + half.y, 0.0f);
            w_br = glm::vec3(tx.position.x + half.x, tx.position.y - half.y, 0.0f);
        }

        glm::vec2 s_tl = camera.world_to_screen(w_tl);
        glm::vec2 s_br = camera.world_to_screen(w_br);

        ImVec2 p_min(img_min.x + s_tl.x * scale_x, img_min.y + s_tl.y * scale_y);
        ImVec2 p_max(img_min.x + s_br.x * scale_x, img_min.y + s_br.y * scale_y);

        ImVec2 rect_min(std::min(p_min.x, p_max.x), std::min(p_min.y, p_max.y));
        ImVec2 rect_max(std::max(p_min.x, p_max.x), std::max(p_min.y, p_max.y));

        // Subtle, minimalist indigo outline matching the selection theme
        dl->AddRectFilled(rect_min, rect_max, IM_COL32(99, 102, 241, 30), 2.0f);
        dl->AddRect(rect_min, rect_max, IM_COL32(129, 140, 248, 220), 2.0f, 0, 1.2f);
    }
}

void ViewportSelectionTool::handle_selection_input(
    Scene* active_scene,
    const Camera2D& camera,
    EditorSelection& selection,
    const ImVec2& img_min,
    const ImVec2& img_size,
    bool is_viewport_hovered,
    bool is_gizmo_active
) {
    if (!active_scene || !is_viewport_hovered) return;

    EntityManager& em = active_scene->get_entity_manager();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos = io.MousePos;

    float scale_x = img_size.x / camera.get_viewport_size().x;
    float scale_y = img_size.y / camera.get_viewport_size().y;

    bool is_additive = io.KeyCtrl || io.KeyShift;
    bool is_alt_strict = io.KeyAlt;

    // 1. Box Selection Drag Start
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !is_gizmo_active) {
        m_is_box_selecting = true;
        m_box_select_start = mouse_pos;
    }

    // 2. Render Marquee Box
    if (m_is_box_selecting && io.MouseDown[ImGuiMouseButton_Left]) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p1 = m_box_select_start;
        ImVec2 p2 = mouse_pos;

        ImVec2 min_p(std::min(p1.x, p2.x), std::min(p1.y, p2.y));
        ImVec2 max_p(std::max(p1.x, p2.x), std::max(p1.y, p2.y));

        dl->AddRectFilled(min_p, max_p, IM_COL32(99, 102, 241, 40), 2.0f);
        dl->AddRect(min_p, max_p, IM_COL32(129, 140, 248, 220), 2.0f, 0, 1.5f);
    }

    // 3. Selection Commit on Mouse Release
    if (m_is_box_selecting && !io.MouseDown[ImGuiMouseButton_Left]) {
        m_is_box_selecting = false;

        float drag_dist = std::hypot(mouse_pos.x - m_box_select_start.x, mouse_pos.y - m_box_select_start.y);

        if (drag_dist > 6.0f) {
            // Marquee Box Selection
            glm::vec2 vp_p1((m_box_select_start.x - img_min.x) / scale_x, (m_box_select_start.y - img_min.y) / scale_y);
            glm::vec2 vp_p2((mouse_pos.x - img_min.x) / scale_x, (mouse_pos.y - img_min.y) / scale_y);

            glm::vec2 w_p1 = camera.screen_to_world(vp_p1);
            glm::vec2 w_p2 = camera.screen_to_world(vp_p2);

            float w_min_x = std::min(w_p1.x, w_p2.x);
            float w_max_x = std::max(w_p1.x, w_p2.x);
            float w_min_y = std::min(w_p1.y, w_p2.y);
            float w_max_y = std::max(w_p1.y, w_p2.y);

            std::unordered_set<EntityID> found_in_box;

            auto process_box_entity = [&](EntityID id, const TransformComponent& tx, glm::vec2 half) {
                if (!tx.world_visible) return;
                glm::vec2 pos = glm::vec2(tx.position.x, tx.position.y);

                EntityID target_id = id;
                if (em.has_component<HierarchyComponent>(id)) {
                    EntityID p_id = em.get_component<HierarchyComponent>(id).parent;
                    if (p_id != 0 && em.is_alive(p_id) && em.has_component<TilemapComponent>(p_id)) {
                        target_id = p_id;
                    }
                }

                if (is_alt_strict) {
                    if (pos.x - half.x >= w_min_x && pos.x + half.x <= w_max_x &&
                        pos.y - half.y >= w_min_y && pos.y + half.y <= w_max_y) {
                        found_in_box.insert(target_id);
                    }
                } else {
                    if (pos.x + half.x >= w_min_x && pos.x - half.x <= w_max_x &&
                        pos.y + half.y >= w_min_y && pos.y - half.y <= w_max_y) {
                        found_in_box.insert(target_id);
                    }
                }
            };

            // A. Sprites
            em.for_each<TransformComponent, SpriteComponent>([&](EntityID id, const TransformComponent& tx, const SpriteComponent& sp) {
                if (!sp.visible) return;
                process_box_entity(id, tx, sp.size * tx.world_scale * 0.5f);
            });

            // B. Tilemaps
            em.for_each<TransformComponent, TilemapComponent>([&](EntityID id, const TransformComponent& tx, const TilemapComponent& tm) {
                if (tm.empty()) {
                    process_box_entity(id, tx, glm::vec2(tm.tile_size * tx.world_scale.x * 0.5f, tm.tile_size * tx.world_scale.y * 0.5f));
                } else {
                    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
                    tm.get_bounds(min_x, min_y, max_x, max_y);
                    float w_world = static_cast<float>(max_x - min_x + 1) * tm.tile_size * tx.world_scale.x;
                    float h_world = static_cast<float>(max_y - min_y + 1) * tm.tile_size * tx.world_scale.y;
                    process_box_entity(id, tx, glm::vec2(w_world * 0.5f, h_world * 0.5f));
                }
            });

            // C. Colliders
            em.for_each<TransformComponent, ColliderComponent>([&](EntityID id, const TransformComponent& tx, const ColliderComponent& col) {
                if (em.has_component<SpriteComponent>(id) || em.has_component<TilemapComponent>(id)) return;
                for (const auto& shape : col.shapes) {
                    process_box_entity(id, tx, shape.size * tx.world_scale * 0.5f);
                }
            });

            if (is_additive) {
                selection.add_selection(found_in_box);
            } else {
                selection.set_selection(found_in_box);
            }
        } else {
            // Single point raycast picking
            glm::vec2 mouse_in_viewport((mouse_pos.x - img_min.x) / scale_x, (mouse_pos.y - img_min.y) / scale_y);
            glm::vec2 mouse_world = camera.screen_to_world(mouse_in_viewport);

            EntityID hit_entity = 0;
            float closest_dist = 999999.0f;

            auto test_pick_entity = [&](EntityID id, const TransformComponent& tx, glm::vec2 half) {
                if (!tx.world_visible) return;
                glm::vec2 pos = glm::vec2(tx.position.x, tx.position.y);

                if (mouse_world.x >= pos.x - half.x && mouse_world.x <= pos.x + half.x &&
                    mouse_world.y >= pos.y - half.y && mouse_world.y <= pos.y + half.y) {
                    float dist = glm::distance(mouse_world, pos);
                    if (dist < closest_dist) {
                        closest_dist = dist;
                        hit_entity = id;
                    }
                }
            };

            // A. Sprites
            em.for_each<TransformComponent, SpriteComponent>([&](EntityID id, const TransformComponent& tx, const SpriteComponent& sp) {
                if (!sp.visible) return;
                test_pick_entity(id, tx, sp.size * tx.world_scale * 0.5f);
            });

            // B. Tilemaps
            em.for_each<TransformComponent, TilemapComponent>([&](EntityID id, const TransformComponent& tx, const TilemapComponent& tm) {
                if (tm.empty()) {
                    test_pick_entity(id, tx, glm::vec2(tm.tile_size * tx.world_scale.x * 0.5f, tm.tile_size * tx.world_scale.y * 0.5f));
                } else {
                    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
                    tm.get_bounds(min_x, min_y, max_x, max_y);
                    float w_world = static_cast<float>(max_x - min_x + 1) * tm.tile_size * tx.world_scale.x;
                    float h_world = static_cast<float>(max_y - min_y + 1) * tm.tile_size * tx.world_scale.y;
                    test_pick_entity(id, tx, glm::vec2(w_world * 0.5f, h_world * 0.5f));
                }
            });

            // C. Colliders
            em.for_each<TransformComponent, ColliderComponent>([&](EntityID id, const TransformComponent& tx, const ColliderComponent& col) {
                if (em.has_component<SpriteComponent>(id) || em.has_component<TilemapComponent>(id)) return;
                for (const auto& shape : col.shapes) {
                    test_pick_entity(id, tx, shape.size * tx.world_scale * 0.5f);
                }
            });

            // If a child tilemap collider was hit, resolve selection to the parent Tilemap
            EntityID resolved_hit = hit_entity;
            if (resolved_hit != 0 && em.has_component<HierarchyComponent>(resolved_hit)) {
                EntityID parent_id = em.get_component<HierarchyComponent>(resolved_hit).parent;
                if (parent_id != 0 && em.is_alive(parent_id) && em.has_component<TilemapComponent>(parent_id)) {
                    resolved_hit = parent_id;
                }
            }

            selection.select(resolved_hit, is_additive);
        }
    }

    // 4. Deselect with Escape key
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        selection.clear();
    }
}

} // namespace Engine
