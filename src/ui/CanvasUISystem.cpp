#include "CanvasUISystem.hpp"

#include "UITextLayout.hpp"

#include "../core/AssetDatabase.hpp"
#include "../core/AssetManager.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/UIComponents.hpp"
#include "../renderer/Font.hpp"
#include "../renderer/IEntityRenderFilter.hpp"
#include "../renderer/Renderer2D.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Engine {
namespace {

constexpr float k_font_atlas_size = 48.0f;
constexpr SDL_Color k_editor_frame_color{78, 183, 235, 210};

bool is_visible(
    const EntityManager& entities,
    EntityID entity,
    const IEntityRenderFilter* visibility_filter
) {
    if (visibility_filter && !visibility_filter->should_render(entity)) return false;
    return !entities.has_component<TransformComponent>(entity) ||
        entities.get_component<TransformComponent>(entity).world_visible;
}

std::vector<std::pair<EntityID, const CanvasComponent*>> collect_canvases(
    const EntityManager& entities,
    const IEntityRenderFilter* visibility_filter
) {
    std::vector<std::pair<EntityID, const CanvasComponent*>> canvases;
    for (const EntityID entity : entities.get_living_entities()) {
        if (!entities.has_component<CanvasComponent>(entity)) continue;
        const auto& canvas = entities.get_component<CanvasComponent>(entity);
        if (!canvas.enabled || !is_visible(entities, entity, visibility_filter)) continue;
        canvases.emplace_back(entity, &canvas);
    }
    std::sort(canvases.begin(), canvases.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second->sorting_order != rhs.second->sorting_order) {
            return lhs.second->sorting_order < rhs.second->sorting_order;
        }
        return lhs.first < rhs.first;
    });
    return canvases;
}

SDL_Color multiply_color(const SDL_Color& lhs, const SDL_Color& rhs) {
    const auto channel = [](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>((static_cast<unsigned int>(a) * b + 127U) / 255U);
    };
    return {
        channel(lhs.r, rhs.r), channel(lhs.g, rhs.g),
        channel(lhs.b, rhs.b), channel(lhs.a, rhs.a)
    };
}

SDL_Color button_color(const UIButtonComponent& button, UIInteractionState state) {
    switch (state) {
        case UIInteractionState::Hovered: return button.hovered_color;
        case UIInteractionState::Pressed: return button.pressed_color;
        case UIInteractionState::Disabled: return button.disabled_color;
        case UIInteractionState::Normal: return button.normal_color;
    }
    return button.normal_color;
}

struct ImageGeometry {
    UIRect destination;
    SDL_FRect source{0.0f, 0.0f, 0.0f, 0.0f};
    bool has_source{false};
};

ImageGeometry resolve_image_geometry(Texture& texture, UIImageScaleMode mode, const UIRect& rect) {
    ImageGeometry result{rect};
    const float texture_width = static_cast<float>(std::max(texture.get_width(), 1));
    const float texture_height = static_cast<float>(std::max(texture.get_height(), 1));
    if (rect.size.x <= 0.0f || rect.size.y <= 0.0f || mode == UIImageScaleMode::Stretch) return result;

    const float texture_aspect = texture_width / texture_height;
    const float rect_aspect = rect.size.x / rect.size.y;
    if (mode == UIImageScaleMode::Fit) {
        if (texture_aspect > rect_aspect) {
            result.destination.size.y = rect.size.x / texture_aspect;
            result.destination.position.y += (rect.size.y - result.destination.size.y) * 0.5f;
        } else {
            result.destination.size.x = rect.size.y * texture_aspect;
            result.destination.position.x += (rect.size.x - result.destination.size.x) * 0.5f;
        }
        return result;
    }

    result.has_source = true;
    if (texture_aspect > rect_aspect) {
        result.source.w = texture_height * rect_aspect;
        result.source.h = texture_height;
        result.source.x = (texture_width - result.source.w) * 0.5f;
    } else {
        result.source.w = texture_width;
        result.source.h = texture_width / rect_aspect;
        result.source.y = (texture_height - result.source.h) * 0.5f;
    }
    return result;
}

float glyph_advance(const Font& font, char character, float scale) {
    const Glyph* glyph = font.get_glyph(character);
    return glyph ? glyph->advance_x * scale : font.get_font_size() * 0.5f * scale;
}

std::vector<std::string> wrap_text(
    const Font& font,
    const std::string& text,
    float scale,
    float maximum_width,
    bool wrap
) {
    std::vector<std::string> lines(1);
    float width = 0.0f;
    for (const char character : text) {
        if (character == '\n') {
            lines.emplace_back();
            width = 0.0f;
            continue;
        }
        const float advance = glyph_advance(font, character, scale);
        if (wrap && maximum_width > 0.0f && width + advance > maximum_width && !lines.back().empty()) {
            lines.emplace_back();
            width = 0.0f;
        }
        lines.back().push_back(character);
        width += advance;
    }
    return lines;
}

void render_text(
    Renderer2D& renderer,
    const Font& font,
    const UITextComponent& text,
    const ResolvedUILayout& resolved,
    const CanvasViewport& viewport,
    float z_layer,
    CanvasUIRenderStats& stats
) {
    const UIRect& rect = resolved.screen_rect;
    const float canvas_scale = std::min(viewport.scale.x, viewport.scale.y);
    const float scale = std::max(text.font_size * canvas_scale / k_font_atlas_size, 0.001f);
    const UIRect content_rect = calculate_ui_text_content_rect(
        rect, text, viewport.scale
    );
    const auto lines = wrap_text(font, text.text, scale, content_rect.size.x, text.wrap);
    const float line_height = font.get_line_height() * scale;
    const float block_height = line_height * static_cast<float>(lines.size());
    float y = content_rect.position.y;
    if (text.vertical_alignment == UIVerticalAlignment::Center) {
        y += (content_rect.size.y - block_height) * 0.5f;
    } else if (text.vertical_alignment == UIVerticalAlignment::Bottom) {
        y += content_rect.size.y - block_height;
    }

    for (const auto& line : lines) {
        const float width = font.measure_text(line, scale).x;
        float x = content_rect.position.x;
        if (text.horizontal_alignment == UIHorizontalAlignment::Center) {
            x += (content_rect.size.x - width) * 0.5f;
        } else if (text.horizontal_alignment == UIHorizontalAlignment::Right) {
            x += content_rect.size.x - width;
        }
        renderer.submit_screen_text(
            font,
            line,
            {x, y + font.get_line_baseline_offset() * scale},
            scale,
            text.color,
            {0.0f, 0.0f},
            z_layer,
            false,
            text.material,
            resolved.rotation,
            resolved.screen_pivot
        );
        y += line_height;
        ++stats.text_lines;
    }
}

} // namespace

CanvasUISystem::CanvasUISystem() = default;
CanvasUISystem::~CanvasUISystem() = default;

CanvasUIRenderStats CanvasUISystem::render(
    EntityManager& entities,
    Renderer2D& renderer,
    const AssetDatabase& database,
    const glm::vec2& output_size,
    const SafeAreaInsets& safe_area,
    const UIInputSystem* input,
    const IEntityRenderFilter* visibility_filter
) {
    CanvasUIRenderStats stats;
    for (const auto& [canvas_entity, canvas] : collect_canvases(entities, visibility_filter)) {
        ++stats.canvases;
        const CanvasViewport viewport = calculate_canvas_viewport(*canvas, output_size, safe_area);
        render_canvas_contents(
            entities,
            renderer,
            database,
            canvas_entity,
            viewport,
            static_cast<float>(canvas->sorting_order),
            input,
            visibility_filter,
            false,
            false,
            stats
        );
    }
    return stats;
}

CanvasUIRenderStats CanvasUISystem::render_editor_preview(
    EntityManager& entities,
    Renderer2D& renderer,
    const AssetDatabase& database,
    const Camera2D& editor_camera,
    const SafeAreaInsets& safe_area,
    bool draw_safe_area_overlay,
    const IEntityRenderFilter* visibility_filter
) {
    CanvasUIRenderStats stats;
    for (const auto& [canvas_entity, canvas] : collect_canvases(entities, visibility_filter)) {
        ++stats.canvases;
        glm::vec2 world_position{0.0f};
        if (entities.has_component<TransformComponent>(canvas_entity)) {
            world_position = glm::vec2(
                entities.get_component<TransformComponent>(canvas_entity).world_position
            );
        }
        const CanvasViewport viewport = calculate_canvas_editor_viewport(
            *canvas, world_position, editor_camera, safe_area
        );
        render_canvas_contents(
            entities,
            renderer,
            database,
            canvas_entity,
            viewport,
            static_cast<float>(canvas->sorting_order),
            nullptr,
            visibility_filter,
            true,
            draw_safe_area_overlay,
            stats
        );
    }
    return stats;
}

void CanvasUISystem::render_canvas_contents(
    EntityManager& entities,
    Renderer2D& renderer,
    const AssetDatabase& database,
    EntityID canvas_entity,
    const CanvasViewport& viewport,
    float z_layer,
    const UIInputSystem* input,
    const IEntityRenderFilter* visibility_filter,
    bool draw_editor_frame,
    bool draw_safe_area_overlay,
    CanvasUIRenderStats& stats
) {
    Texture* white = AssetManager::get().get_white_texture(renderer.get_sdl_renderer());

    for (const auto& resolved : resolve_canvas_hierarchy(
            entities, canvas_entity, viewport, &database
        )) {
        const EntityID entity = resolved.entity;
        if (!is_visible(entities, entity, visibility_filter)) continue;

        const UIImageComponent* image = entities.has_component<UIImageComponent>(entity)
            ? &entities.get_component<UIImageComponent>(entity) : nullptr;
        const UIButtonComponent* button = entities.has_component<UIButtonComponent>(entity)
            ? &entities.get_component<UIButtonComponent>(entity) : nullptr;
        const bool draw_image = image && image->enabled;
        const bool draw_button_background = button && button->enabled && !draw_image;
        if (draw_image || draw_button_background) {
            Texture* texture = white;
            SDL_Color color = draw_image ? image->color : SDL_Color{255, 255, 255, 255};
            UIImageScaleMode scale_mode = draw_image ? image->scale_mode : UIImageScaleMode::Stretch;
            if (draw_image && image->texture_handle.is_valid()) {
                const std::string path = database.resolve(image->texture_handle);
                if (!path.empty()) {
                    texture = AssetManager::get().get_texture(renderer.get_sdl_renderer(), path);
                }
            }
            if (button && button->enabled) {
                const UIInteractionState state = input
                    ? input->state_for(entities, entity)
                    : (!button->interactable ? UIInteractionState::Disabled : UIInteractionState::Normal);
                color = multiply_color(color, button_color(*button, state));
            }
            if (texture) {
                const ImageGeometry geometry = resolve_image_geometry(
                    *texture, scale_mode, resolved.screen_rect
                );
                const glm::vec2 safe_size = glm::max(
                    geometry.destination.size, glm::vec2(0.0001f)
                );
                const glm::vec2 image_pivot =
                    (resolved.screen_pivot - geometry.destination.position) / safe_size;
                renderer.submit_screen_sprite(
                    texture,
                    resolved.screen_pivot,
                    geometry.destination.size,
                    resolved.rotation,
                    color,
                    geometry.has_source ? &geometry.source : nullptr,
                    image_pivot,
                    z_layer,
                    false,
                    false,
                    image ? image->material : nullptr,
                    false
                );
                if (draw_image) ++stats.images;
                else ++stats.button_backgrounds;
            }
        }

        if (entities.has_component<UITextComponent>(entity)) {
            const auto& text = entities.get_component<UITextComponent>(entity);
            if (!text.enabled || text.text.empty()) continue;
            if (Font* font = resolve_font(renderer.get_sdl_renderer(), text.font_handle, database)) {
                render_text(renderer, *font, text, resolved, viewport, z_layer, stats);
            }
        }
    }

    if (draw_editor_frame && white) {
        constexpr float thickness = 2.0f;
        const auto& frame = viewport.output_rect;
        const auto submit_edge = [&](const glm::vec2& position, const glm::vec2& size, SDL_Color color) {
            renderer.submit_screen_sprite(
                white, position, size, 0.0f, color,
                nullptr, {0.0f, 0.0f}, z_layer, false, false, nullptr, false
            );
        };
        submit_edge(frame.position, {frame.size.x, thickness}, k_editor_frame_color);
        submit_edge(
            {frame.position.x, frame.position.y + frame.size.y - thickness},
            {frame.size.x, thickness}, k_editor_frame_color
        );
        submit_edge(frame.position, {thickness, frame.size.y}, k_editor_frame_color);
        submit_edge(
            {frame.position.x + frame.size.x - thickness, frame.position.y},
            {thickness, frame.size.y}, k_editor_frame_color
        );
        const bool has_safe_area = viewport.platform_safe_rect != viewport.output_rect;
        if (draw_safe_area_overlay && has_safe_area) {
            constexpr SDL_Color safe_color{255, 188, 74, 210};
            const auto& safe = viewport.platform_safe_rect;
            submit_edge(safe.position, {safe.size.x, thickness}, safe_color);
            submit_edge(
                {safe.position.x, safe.position.y + safe.size.y - thickness},
                {safe.size.x, thickness}, safe_color
            );
            submit_edge(safe.position, {thickness, safe.size.y}, safe_color);
            submit_edge(
                {safe.position.x + safe.size.x - thickness, safe.position.y},
                {thickness, safe.size.y}, safe_color
            );
        }
        ++stats.editor_frames;
    }
}

Font* CanvasUISystem::resolve_font(
    SDL_Renderer* renderer,
    AssetHandle handle,
    const AssetDatabase& database
) {
    if (handle.is_valid()) {
        if (const auto found = m_fonts.find(handle); found != m_fonts.end()) return found->second.get();
        if (!m_failed_fonts.contains(handle)) {
            const std::string path = database.resolve(handle);
            if (!path.empty()) {
                auto font = Font::load_from_file(renderer, path, k_font_atlas_size);
                if (font) {
                    Font* result = font.get();
                    m_fonts.emplace(handle, std::move(font));
                    return result;
                }
            }
            m_failed_fonts.insert(handle);
        }
    }
    if (!m_default_font) m_default_font = Font::create_default(renderer, k_font_atlas_size);
    return m_default_font.get();
}

void CanvasUISystem::clear() {
    m_fonts.clear();
    m_failed_fonts.clear();
    m_default_font.reset();
}

} // namespace Engine
