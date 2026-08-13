#pragma once

#include "../ecs/Components.hpp"
#include <unordered_map>
#include <vector>
#include <cmath>
#include <SDL3/SDL.h>

namespace Engine {

class SpatialGrid {
public:
    explicit SpatialGrid(float cell_size = 256.0f)
        : m_cell_size(cell_size) {}

    // Clear all spatial cell buckets
    void clear() {
        m_cells.clear();
    }

    // Insert an entity into spatial grid cell buckets based on its 2.5D world bounding box
    void insert(EntityID id, const glm::vec3& world_pos, const glm::vec2& size) {
        int min_x = static_cast<int>(std::floor((world_pos.x - size.x * 0.5f) / m_cell_size));
        int max_x = static_cast<int>(std::floor((world_pos.x + size.x * 0.5f) / m_cell_size));
        int min_y = static_cast<int>(std::floor((world_pos.y - size.y * 0.5f) / m_cell_size));
        int max_y = static_cast<int>(std::floor((world_pos.y + size.y * 0.5f) / m_cell_size));

        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                int64_t cell_key = get_cell_key(x, y);
                m_cells[cell_key].push_back(id);
            }
        }
    }

    // Query entity candidate IDs within target 2.5D bounding box (e.g. camera frustum)
    void query_bounds(const SDL_FRect& bounds, std::vector<EntityID>& out_entities) const {
        int min_x = static_cast<int>(std::floor(bounds.x / m_cell_size));
        int max_x = static_cast<int>(std::floor((bounds.x + bounds.w) / m_cell_size));
        int min_y = static_cast<int>(std::floor(bounds.y / m_cell_size));
        int max_y = static_cast<int>(std::floor((bounds.y + bounds.h) / m_cell_size));

        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                int64_t cell_key = get_cell_key(x, y);
                auto it = m_cells.find(cell_key);
                if (it != m_cells.end()) {
                    out_entities.insert(out_entities.end(), it->second.begin(), it->second.end());
                }
            }
        }
    }

private:
    int64_t get_cell_key(int x, int y) const {
        return (static_cast<int64_t>(x) << 32) | (static_cast<int64_t>(y) & 0xFFFFFFFF);
    }

    float m_cell_size{256.0f};
    std::unordered_map<int64_t, std::vector<EntityID>> m_cells;
};

} // namespace Engine
