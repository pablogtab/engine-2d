#pragma once

#include "../ecs/EcsTypes.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

class EntityManager;
class Renderer2D;
struct TrailRenderer2DComponent;

class TrailSystem2D {
public:
    void update(EntityManager& entities, float delta_time);
    void render(Renderer2D& renderer);
    void clear();

    [[nodiscard]] std::size_t active_trail_count() const { return m_states.size(); }
    [[nodiscard]] std::size_t total_point_count() const;
    [[nodiscard]] std::size_t point_count(EntityID entity) const;

private:
    struct Point {
        glm::vec2 position{0.0f};
        float age{0.0f};
    };

    struct State {
        std::vector<Point> points;
        glm::vec2 last_sample_position{0.0f};
        bool has_sample{false};
        bool head_is_sample{true};
        std::uint64_t source_generation{0};
        float elapsed_time{0.0f};

        // A copy lets the ribbon finish fading after its source entity dies.
        struct Style;
        std::shared_ptr<Style> style;
    };

    std::unordered_map<EntityID, State> m_states;
    std::vector<glm::vec2> m_render_positions;
    std::vector<float> m_render_ages;
    std::vector<glm::vec2> m_directions;
    std::vector<float> m_lengths;
    std::vector<glm::vec2> m_offsets;
    std::vector<float> m_u_coordinates;
    std::vector<SDL_Color> m_colors;
};

} // namespace Engine
