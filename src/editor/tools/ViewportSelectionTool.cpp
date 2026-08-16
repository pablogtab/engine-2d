#include "ViewportSelectionTool.hpp"
#include "../../ecs/Components.hpp"
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
        glm::vec2 spr_size = glm::vec2(64.0f, 64.0f);
        if (em.has_component<SpriteComponent>(id)) {
            spr_size = em.get_component<SpriteComponent>(id).size;
        }
        glm::vec2 half = spr_size * tx.world_scale * 0.5f;

        glm::vec3 w_tl(tx.position.x - half.x, tx.position.y + half.y, 0.0f);
        glm::vec3 w_br(tx.position.x + half.x, tx.position.y - half.y, 0.0f);

        glm::vec2 s_tl = camera.world_to_screen(w_tl);
        glm::vec2 s_br = camera.world_to_screen(w_br);

        ImVec2 p_min(img_min.x + s_tl.x * scale_x, img_min.y + s_tl.y * scale_y);
        ImVec2 p_max(img_min.x + s_br.x * scale_x, img_min.y + s_br.y * scale_y);

        // Subtle, minimalist indigo outline matching the selection theme
        dl->AddRectFilled(p_min, p_max, IM_COL32(99, 102, 241, 30), 2.0f);
        dl->AddRect(p_min, p_max, IM_COL32(129, 140, 248, 220), 2.0f, 0, 1.2f);
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

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos = io.MousePos;

    // 1. Start Marquee Box Selection
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !is_gizmo_active) {
        m_is_box_selecting = true;
        m_box_select_start = mouse_pos;
    }

    // 2. Render Active Box Selection Rectangle
    if (m_is_box_selecting && io.MouseDown[ImGuiMouseButton_Left]) {
        ImVec2 b_min(std::min(m_box_select_start.x, mouse_pos.x), std::min(m_box_select_start.y, mouse_pos.y));
        ImVec2 b_max(std::max(m_box_select_start.x, mouse_pos.x), std::max(m_box_select_start.y, mouse_pos.y));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(b_min, b_max, IM_COL32(99, 102, 241, 24), 2.0f);
        dl->AddRect(b_min, b_max, IM_COL32(129, 140, 248, 150), 2.0f, 0, 1.0f);
    }

    // 3. Finalize Box Selection or Single Click
    if (m_is_box_selecting && !io.MouseDown[ImGuiMouseButton_Left]) {
        m_is_box_selecting = false;
        float drag_dist = std::hypot(mouse_pos.x - m_box_select_start.x, mouse_pos.y - m_box_select_start.y);
        bool is_additive = io.KeyShift || io.KeyCtrl || io.KeySuper; // Shift or Cmd/Ctrl = Additive multi-selection
        bool is_alt_strict = io.KeyAlt;                             // Alt/Option = Strict Inclusion (100% contained)

        float scale_x = img_size.x / camera.get_viewport_size().x;
        float scale_y = img_size.y / camera.get_viewport_size().y;
        EntityManager& em = active_scene->get_entity_manager();

        if (drag_dist > 6.0f) {
            // AABB Multi-entity selection
            ImVec2 b_min(std::min(m_box_select_start.x, mouse_pos.x), std::min(m_box_select_start.y, mouse_pos.y));
            ImVec2 b_max(std::max(m_box_select_start.x, mouse_pos.x), std::max(m_box_select_start.y, mouse_pos.y));

            glm::vec2 w_p1 = camera.screen_to_world(glm::vec2((b_min.x - img_min.x) / scale_x, (b_min.y - img_min.y) / scale_y));
            glm::vec2 w_p2 = camera.screen_to_world(glm::vec2((b_max.x - img_min.x) / scale_x, (b_max.y - img_min.y) / scale_y));

            float w_min_x = std::min(w_p1.x, w_p2.x);
            float w_max_x = std::max(w_p1.x, w_p2.x);
            float w_min_y = std::min(w_p1.y, w_p2.y);
            float w_max_y = std::max(w_p1.y, w_p2.y);

            std::unordered_set<EntityID> found_in_box;
            em.for_each<TransformComponent, SpriteComponent>([&](EntityID id, const TransformComponent& tx, const SpriteComponent& sp) {
                if (!tx.world_visible || !sp.visible) return;
                glm::vec2 half = sp.size * tx.world_scale * 0.5f;
                glm::vec2 pos = glm::vec2(tx.position.x, tx.position.y);

                if (is_alt_strict) {
                    // Strict Inclusion (100% inside box)
                    if (pos.x - half.x >= w_min_x && pos.x + half.x <= w_max_x &&
                        pos.y - half.y >= w_min_y && pos.y + half.y <= w_max_y) {
                        found_in_box.insert(id);
                    }
                } else {
                    // Crossing / Overlapping
                    if (pos.x + half.x >= w_min_x && pos.x - half.x <= w_max_x &&
                        pos.y + half.y >= w_min_y && pos.y - half.y <= w_max_y) {
                        found_in_box.insert(id);
                    }
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

            selection.select(hit_entity, is_additive);
        }
    }

    // 4. Deselect with Escape key
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        selection.clear();
    }
}

} // namespace Engine
