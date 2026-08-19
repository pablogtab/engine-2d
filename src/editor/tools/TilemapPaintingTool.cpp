#include "TilemapPaintingTool.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include <algorithm>
#include <cmath>

namespace Engine {

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
    if (!active_scene || active_tilemap_entity == 0 || !is_viewport_hovered) return;

    EntityManager& em = active_scene->get_entity_manager();
    if (!em.is_alive(active_tilemap_entity) || !em.has_component<TilemapComponent>(active_tilemap_entity) || !em.has_component<TransformComponent>(active_tilemap_entity)) {
        return;
    }

    auto& tilemap = em.get_component<TilemapComponent>(active_tilemap_entity);
    const auto& tx = em.get_component<TransformComponent>(active_tilemap_entity);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos = io.MousePos;

    float scale_x = img_size.x / camera.get_viewport_size().x;
    float scale_y = img_size.y / camera.get_viewport_size().y;

    glm::vec2 mouse_in_vp((mouse_pos.x - img_min.x) / scale_x, (mouse_pos.y - img_min.y) / scale_y);
    glm::vec2 mouse_world = camera.screen_to_world(mouse_in_vp);

    // Convert world space to sparse grid coordinates centered at tx.position
    float tile_sz = tilemap.tile_size * tx.world_scale.x;
    int grid_x = static_cast<int>(std::floor((mouse_world.x - tx.position.x) / tile_sz));
    int grid_y = static_cast<int>(std::floor((tx.position.y - mouse_world.y) / tile_sz));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    TileToolMode tool = palette_panel.get_tool_mode();
    uint16_t active_tile = palette_panel.get_active_tile_id();

    // --- 1. Render Ghost Preview & Interactive Drag Overlays ---
    if (m_is_box_filling && tool == TileToolMode::BoxFill) {
        int fill_min_x = std::min(m_box_fill_start.x, grid_x);
        int fill_max_x = std::max(m_box_fill_start.x, grid_x);
        int fill_min_y = std::min(m_box_fill_start.y, grid_y);
        int fill_max_y = std::max(m_box_fill_start.y, grid_y);

        float box_start_x = tx.position.x + (static_cast<float>(fill_min_x) * tile_sz);
        float box_start_y = tx.position.y - (static_cast<float>(fill_min_y) * tile_sz);
        float box_end_x   = tx.position.x + (static_cast<float>(fill_max_x + 1) * tile_sz);
        float box_end_y   = tx.position.y - (static_cast<float>(fill_max_y + 1) * tile_sz);

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
    } else {
        // Single cell cursor preview
        float start_x = tx.position.x + (static_cast<float>(grid_x) * tile_sz);
        float start_y = tx.position.y - (static_cast<float>(grid_y) * tile_sz);

        glm::vec2 s_tl = camera.world_to_screen(glm::vec3(start_x, start_y, 0.0f));
        glm::vec2 s_br = camera.world_to_screen(glm::vec3(start_x + tile_sz, start_y - tile_sz, 0.0f));

        ImVec2 p_min(img_min.x + s_tl.x * scale_x, img_min.y + s_tl.y * scale_y);
        ImVec2 p_max(img_min.x + s_br.x * scale_x, img_min.y + s_br.y * scale_y);

        ImU32 ghost_color = (tool == TileToolMode::Eraser) 
            ? IM_COL32(239, 68, 68, 80) 
            : (tool == TileToolMode::RandomBrush 
                ? IM_COL32(245, 158, 11, 85)
                : (tool == TileToolMode::BoxFill ? IM_COL32(139, 92, 246, 80) : IM_COL32(59, 130, 246, 80)));

        dl->AddRectFilled(p_min, p_max, ghost_color, 1.0f);
        dl->AddRect(p_min, p_max, IM_COL32(255, 255, 255, 180), 1.0f, 0, 1.0f);

        if (tool == TileToolMode::RandomBrush) {
            dl->AddText(ImVec2(p_min.x + 2, p_min.y + 2), IM_COL32(255, 255, 255, 220), "🎲");
        }
    }

    // --- 2. Painting Action Execution ---
    bool is_painting = io.MouseDown[ImGuiMouseButton_Left] && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt;

    if (is_painting) {
        m_was_painting = true;
        if (tool == TileToolMode::Brush || tool == TileToolMode::RandomBrush || tool == TileToolMode::Eraser) {
            int64_t key = TilemapComponent::make_key(grid_x, grid_y);
            if (!m_stroke_old_tiles.contains(key)) {
                m_stroke_old_tiles[key] = tilemap.get_tile(grid_x, grid_y);
            }
            uint16_t target_id = (tool == TileToolMode::Eraser) 
                ? 0 
                : (tool == TileToolMode::RandomBrush ? palette_panel.get_random_tile_id() : active_tile);
            tilemap.set_tile(grid_x, grid_y, target_id);
            m_stroke_new_tiles[key] = target_id;
        } else if (tool == TileToolMode::BoxFill) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_is_box_filling = true;
                m_box_fill_start = glm::ivec2(grid_x, grid_y);
            }
        }
    }

    // Finalize Box Fill on mouse release
    if (m_is_box_filling && !io.MouseDown[ImGuiMouseButton_Left]) {
        m_is_box_filling = false;
        int min_x = std::min(m_box_fill_start.x, grid_x);
        int max_x = std::max(m_box_fill_start.x, grid_x);
        int min_y = std::min(m_box_fill_start.y, grid_y);
        int max_y = std::max(m_box_fill_start.y, grid_y);

        bool use_random = !palette_panel.get_random_pool().empty();

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

        // Rebuild composite colliders for physics
        TilemapColliderSystem::build_composite_colliders(em, active_tilemap_entity);

        // Push Undo/Redo command
        if (command_history && !m_stroke_old_tiles.empty()) {
            std::string desc = "Box Fill (" + std::to_string(m_stroke_old_tiles.size()) + " tiles)";
            command_history->push_executed_command(
                std::make_unique<PaintTilesCommand>(&em, active_tilemap_entity, std::move(m_stroke_old_tiles), std::move(m_stroke_new_tiles), desc)
            );
        }
        m_stroke_old_tiles.clear();
        m_stroke_new_tiles.clear();
    }

    // Rebuild composite colliders on mouse release after brush/eraser painting
    if (m_was_painting && !io.MouseDown[ImGuiMouseButton_Left] && !m_is_box_filling) {
        m_was_painting = false;
        TilemapColliderSystem::build_composite_colliders(em, active_tilemap_entity);

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
                std::string desc = (tool == TileToolMode::Eraser)
                    ? "Erase (" + std::to_string(m_stroke_old_tiles.size()) + " tiles)"
                    : "Paint (" + std::to_string(m_stroke_old_tiles.size()) + " tiles)";
                command_history->push_executed_command(
                    std::make_unique<PaintTilesCommand>(&em, active_tilemap_entity, std::move(m_stroke_old_tiles), std::move(m_stroke_new_tiles), desc)
                );
            }
        }
        m_stroke_old_tiles.clear();
        m_stroke_new_tiles.clear();
    }
}

} // namespace Engine
