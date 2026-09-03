#include "TrailSystem2D.hpp"
#include "Renderer2D.hpp"
#include "Texture.hpp"
#include "../core/AssetManager.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace Engine {

struct TrailSystem2D::State::Style {
    std::shared_ptr<const Material> material;
    SDL_Color head_color{255, 255, 255, 255};
    SDL_Color tail_color{255, 255, 255, 0};
    float lifetime{0.25f};
    float min_vertex_distance{3.0f};
    float head_width{10.0f};
    float tail_width{0.0f};
    float texture_scale{1.0f};
    float flow_speed{0.0f};
    std::uint8_t smoothing{2};
    std::uint16_t max_points{64};
    std::int16_t sorting_layer{0};
    std::int16_t order_in_layer{-1};
    float z_layer{-0.08f};
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};
    TrailBlendMode blend_mode{TrailBlendMode::Additive};
};

namespace {

constexpr float epsilon = 0.0001f;

SDL_Color lerp_color(SDL_Color from, SDL_Color to, float amount) {
    const auto channel = [amount](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lerp(static_cast<float>(a), static_cast<float>(b), amount),
            0.0f, 255.0f
        ));
    };
    return {channel(from.r, to.r), channel(from.g, to.g),
            channel(from.b, to.b), channel(from.a, to.a)};
}

glm::vec2 perpendicular(glm::vec2 direction) {
    return {-direction.y, direction.x};
}

glm::vec2 catmull_rom(
    const glm::vec2& p0,
    const glm::vec2& p1,
    const glm::vec2& p2,
    const glm::vec2& p3,
    float amount
) {
    const float amount_squared = amount * amount;
    const float amount_cubed = amount_squared * amount;
    return 0.5f * (
        2.0f * p1 +
        (-p0 + p2) * amount +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * amount_squared +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * amount_cubed
    );
}

} // namespace

void TrailSystem2D::update(EntityManager& entities, float delta_time) {
    delta_time = std::max(delta_time, 0.0f);

    for (auto& [entity, state] : m_states) {
        static_cast<void>(entity);
        if (!state.style) continue;
        state.elapsed_time += delta_time;
        for (Point& point : state.points) point.age += delta_time;
        std::erase_if(state.points, [&](const Point& point) {
            return point.age >= state.style->lifetime;
        });
        if (state.points.empty()) state.has_sample = false;
    }

    entities.for_each<TransformComponent, TrailRenderer2DComponent>([&](
        EntityID entity,
        const TransformComponent& transform,
        TrailRenderer2DComponent& component
    ) {
        if (!component.enabled || !transform.world_visible) {
            m_states.erase(entity);
            return;
        }

        State& state = m_states[entity];
        const std::uint64_t generation = entities.entity_generation(entity);
        if (state.source_generation != 0 && state.source_generation != generation) {
            // Numeric entity IDs are recycled. Never connect the fading trail
            // from an old lifetime to a newly spawned entity with the same ID.
            state = State{};
        }
        state.source_generation = generation;
        if (!state.style) state.style = std::make_shared<State::Style>();
        *state.style = {
            component.material,
            component.head_color,
            component.tail_color,
            component.lifetime,
            component.min_vertex_distance,
            component.head_width,
            component.tail_width,
            component.texture_scale,
            component.flow_speed,
            component.smoothing,
            component.max_points,
            component.sorting_layer,
            component.order_in_layer,
            component.z_layer,
            component.render_layer,
            component.blend_mode
        };
        if (!component.emitting) return;

        const glm::vec2 position{transform.world_position.x, transform.world_position.y};
        if (!state.has_sample || state.points.empty()) {
            state.points.push_back({position, 0.0f});
            state.last_sample_position = position;
            state.has_sample = true;
            state.head_is_sample = true;
            return;
        }

        const float distance_from_sample = glm::length(position - state.last_sample_position);
        if (state.head_is_sample && glm::length(position - state.points.back().position) > epsilon) {
            state.points.push_back({position, 0.0f});
            state.head_is_sample = false;
        } else if (!state.head_is_sample) {
            state.points.back() = {position, 0.0f};
        } else {
            state.points.back().age = 0.0f;
        }

        if (distance_from_sample >= component.min_vertex_distance) {
            state.last_sample_position = position;
            state.head_is_sample = true;
        }

        const std::size_t limit = std::max<std::size_t>(component.max_points, 2);
        if (state.points.size() > limit) {
            state.points.erase(state.points.begin(), state.points.begin() +
                static_cast<std::ptrdiff_t>(state.points.size() - limit));
        }
    });

    std::erase_if(m_states, [](const auto& entry) {
        return !entry.second.style || entry.second.points.empty();
    });
}

void TrailSystem2D::render(Renderer2D& renderer) {
    Texture* white = renderer.get_sdl_renderer()
        ? AssetManager::get().get_white_texture(renderer.get_sdl_renderer()) : nullptr;
    if (!white) return;

    for (const auto& [entity, state] : m_states) {
        static_cast<void>(entity);
        if (!state.style || state.points.size() < 2) continue;
        const State::Style& style = *state.style;
        const std::size_t source_count = state.points.size();

        m_render_positions.clear();
        m_render_ages.clear();
        if (style.smoothing == 0 || source_count < 3) {
            m_render_positions.reserve(source_count);
            m_render_ages.reserve(source_count);
            for (const Point& point : state.points) {
                m_render_positions.push_back(point.position);
                m_render_ages.push_back(point.age);
            }
        } else {
            const std::size_t subdivisions =
                static_cast<std::size_t>(style.smoothing) + 1;
            m_render_positions.reserve((source_count - 1) * subdivisions + 1);
            m_render_ages.reserve((source_count - 1) * subdivisions + 1);
            m_render_positions.push_back(state.points.front().position);
            m_render_ages.push_back(state.points.front().age);
            for (std::size_t index = 0; index + 1 < source_count; ++index) {
                const glm::vec2& p0 = state.points[index == 0 ? 0 : index - 1].position;
                const glm::vec2& p1 = state.points[index].position;
                const glm::vec2& p2 = state.points[index + 1].position;
                const glm::vec2& p3 = state.points[
                    std::min(index + 2, source_count - 1)
                ].position;
                for (std::size_t step = 1; step <= subdivisions; ++step) {
                    const float amount = static_cast<float>(step) /
                        static_cast<float>(subdivisions);
                    m_render_positions.push_back(catmull_rom(
                        p0, p1, p2, p3, amount
                    ));
                    m_render_ages.push_back(std::lerp(
                        state.points[index].age,
                        state.points[index + 1].age,
                        amount
                    ));
                }
            }
        }
        const std::size_t count = m_render_positions.size();

        m_directions.resize(count - 1);
        m_lengths.assign(count - 1, 0.0f);
        float total_length = 0.0f;
        for (std::size_t index = 0; index + 1 < count; ++index) {
            const glm::vec2 delta =
                m_render_positions[index + 1] - m_render_positions[index];
            m_lengths[index] = glm::length(delta);
            m_directions[index] = m_lengths[index] > epsilon
                ? delta / m_lengths[index] : glm::vec2(1.0f, 0.0f);
            total_length += m_lengths[index];
        }
        if (total_length <= epsilon) continue;

        m_offsets.resize(count);
        m_u_coordinates.assign(count, 0.0f);
        m_colors.resize(count);
        float walked = 0.0f;
        for (std::size_t index = 0; index < count; ++index) {
            if (index > 0) walked += m_lengths[index - 1];
            m_u_coordinates[index] = walked / total_length;
            const float freshness = std::clamp(
                1.0f - m_render_ages[index] / style.lifetime, 0.0f, 1.0f
            );
            const float half_width = std::lerp(style.tail_width, style.head_width, freshness) * 0.5f;
            m_colors[index] = lerp_color(style.tail_color, style.head_color, freshness);

            if (index == 0) {
                m_offsets[index] = perpendicular(m_directions.front()) * half_width;
            } else if (index + 1 == count) {
                m_offsets[index] = perpendicular(m_directions.back()) * half_width;
            } else {
                const glm::vec2 previous_normal = perpendicular(m_directions[index - 1]);
                const glm::vec2 next_normal = perpendicular(m_directions[index]);
                const glm::vec2 sum = previous_normal + next_normal;
                if (glm::length(sum) <= epsilon) {
                    m_offsets[index] = next_normal * half_width;
                } else {
                    const glm::vec2 miter = glm::normalize(sum);
                    const float denominator = glm::dot(miter, next_normal);
                    const float miter_length = std::abs(denominator) > 0.15f
                        ? std::min(half_width / std::abs(denominator), half_width * 2.0f)
                        : half_width;
                    m_offsets[index] = miter * miter_length;
                }
            }
        }

        const SDL_BlendMode blend = style.blend_mode == TrailBlendMode::Additive
            ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND;
        const float uv_offset = state.elapsed_time * style.flow_speed;
        for (std::size_t index = 0; index + 1 < count; ++index) {
            if (m_lengths[index] <= epsilon) continue;
            const std::array<glm::vec2, 4> corners{
                m_render_positions[index] + m_offsets[index],
                m_render_positions[index + 1] + m_offsets[index + 1],
                m_render_positions[index + 1] - m_offsets[index + 1],
                m_render_positions[index] - m_offsets[index]
            };
            const std::array<glm::vec2, 4> uvs{
                glm::vec2{m_u_coordinates[index] * style.texture_scale - uv_offset, 0.0f},
                glm::vec2{m_u_coordinates[index + 1] * style.texture_scale - uv_offset, 0.0f},
                glm::vec2{m_u_coordinates[index + 1] * style.texture_scale - uv_offset, 1.0f},
                glm::vec2{m_u_coordinates[index] * style.texture_scale - uv_offset, 1.0f}
            };
            const std::array<SDL_Color, 4> vertex_colors{
                m_colors[index], m_colors[index + 1], m_colors[index + 1], m_colors[index]
            };
            renderer.submit_world_quad(
                white, corners, uvs, vertex_colors, style.z_layer,
                style.sorting_layer, style.order_in_layer,
                style.material, style.render_layer, blend
            );
        }
    }
}

void TrailSystem2D::clear() {
    m_states.clear();
}

std::size_t TrailSystem2D::total_point_count() const {
    std::size_t total = 0;
    for (const auto& [entity, state] : m_states) {
        static_cast<void>(entity);
        total += state.points.size();
    }
    return total;
}

std::size_t TrailSystem2D::point_count(EntityID entity) const {
    const auto found = m_states.find(entity);
    return found == m_states.end() ? 0 : found->second.points.size();
}

} // namespace Engine
