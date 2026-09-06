#include "TilemapPaintingTool.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include <algorithm>
#include <cmath>

namespace Engine {

void TilemapPaintingTool::reset_active_stroke() {
    m_is_box_filling = false;
    m_was_painting = false;
    m_has_last_grid_cell = false;
    m_stroke_scene = nullptr;
    m_stroke_entity = 0;
    m_stroke_tool = TileToolMode::Brush;
    m_stroke_old_tiles.clear();
    m_stroke_new_tiles.clear();
}

void TilemapPaintingTool::cancel_active_stroke(Scene* active_scene) {
    if (!has_active_stroke()) return;

    if (active_scene && active_scene == m_stroke_scene && m_stroke_entity != 0) {
        EntityManager& em = active_scene->get_entity_manager();
        if (em.is_alive(m_stroke_entity) && em.has_component<TilemapComponent>(m_stroke_entity)) {
            auto& tilemap = em.get_component<TilemapComponent>(m_stroke_entity);
            for (const auto& [key, old_id] : m_stroke_old_tiles) {
                int x = 0;
                int y = 0;
                TilemapComponent::parse_key(key, x, y);
                tilemap.set_tile(x, y, old_id);
            }
            if (!m_stroke_old_tiles.empty()) {
                TilemapColliderSystem::build_composite_colliders(em, m_stroke_entity);
            }
        }
    }

    reset_active_stroke();
}

void TilemapPaintingTool::handle_viewport_painting(
    Scene* active_scene,
    EntityID active_tilemap_entity,
    const TilemapPalettePanel& palette_panel,
    const Camera2D& camera,
    const ImVec2& img_min,
    const ImVec2& img_size,
    bool is_viewport_hovered,
    EditorCommandHistory* command_history
) {
    if (!active_scene) {
        reset_active_stroke();
        return;
    }

    if (has_active_stroke() && active_scene != m_stroke_scene) {
        // The old scene is no longer an addressable editing target. Forget the
        // transient gesture instead of ever applying it to the new scene.
        reset_active_stroke();
        return;
    }

    EntityManager& em = active_scene->get_entity_manager();
    const EntityID target_entity = has_active_stroke()
        ? m_stroke_entity
        : active_tilemap_entity;
    if (target_entity == 0 || !em.is_alive(target_entity) ||
        !em.has_component<TilemapComponent>(target_entity) ||
        !em.has_component<TransformComponent>(target_entity)) {
        cancel_active_stroke(active_scene);
        return;
    }

    auto& tilemap = em.get_component<TilemapComponent>(target_entity);
    const auto& tx = em.get_component<TransformComponent>(target_entity);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos = io.MousePos;

    float scale_x = img_size.x / camera.get_viewport_size().x;
    float scale_y = img_size.y / camera.get_viewport_size().y;

    // Convert world space to sparse grid coordinates centered at the tilemap's
    // effective world origin. This keeps parented tilemaps aligned with render.
    float tile_sz = tilemap.tile_size * tx.world_scale.x;
    if (!std::isfinite(tile_sz) || tile_sz <= 0.0f) {
        cancel_active_stroke(active_scene);
        return;
    }

    glm::ivec2 grid_cell = m_last_grid_cell;
    bool has_grid_cell = m_has_last_grid_cell;
    if (is_viewport_hovered) {
        glm::vec2 mouse_in_vp(
            (mouse_pos.x - img_min.x) / scale_x,
            (mouse_pos.y - img_min.y) / scale_y
        );
        const glm::vec2 mouse_world = camera.screen_to_world(mouse_in_vp);
        grid_cell.x = static_cast<int>(std::floor(
            (mouse_world.x - tx.world_position.x) / tile_sz
        ));
        grid_cell.y = static_cast<int>(std::floor(
            (tx.world_position.y - mouse_world.y) / tile_sz
        ));
        has_grid_cell = true;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const TileToolMode requested_tool = palette_panel.get_tool_mode();
    const uint16_t active_tile = palette_panel.get_active_tile_id();
    const TileToolMode effective_tool = has_active_stroke()
        ? m_stroke_tool
        : requested_tool;

    // --- 1. Render Ghost Preview & Interactive Drag Overlays ---
    if (has_grid_cell && m_is_box_filling && effective_tool == TileToolMode::BoxFill) {
        int fill_min_x = std::min(m_box_fill_start.x, grid_cell.x);
        int fill_max_x = std::max(m_box_fill_start.x, grid_cell.x);
        int fill_min_y = std::min(m_box_fill_start.y, grid_cell.y);
        int fill_max_y = std::max(m_box_fill_start.y, grid_cell.y);

        float box_start_x = tx.world_position.x + (static_cast<float>(fill_min_x) * tile_sz);
        float box_start_y = tx.world_position.y - (static_cast<float>(fill_min_y) * tile_sz);
        float box_end_x   = tx.world_position.x + (static_cast<float>(fill_max_x + 1) * tile_sz);
        float box_end_y   = tx.world_position.y - (static_cast<float>(fill_max_y + 1) * tile_sz);

        glm::vec2 box_tl = camera.world_to_screen(glm::vec3(box_start_x, box_start_y, 0.0f));
        glm::vec2 box_br = camera.world_to_screen(glm::vec3(box_end_x, box_end_y, 0.0f));

        ImVec2 b_min(img_min.x + box_tl.x * scale_x, img_min.y + box_tl.y * scale_y);
        ImVec2 b_max(img_min.x + box_br.x * scale_x, img_min.y + box_br.y * scale_y);

        ImVec2 rect_min(std::min(b_min.x, b_max.x), std::min(b_min.y, b_max.y));
        ImVec2 rect_max(std::max(b_min.x, b_max.x), std::max(b_min.y, b_max.y));

        // Semi-transparent box fill area with modern indigo accent
        dl->AddRectFilled(rect_min, rect_max, IM_COL32(99, 102, 241, 65), 2.0f);
        dl->AddRect(rect_min, rect_max, IM_COL32(129, 140, 248, 230), 2.0f, 0, 2.0f);

        // Dimensions badge tooltip
        int count_w = fill_max_x - fill_min_x + 1;
        int count_h = fill_max_y - fill_min_y + 1;
        char badge_str[48];
        snprintf(badge_str, sizeof(badge_str), "%dx%d (%d tiles)", count_w, count_h, count_w * count_h);
        
        ImVec2 text_size = ImGui::CalcTextSize(badge_str);
        ImVec2 badge_bg_min(rect_max.x + 4, rect_max.y - text_size.y - 4);
        ImVec2 badge_bg_max(rect_max.x + text_size.x + 12, rect_max.y + 2);
        dl->AddRectFilled(badge_bg_min, badge_bg_max, IM_COL32(20, 20, 30, 220), 3.0f);
        dl->AddText(ImVec2(badge_bg_min.x + 4, badge_bg_min.y + 1), IM_COL32(255, 255, 255, 255), badge_str);
    } else if (is_viewport_hovered && has_grid_cell) {
        // Single cell cursor preview
        float start_x = tx.world_position.x + (static_cast<float>(grid_cell.x) * tile_sz);
        float start_y = tx.world_position.y - (static_cast<float>(grid_cell.y) * tile_sz);

        glm::vec2 s_tl = camera.world_to_screen(glm::vec3(start_x, start_y, 0.0f));
        glm::vec2 s_br = camera.world_to_screen(glm::vec3(start_x + tile_sz, start_y - tile_sz, 0.0f));

        ImVec2 p_min(img_min.x + s_tl.x * scale_x, img_min.y + s_tl.y * scale_y);
        ImVec2 p_max(img_min.x + s_br.x * scale_x, img_min.y + s_br.y * scale_y);

        ImU32 ghost_color = (effective_tool == TileToolMode::Eraser)
            ? IM_COL32(239, 68, 68, 80) 
            : (effective_tool == TileToolMode::RandomBrush
                ? IM_COL32(245, 158, 11, 85)
                : (effective_tool == TileToolMode::BoxFill ? IM_COL32(139, 92, 246, 80) : IM_COL32(59, 130, 246, 80)));

        dl->AddRectFilled(p_min, p_max, ghost_color, 1.0f);
        dl->AddRect(p_min, p_max, IM_COL32(255, 255, 255, 180), 1.0f, 0, 1.0f);

        if (effective_tool == TileToolMode::RandomBrush) {
            dl->AddText(ImVec2(p_min.x + 2, p_min.y + 2), IM_COL32(255, 255, 255, 220), "🎲");
        }
    }

    // --- 2. Painting Action Execution ---
    const bool is_painting = io.MouseDown[ImGuiMouseButton_Left] &&
        !io.KeyCtrl && !io.KeySuper && !io.KeyAlt;

    const bool can_begin_stroke = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (is_painting && is_viewport_hovered && has_grid_cell &&
        (has_active_stroke() || can_begin_stroke)) {
        if (!has_active_stroke()) {
            m_stroke_scene = active_scene;
            m_stroke_entity = target_entity;
            m_stroke_tool = requested_tool;
            m_box_fill_start = grid_cell;
            m_last_grid_cell = grid_cell;
            m_has_last_grid_cell = true;
            if (requested_tool == TileToolMode::BoxFill) {
                m_is_box_filling = true;
            } else {
                m_was_painting = true;
            }
        }

        m_last_grid_cell = grid_cell;
        m_has_last_grid_cell = true;
        if (m_stroke_tool == TileToolMode::Brush ||
            m_stroke_tool == TileToolMode::RandomBrush ||
            m_stroke_tool == TileToolMode::Eraser) {
            const int64_t key = TilemapComponent::make_key(grid_cell.x, grid_cell.y);
            if (!m_stroke_old_tiles.contains(key)) {
                m_stroke_old_tiles[key] = tilemap.get_tile(grid_cell.x, grid_cell.y);
            }
            const uint16_t target_id = (m_stroke_tool == TileToolMode::Eraser)
                ? 0 
                : (m_stroke_tool == TileToolMode::RandomBrush
                    ? palette_panel.get_random_tile_id()
                    : active_tile);
            tilemap.set_tile(grid_cell.x, grid_cell.y, target_id);
            m_stroke_new_tiles[key] = target_id;
        }
    }

    // Finalize Box Fill on mouse release
    if (m_is_box_filling && !io.MouseDown[ImGuiMouseButton_Left]) {
        const int min_x = std::min(m_box_fill_start.x, m_last_grid_cell.x);
        const int max_x = std::max(m_box_fill_start.x, m_last_grid_cell.x);
        const int min_y = std::min(m_box_fill_start.y, m_last_grid_cell.y);
        const int max_y = std::max(m_box_fill_start.y, m_last_grid_cell.y);

        const bool use_random = palette_panel.use_random_box_fill() &&
            !palette_panel.get_random_pool().empty();

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                int64_t key = TilemapComponent::make_key(x, y);
                if (!m_stroke_old_tiles.contains(key)) {
                    m_stroke_old_tiles[key] = tilemap.get_tile(x, y);
                }
                uint16_t target_id = use_random ? palette_panel.get_random_tile_id() : active_tile;
                tilemap.set_tile(x, y, target_id);
                m_stroke_new_tiles[key] = target_id;
            }
        }

        bool any_changed = false;
        for (const auto& [key, old_id] : m_stroke_old_tiles) {
            if (m_stroke_new_tiles[key] != old_id) {
                any_changed = true;
                break;
            }
        }

        // Rebuild composite colliders for physics only when authored data changed.
        if (any_changed) {
            TilemapColliderSystem::build_composite_colliders(em, m_stroke_entity);
        }

        // Push Undo/Redo command
        if (command_history && any_changed) {
            std::string desc = "Box Fill (" + std::to_string(m_stroke_old_tiles.size()) + " tiles)";
            command_history->push_executed_command(
                std::make_unique<PaintTilesCommand>(&em, m_stroke_entity, std::move(m_stroke_old_tiles), std::move(m_stroke_new_tiles), desc)
            );
        }
        reset_active_stroke();
    }

    // Rebuild composite colliders on mouse release after brush/eraser painting
    if (m_was_painting && !io.MouseDown[ImGuiMouseButton_Left] && !m_is_box_filling) {
        TilemapColliderSystem::build_composite_colliders(em, m_stroke_entity);

        // Push Undo/Redo command
        if (command_history && !m_stroke_old_tiles.empty()) {
            bool any_changed = false;
            for (const auto& [k, old_id] : m_stroke_old_tiles) {
                if (m_stroke_new_tiles[k] != old_id) {
                    any_changed = true;
                    break;
                }
            }

            if (any_changed) {
                std::string desc = (m_stroke_tool == TileToolMode::Eraser)
                    ? "Erase (" + std::to_string(m_stroke_old_tiles.size()) + " tiles)"
                    : "Paint (" + std::to_string(m_stroke_old_tiles.size()) + " tiles)";
                command_history->push_executed_command(
                    std::make_unique<PaintTilesCommand>(&em, m_stroke_entity, std::move(m_stroke_old_tiles), std::move(m_stroke_new_tiles), desc)
                );
            }
        }
        reset_active_stroke();
    }
}

} // namespace Engine
