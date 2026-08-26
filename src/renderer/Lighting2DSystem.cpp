#include "Lighting2DSystem.hpp"

#include "Renderer2D.hpp"
#include "RenderTargetPool.hpp"
#include "Texture.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../physics/ColliderGeometry.hpp"
#include "../scene/Camera2D.hpp"
#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Engine {
namespace {

constexpr int CIRCLE_SHADOW_SEGMENTS = 24;
constexpr int LIGHT_GRADIENT_SEGMENTS = 96;
constexpr int LIGHT_GRADIENT_RINGS = 32;

glm::vec2 safe_extrusion(const glm::vec2& point, const glm::vec2& light, float distance) {
    const glm::vec2 delta = point - light;
    const float length = glm::length(delta);
    return length > 0.0001f ? point + delta * (distance / length) : point;
}

SDL_BlendMode preserve_alpha_blend(
    SDL_BlendFactor source_color,
    SDL_BlendFactor destination_color
) {
    return SDL_ComposeCustomBlendMode(
        source_color,
        destination_color,
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_ADD
    );
}

SDL_BlendMode point_composite_mode(
    Light2DBlendMode mode,
    bool composite_uncovered_pixels
) {
    const SDL_BlendFactor source = composite_uncovered_pixels
        ? SDL_BLENDFACTOR_ONE
        : SDL_BLENDFACTOR_DST_ALPHA;
    switch (mode) {
        case Light2DBlendMode::Multiply:
            return preserve_alpha_blend(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA);
        case Light2DBlendMode::Screen:
            return preserve_alpha_blend(source, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR);
        case Light2DBlendMode::Additive:
            return preserve_alpha_blend(source, SDL_BLENDFACTOR_ONE);
    }
    return SDL_BLENDMODE_ADD;
}

SDL_BlendMode shadow_mask_blend() {
    return SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD
    );
}

SDL_BlendMode receiver_intersection_blend() {
    return SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD
    );
}

bool draw_triangle_list(SDL_Renderer* renderer, const std::vector<glm::vec2>& points, float opacity, const glm::vec2& offset = {}) {
    if (points.empty()) return true;
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(points.size());
    for (const glm::vec2& point : points) {
        vertices.push_back({{point.x + offset.x, point.y + offset.y}, {0.0f, 0.0f, 0.0f, std::clamp(opacity, 0.0f, 1.0f)}, {0.0f, 0.0f}});
    }
    return SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
}

std::vector<glm::vec2> collider_polygon(
    const ColliderShape& shape,
    const TransformComponent& transform
) {
    std::vector<glm::vec2> local;
    if (shape.type == ColliderShapeType::Circle) {
        const glm::vec2 center = collider_scaled_offset(shape, transform);
        const float radius = collider_scaled_circle_radius(shape, transform);
        local.reserve(CIRCLE_SHADOW_SEGMENTS);
        for (int index = 0; index < CIRCLE_SHADOW_SEGMENTS; ++index) {
            const float angle = static_cast<float>(index) * 2.0f * glm::pi<float>() /
                static_cast<float>(CIRCLE_SHADOW_SEGMENTS);
            local.push_back(center + glm::vec2(std::cos(angle), std::sin(angle)) * radius);
        }
    } else if (shape.type == ColliderShapeType::Box) {
        const glm::vec2 center = collider_scaled_offset(shape, transform);
        const glm::vec2 half = collider_scaled_box_half_extent(shape, transform);
        local = {
            center + glm::vec2(-half.x, -half.y),
            center + glm::vec2(half.x, -half.y),
            center + glm::vec2(half.x, half.y),
            center + glm::vec2(-half.x, half.y)
        };
    } else {
        local = collider_scaled_polygon_vertices(shape, transform);
    }

    for (glm::vec2& point : local) point = collider_local_to_world(point, transform);
    return local;
}

std::vector<glm::vec2> static_polygon(
    const ShadowCaster2DComponent& caster,
    const TransformComponent& transform
) {
    std::vector<glm::vec2> result;
    result.reserve(caster.vertices.size());
    for (const glm::vec2& vertex : caster.vertices) {
        result.push_back(collider_local_to_world(vertex * transform.world_scale, transform));
    }
    return result;
}

std::vector<glm::vec2> sprite_alpha_contour(const SpriteComponent& sprite, const TransformComponent& transform, float threshold) {
    if (!sprite.texture) return {};
    const SDL_FRect source = sprite.use_src_rect ? sprite.src_rect : SDL_FRect{0.0f, 0.0f, static_cast<float>(sprite.texture->get_width()), static_cast<float>(sprite.texture->get_height())};
    const int left = std::clamp(static_cast<int>(std::floor(source.x)), 0, sprite.texture->get_width() - 1);
    const int top = std::clamp(static_cast<int>(std::floor(source.y)), 0, sprite.texture->get_height() - 1);
    const int width = std::clamp(static_cast<int>(std::ceil(source.w)), 1, sprite.texture->get_width() - left);
    const int height = std::clamp(static_cast<int>(std::ceil(source.h)), 1, sprite.texture->get_height() - top);
    const std::uint8_t alpha_limit = static_cast<std::uint8_t>(std::clamp(threshold, 0.0f, 1.0f) * 255.0f);
    constexpr int samples = 12;
    std::vector<glm::vec2> local_top, local_bottom;
    local_top.reserve(samples + 1); local_bottom.reserve(samples + 1);
    for (int sample = 0; sample <= samples; ++sample) {
        const int x = left + (width - 1) * sample / samples;
        int first = height, last = -1;
        for (int y = 0; y < height; ++y) if (sprite.texture->alpha_at(x, top + y) > alpha_limit) { first = std::min(first, y); last = y; }
        if (last < 0) continue;
        const float u = static_cast<float>(x - left) / std::max(1, width - 1);
        const auto point = [&](int y) { return glm::vec2{(u - transform.world_pivot.x) * sprite.size.x, (static_cast<float>(y) / std::max(1, height - 1) - transform.world_pivot.y) * sprite.size.y}; };
        local_top.push_back(point(first)); local_bottom.push_back(point(last));
    }
    if (local_top.size() < 2) return {};
    std::vector<glm::vec2> result;
    result.reserve(local_top.size() + local_bottom.size());
    for (const auto& point : local_top) result.push_back(collider_local_to_world(point * transform.world_scale, transform));
    for (auto it = local_bottom.rbegin(); it != local_bottom.rend(); ++it) result.push_back(collider_local_to_world(*it * transform.world_scale, transform));
    return result;
}

} // namespace

float point_light_falloff(float distance, float radius, float intensity, float exponent) {
    if (radius <= 0.0f || intensity <= 0.0f || distance >= radius) return 0.0f;
    const float normalized = 1.0f - std::max(0.0f, distance) / radius;
    const float smooth = normalized * normalized * (3.0f - 2.0f * normalized);
    return std::clamp(intensity, 0.0f, 1.0f) *
        std::pow(smooth, std::clamp(exponent, 0.1f, 8.0f));
}

glm::vec4 point_light_bounds(const glm::vec2& position, float radius) {
    const float valid_radius = std::max(0.0f, radius);
    return {
        position.x - valid_radius,
        position.y - valid_radius,
        position.x + valid_radius,
        position.y + valid_radius
    };
}

glm::vec4 blend_light_sample(
    const glm::vec4& base,
    const glm::vec4& light,
    float coverage,
    Light2DBlendMode mode
) {
    const float amount = std::clamp(coverage * light.a, 0.0f, 1.0f);
    const glm::vec3 base_rgb = glm::clamp(glm::vec3(base), 0.0f, 1.0f);
    const glm::vec3 light_rgb = glm::clamp(glm::vec3(light), 0.0f, 1.0f);
    glm::vec3 result = base_rgb;
    switch (mode) {
        case Light2DBlendMode::Additive:
            result = glm::min(base_rgb + light_rgb * amount, glm::vec3(1.0f));
            break;
        case Light2DBlendMode::Multiply:
            result = base_rgb * glm::mix(glm::vec3(1.0f), light_rgb, amount);
            break;
        case Light2DBlendMode::Screen: {
            const glm::vec3 source = light_rgb * amount;
            result = glm::vec3(1.0f) - (glm::vec3(1.0f) - base_rgb) * (glm::vec3(1.0f) - source);
            break;
        }
    }
    return {result, base.a};
}

std::vector<glm::vec2> build_shadow_triangles(
    const glm::vec2& light_position,
    const std::vector<glm::vec2>& polygon,
    float extrusion_distance
) {
    std::vector<glm::vec2> triangles;
    if (polygon.size() < 3 || extrusion_distance <= 0.0f) return triangles;

    triangles.reserve((polygon.size() - 2 + polygon.size() * 2) * 3);
    for (std::size_t index = 1; index + 1 < polygon.size(); ++index) {
        triangles.push_back(polygon[0]);
        triangles.push_back(polygon[index]);
        triangles.push_back(polygon[index + 1]);
    }

    float signed_area = 0.0f;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const glm::vec2& a = polygon[index];
        const glm::vec2& b = polygon[(index + 1) % polygon.size()];
        signed_area += a.x * b.y - b.x * a.y;
    }
    const bool counter_clockwise = signed_area > 0.0f;

    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const glm::vec2 a = polygon[index];
        const glm::vec2 b = polygon[(index + 1) % polygon.size()];
        const glm::vec2 edge = b - a;
        const glm::vec2 outward = counter_clockwise
            ? glm::vec2(edge.y, -edge.x)
            : glm::vec2(-edge.y, edge.x);
        const glm::vec2 midpoint = (a + b) * 0.5f;
        if (glm::dot(outward, light_position - midpoint) <= 0.0f) continue;

        const glm::vec2 far_a = safe_extrusion(a, light_position, extrusion_distance);
        const glm::vec2 far_b = safe_extrusion(b, light_position, extrusion_distance);
        triangles.insert(triangles.end(), {a, b, far_b, a, far_b, far_a});
    }
    return triangles;
}

std::vector<glm::vec2> build_directional_shadow_triangles(
    const glm::vec2& light_direction,
    const std::vector<glm::vec2>& polygon,
    float extrusion_distance
) {
    std::vector<glm::vec2> triangles;
    if (polygon.size() < 3 || extrusion_distance <= 0.0f || glm::length(light_direction) < 0.0001f) return triangles;
    const glm::vec2 direction = glm::normalize(light_direction);
    triangles.reserve((polygon.size() + polygon.size() - 2) * 3);
    for (std::size_t index = 1; index + 1 < polygon.size(); ++index) {
        triangles.insert(triangles.end(), {polygon[0], polygon[index], polygon[index + 1]});
    }
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const glm::vec2 a = polygon[index];
        const glm::vec2 b = polygon[(index + 1) % polygon.size()];
        const glm::vec2 edge = b - a;
        const glm::vec2 normal{edge.y, -edge.x};
        if (glm::dot(normal, direction) <= 0.0f) continue;
        const glm::vec2 far_a = a - direction * extrusion_distance;
        const glm::vec2 far_b = b - direction * extrusion_distance;
        triangles.insert(triangles.end(), {a, b, far_b, a, far_b, far_a});
    }
    return triangles;
}

void Lighting2DPass::update(
    std::vector<Light2DInstance> lights,
    std::vector<ShadowPolygon2D> casters
) {
    std::stable_sort(lights.begin(), lights.end(), [](const Light2DInstance& lhs, const Light2DInstance& rhs) {
        if (lhs.light.shadow_priority != rhs.light.shadow_priority) return lhs.light.shadow_priority > rhs.light.shadow_priority;
        if (lhs.light.intensity != rhs.light.intensity) return lhs.light.intensity > rhs.light.intensity;
        return lhs.entity < rhs.entity;
    });
    m_lights = std::move(lights);
    m_casters = std::move(casters);
    m_affected_layers = 0;
    for (const auto& instance : m_lights) {
        if (instance.light.enabled && instance.light.intensity > 0.0f) {
            m_affected_layers |= instance.light.affected_layers;
        }
    }
}

bool Lighting2DPass::requires_shadow_receiver_mask() const {
    return std::any_of(m_lights.begin(), m_lights.end(), [](const Light2DInstance& instance) {
        return instance.light.enabled && instance.light.mobility == Light2DMobility::Realtime &&
            instance.light.shadow_mode != ShadowMode::None && instance.light.intensity > 0.0f;
    });
}

bool Lighting2DPass::draw_point_gradient(
    SDL_Renderer* renderer,
    const glm::vec2& center,
    float radius,
    const Light2DComponent& light,
    float world_rotation
) {
    if (!renderer || radius <= 0.0f) return false;
    const std::size_t vertex_count = 1 + LIGHT_GRADIENT_RINGS * LIGHT_GRADIENT_SEGMENTS;
    const std::size_t triangle_count = LIGHT_GRADIENT_SEGMENTS +
        (LIGHT_GRADIENT_RINGS - 1) * LIGHT_GRADIENT_SEGMENTS * 2;
    m_radial_vertices.clear();
    m_radial_indices.clear();
    m_radial_vertices.reserve(vertex_count);
    m_radial_indices.reserve(triangle_count * 3);

    const SDL_FColor rgb{
        light.color.r / 255.0f,
        light.color.g / 255.0f,
        light.color.b / 255.0f,
        1.0f
    };
    const auto premultiplied_color = [&](float coverage) {
        return SDL_FColor{
            rgb.r * coverage,
            rgb.g * coverage,
            rgb.b * coverage,
            coverage
        };
    };
    const SDL_FColor center_color = premultiplied_color(
        point_light_falloff(0.0f, radius, 1.0f, light.falloff)
    );
    m_radial_vertices.push_back({{center.x, center.y}, center_color, {0.5f, 0.5f}});

    for (int ring = 1; ring <= LIGHT_GRADIENT_RINGS; ++ring) {
        const float normalized_radius = static_cast<float>(ring) / LIGHT_GRADIENT_RINGS;
        const SDL_FColor ring_color = premultiplied_color(point_light_falloff(
            radius * normalized_radius, radius, 1.0f, light.falloff
        ));
        for (int segment = 0; segment < LIGHT_GRADIENT_SEGMENTS; ++segment) {
            const float angle = static_cast<float>(segment) * 2.0f * glm::pi<float>() /
                LIGHT_GRADIENT_SEGMENTS;
            float cone_coverage = 1.0f;
            if (light.type == Light2DType::Spot) {
                const float relative = std::abs(std::remainder(
                    angle - glm::radians(world_rotation), 2.0f * glm::pi<float>()
                ));
                const float inner = glm::radians(std::clamp(light.spot_inner_angle, 0.0f, 179.0f) * 0.5f);
                const float outer = std::max(inner, glm::radians(std::clamp(light.spot_outer_angle, 0.0f, 180.0f) * 0.5f));
                cone_coverage = outer <= inner ? (relative <= outer ? 1.0f : 0.0f) :
                    std::clamp((outer - relative) / (outer - inner), 0.0f, 1.0f);
            }
            m_radial_vertices.push_back({
                {
                    center.x + std::cos(angle) * radius * normalized_radius,
                    center.y + std::sin(angle) * radius * normalized_radius
                },
                {ring_color.r * cone_coverage, ring_color.g * cone_coverage, ring_color.b * cone_coverage, ring_color.a * cone_coverage},
                {0.0f, 0.0f}
            });
        }
    }

    for (int segment = 0; segment < LIGHT_GRADIENT_SEGMENTS; ++segment) {
        const int next = (segment + 1) % LIGHT_GRADIENT_SEGMENTS;
        m_radial_indices.insert(m_radial_indices.end(), {0, 1 + segment, 1 + next});
    }
    for (int ring = 1; ring < LIGHT_GRADIENT_RINGS; ++ring) {
        const int inner = 1 + (ring - 1) * LIGHT_GRADIENT_SEGMENTS;
        const int outer = 1 + ring * LIGHT_GRADIENT_SEGMENTS;
        for (int segment = 0; segment < LIGHT_GRADIENT_SEGMENTS; ++segment) {
            const int next = (segment + 1) % LIGHT_GRADIENT_SEGMENTS;
            m_radial_indices.insert(m_radial_indices.end(), {
                inner + segment, outer + segment, outer + next,
                inner + segment, outer + next, inner + next
            });
        }
    }

    // The map is cleared for every point light. Replace pixels instead of
    // accumulating shared triangle edges, whose subpixel coverage otherwise
    // changes with camera zoom and produces apparent intensity pumping.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    return SDL_RenderGeometry(
        renderer,
        nullptr,
        m_radial_vertices.data(),
        static_cast<int>(m_radial_vertices.size()),
        m_radial_indices.data(),
        static_cast<int>(m_radial_indices.size())
    );
}

bool Lighting2DPass::apply(const LightingContext& context) {
    if (!context.renderer || !context.scene_target || !context.camera ||
        !context.render_targets || !renders_layer(m_affected_layers, context.render_layer) ||
        !context.camera->renders_layer(context.render_layer)) {
        return false;
    }

    SDL_Texture* const light_map = context.render_targets->acquire(context.width, context.height);
    if (!light_map) return false;
    SDL_Texture* const shadow_mask = context.shadow_receiver_mask
        ? context.render_targets->acquire(context.width, context.height) : nullptr;
    struct ReturnTarget final {
        RenderTargetPool* pool{nullptr};
        SDL_Texture* texture{nullptr};
        SDL_Texture* second{nullptr};
        ~ReturnTarget() {
            if (pool && texture) pool->release(texture);
            if (pool && second) pool->release(second);
        }
    } return_target{context.render_targets, light_map, shadow_mask};

    SDL_Texture* previous_target = SDL_GetRenderTarget(context.renderer);
    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(context.renderer, &previous_blend);
    std::uint8_t previous_r = 0;
    std::uint8_t previous_g = 0;
    std::uint8_t previous_b = 0;
    std::uint8_t previous_a = 0;
    SDL_GetRenderDrawColor(context.renderer, &previous_r, &previous_g, &previous_b, &previous_a);

    bool drew = false;
    SDL_SetRenderTarget(context.renderer, context.scene_target);
    int shadowed_lights = 0;
    for (const Light2DInstance& instance : m_lights) {
        const Light2DComponent& light = instance.light;
        if (!light.enabled || light.mobility != Light2DMobility::Realtime || light.intensity <= 0.0f ||
            !renders_layer(light.affected_layers, context.render_layer)) continue;

        const float intensity = std::clamp(light.intensity, 0.0f, 1.0f);
        const bool composite_uncovered_pixels =
            (light.affected_layers & context.previously_composited_layers) == 0;
        if (light.type == Light2DType::Ambient) {
            if (light.blend_mode == Light2DBlendMode::Multiply) {
                const auto channel = [&](std::uint8_t value) {
                    return static_cast<std::uint8_t>(255.0f + (static_cast<float>(value) - 255.0f) * intensity);
                };
                SDL_SetRenderDrawBlendMode(context.renderer, SDL_BLENDMODE_MOD);
                SDL_SetRenderDrawColor(context.renderer, channel(light.color.r), channel(light.color.g),
                                       channel(light.color.b), 255);
            } else {
                const SDL_BlendFactor destination = light.blend_mode == Light2DBlendMode::Screen
                    ? SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR
                    : SDL_BLENDFACTOR_ONE;
                const SDL_BlendFactor source = composite_uncovered_pixels
                    ? SDL_BLENDFACTOR_ONE
                    : SDL_BLENDFACTOR_DST_ALPHA;
                SDL_SetRenderDrawBlendMode(
                    context.renderer,
                    preserve_alpha_blend(source, destination)
                );
                const auto channel = [&](std::uint8_t value) {
                    return static_cast<std::uint8_t>(static_cast<float>(value) * intensity);
                };
                SDL_SetRenderDrawColor(
                    context.renderer,
                    channel(light.color.r),
                    channel(light.color.g),
                    channel(light.color.b),
                    255
                );
            }
            SDL_RenderFillRect(context.renderer, nullptr);
            drew = true;
            continue;
        }

        if (light.type != Light2DType::Directional && light.range <= 0.0f) continue;
        SDL_SetRenderTarget(context.renderer, light_map);
        SDL_SetRenderDrawBlendMode(context.renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(context.renderer, 0, 0, 0, 0);
        SDL_RenderClear(context.renderer);

        const glm::vec2 center = context.camera->world_to_screen(
            glm::vec3(instance.world_position, 0.0f)
        ) * context.raster_scale;
        const glm::vec2 edge = context.camera->world_to_screen(
            glm::vec3(instance.world_position + glm::vec2(light.range, 0.0f), 0.0f)
        ) * context.raster_scale;
        const float screen_radius = glm::length(edge - center);
        if (light.type == Light2DType::Directional) {
            SDL_SetRenderDrawColor(context.renderer, light.color.r, light.color.g, light.color.b, 255);
            SDL_RenderFillRect(context.renderer, nullptr);
        } else {
            draw_point_gradient(context.renderer, center, screen_radius, light, instance.world_rotation);
        }

        const bool shadows_enabled = light.shadow_mode != ShadowMode::None &&
            shadowed_lights < m_max_shadowed_lights &&
            (!context.shadow_receiver_mask || shadow_mask);
        if (shadows_enabled) {
            ++shadowed_lights;
            if (shadow_mask) {
                SDL_SetRenderTarget(context.renderer, shadow_mask);
                SDL_SetRenderDrawBlendMode(context.renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(context.renderer, 0, 0, 0, 0);
                SDL_RenderClear(context.renderer);
                SDL_SetRenderDrawBlendMode(context.renderer, SDL_BLENDMODE_ADD);
            } else {
                SDL_SetRenderTarget(context.renderer, light_map);
                SDL_SetRenderDrawBlendMode(context.renderer, shadow_mask_blend());
            }
            for (const ShadowPolygon2D& caster : m_casters) {
                if (!renders_layer(caster.affected_layers, context.render_layer)) continue;
                std::vector<glm::vec2> biased_vertices = caster.world_vertices;
                if (light.shadow_bias > 0.0f) {
                    for (glm::vec2& vertex : biased_vertices) {
                        const glm::vec2 direction = light.type == Light2DType::Directional
                            ? glm::vec2(std::cos(glm::radians(instance.world_rotation)), std::sin(glm::radians(instance.world_rotation)))
                            : glm::normalize(vertex - instance.world_position);
                        if (glm::length(direction) > 0.0001f) vertex += direction * light.shadow_bias;
                    }
                }
                const auto world_triangles = light.type == Light2DType::Directional
                    ? build_directional_shadow_triangles(
                        glm::vec2(std::cos(glm::radians(instance.world_rotation)), std::sin(glm::radians(instance.world_rotation))),
                        biased_vertices, static_cast<float>(std::max(context.width, context.height)) * 4.0f
                    )
                    : build_shadow_triangles(instance.world_position, biased_vertices, light.range * 2.0f);
                std::vector<glm::vec2> screen_triangles;
                screen_triangles.reserve(world_triangles.size());
                for (const glm::vec2& point : world_triangles) {
                    screen_triangles.push_back(
                        context.camera->world_to_screen(glm::vec3(point, 0.0f)) * context.raster_scale
                    );
                }
                const float softness = light.shadow_mode == ShadowMode::Hard ? 0.0f : light.shadow_softness * 8.0f;
                const int samples = softness > 0.01f ? 5 : 1;
                for (int sample = 0; sample < samples; ++sample) {
                    const float angle = static_cast<float>(sample) * 2.0f * glm::pi<float>() / samples;
                    const glm::vec2 offset = samples == 1 ? glm::vec2{} : glm::vec2(std::cos(angle), std::sin(angle)) * softness;
                    draw_triangle_list(context.renderer, screen_triangles, light.shadow_strength / samples, offset);
                }
            }
            if (shadow_mask) {
                SDL_SetTextureBlendMode(context.shadow_receiver_mask, receiver_intersection_blend());
                SDL_RenderTexture(context.renderer, context.shadow_receiver_mask, nullptr, nullptr);
                SDL_SetRenderTarget(context.renderer, light_map);
                SDL_SetTextureBlendMode(shadow_mask, shadow_mask_blend());
                SDL_RenderTexture(context.renderer, shadow_mask, nullptr, nullptr);
            }
        }

        SDL_SetRenderTarget(context.renderer, context.scene_target);
        const float composite_intensity = intensity * (light.color.a / 255.0f);
        SDL_SetTextureColorModFloat(
            light_map, composite_intensity, composite_intensity, composite_intensity
        );
        SDL_SetTextureAlphaModFloat(light_map, composite_intensity);
        SDL_SetTextureBlendMode(
            light_map,
            point_composite_mode(light.blend_mode, composite_uncovered_pixels)
        );
        SDL_RenderTexture(context.renderer, light_map, nullptr, nullptr);
        drew = true;
    }

    SDL_SetRenderTarget(context.renderer, previous_target);
    SDL_SetRenderDrawBlendMode(context.renderer, previous_blend);
    SDL_SetRenderDrawColor(context.renderer, previous_r, previous_g, previous_b, previous_a);
    return drew;
}

Lighting2DSystem::Lighting2DSystem() : m_runtime_pass(std::make_shared<Lighting2DPass>()) {}

void Lighting2DSystem::configure(EntityManager& entities, Renderer2D& renderer) {
    std::vector<Light2DInstance> lights;
    std::vector<ShadowPolygon2D> casters;

    entities.for_each<TransformComponent, Light2DComponent>(
        [&](EntityID entity, const TransformComponent& transform, const Light2DComponent& light) {
            if (!transform.world_visible || !light.enabled || light.mobility != Light2DMobility::Realtime || light.intensity <= 0.0f) return;
            lights.push_back({glm::vec2(transform.world_position), light, entity, transform.world_rotation});
        }
    );

    entities.for_each<TransformComponent, ShadowCaster2DComponent>(
        [&](EntityID entity, const TransformComponent& transform, const ShadowCaster2DComponent& caster) {
            if (!transform.world_visible || !caster.enabled) return;
            if (caster.source == ShadowGeometrySource::Collider) {
                if (!entities.has_component<ColliderComponent>(entity)) return;
                const auto& collider = entities.get_component<ColliderComponent>(entity);
                for (const ColliderShape& shape : collider.shapes) {
                    auto polygon = collider_polygon(shape, transform);
                    if (polygon.size() >= 3) casters.push_back({std::move(polygon), caster.affected_layers});
                }
            } else if (caster.source == ShadowGeometrySource::StaticPolygon) {
                auto polygon = static_polygon(caster, transform);
                if (polygon.size() >= 3) casters.push_back({std::move(polygon), caster.affected_layers});
            } else if (entities.has_component<SpriteComponent>(entity)) {
                auto polygon = sprite_alpha_contour(entities.get_component<SpriteComponent>(entity), transform, caster.alpha_threshold);
                casters.push_back({std::move(polygon), caster.affected_layers});
            }
        }
    );

    m_runtime_pass->update(std::move(lights), std::move(casters));
    renderer.add_lighting_pass(m_runtime_pass);
}

} // namespace Engine
