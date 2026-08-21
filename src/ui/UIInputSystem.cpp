#include "UIInputSystem.hpp"

#include "../core/AssetDatabase.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/UIComponents.hpp"
#include "../events/Event.hpp"
#include "../events/EventDispatcher.hpp"
#include <algorithm>

namespace Engine {
namespace {

struct HitCandidate {
    EntityID entity{0};
    ResolvedUILayout layout;
};

bool is_interactable(const EntityManager& entities, EntityID entity) {
    if (!entities.is_alive(entity) || !entities.has_component<UIButtonComponent>(entity)) return false;
    if (entities.has_component<TransformComponent>(entity) &&
        !entities.get_component<TransformComponent>(entity).world_visible) return false;
    const auto& button = entities.get_component<UIButtonComponent>(entity);
    if (!button.enabled || !button.interactable) return false;
    if (entities.has_component<UIImageComponent>(entity)) {
        const auto& image = entities.get_component<UIImageComponent>(entity);
        if (!image.enabled || !image.raycast_target) return false;
    }
    return true;
}

std::vector<HitCandidate> collect_candidates(
    const EntityManager& entities,
    const glm::vec2& output_size,
    const SafeAreaInsets& safe_area
) {
    std::vector<std::pair<EntityID, const CanvasComponent*>> canvases;
    for (const EntityID entity : entities.get_living_entities()) {
        if (!entities.has_component<CanvasComponent>(entity)) continue;
        const auto& canvas = entities.get_component<CanvasComponent>(entity);
        const bool visible = !entities.has_component<TransformComponent>(entity) ||
            entities.get_component<TransformComponent>(entity).world_visible;
        if (canvas.enabled && visible) canvases.emplace_back(entity, &canvas);
    }
    std::sort(canvases.begin(), canvases.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second->sorting_order != rhs.second->sorting_order) {
            return lhs.second->sorting_order < rhs.second->sorting_order;
        }
        return lhs.first < rhs.first;
    });

    std::vector<HitCandidate> result;
    for (const auto& [canvas_entity, canvas] : canvases) {
        const CanvasViewport viewport = calculate_canvas_viewport(*canvas, output_size, safe_area);
        for (const auto& resolved : resolve_canvas_hierarchy(
                entities, canvas_entity, viewport, &AssetDatabase::get()
            )) {
            if (is_interactable(entities, resolved.entity)) {
                result.push_back({resolved.entity, resolved});
            }
        }
    }
    return result;
}

void publish_click(EventDispatcher* dispatcher, EntityID entity) {
    if (dispatcher) dispatcher->publish(UIButtonClickedEvent{entity});
}

void publish_focus(EventDispatcher* dispatcher, EntityID previous, EntityID current) {
    if (dispatcher && previous != current) dispatcher->publish(UIFocusChangedEvent{previous, current});
}

} // namespace

UIInputResult UIInputSystem::update(
    EntityManager& entities,
    const glm::vec2& output_size,
    const SafeAreaInsets& safe_area,
    const UIInputFrame& input,
    EventDispatcher* dispatcher
) {
    const std::vector<HitCandidate> candidates = collect_candidates(
        entities, glm::max(output_size, glm::vec2(1.0f)), safe_area
    );
    const auto candidate_exists = [&](EntityID entity) {
        return std::any_of(candidates.begin(), candidates.end(), [entity](const HitCandidate& item) {
            return item.entity == entity;
        });
    };

    const EntityID focus_before = m_focused;
    if (!candidate_exists(m_focused)) m_focused = 0;
    if (!candidate_exists(m_captured)) m_captured = 0;
    if (!candidate_exists(m_keyboard_pressed)) m_keyboard_pressed = 0;

    m_hovered = 0;
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        if (point_in_resolved_ui_rect(it->layout, input.pointer_position)) {
            m_hovered = it->entity;
            break;
        }
    }

    if (input.pointer_pressed) {
        m_captured = m_hovered;
        m_focused = m_captured;
    }

    std::vector<EntityID> clicked;
    if (input.pointer_released) {
        if (m_captured != 0 && m_hovered == m_captured) {
            clicked.push_back(m_captured);
            publish_click(dispatcher, m_captured);
        }
        m_captured = 0;
    } else if (!input.pointer_down && !input.pointer_pressed) {
        m_captured = 0;
    }

    if (!candidates.empty() && (input.focus_next || input.focus_previous)) {
        auto current = std::find_if(candidates.begin(), candidates.end(), [&](const HitCandidate& item) {
            return item.entity == m_focused;
        });
        if (input.focus_next) {
            m_focused = current == candidates.end() || ++current == candidates.end()
                ? candidates.front().entity : current->entity;
        } else {
            m_focused = current == candidates.end() || current == candidates.begin()
                ? candidates.back().entity : std::prev(current)->entity;
        }
    }

    if (input.activate_pressed && m_focused != 0) m_keyboard_pressed = m_focused;
    if (input.activate_released) {
        if (m_keyboard_pressed != 0 && m_keyboard_pressed == m_focused && candidate_exists(m_focused)) {
            clicked.push_back(m_focused);
            publish_click(dispatcher, m_focused);
        }
        m_keyboard_pressed = 0;
    } else if (!input.activate_down) {
        m_keyboard_pressed = 0;
    }

    publish_focus(dispatcher, focus_before, m_focused);
    return {m_hovered, m_captured, m_focused, std::move(clicked)};
}

void UIInputSystem::reset() {
    m_hovered = 0;
    m_captured = 0;
    m_focused = 0;
    m_keyboard_pressed = 0;
}

UIInteractionState UIInputSystem::state_for(const EntityManager& entities, EntityID entity) const {
    if (!is_interactable(entities, entity)) return UIInteractionState::Disabled;
    if (entity == m_captured || entity == m_keyboard_pressed) return UIInteractionState::Pressed;
    if (entity == m_hovered || entity == m_focused) return UIInteractionState::Hovered;
    return UIInteractionState::Normal;
}

} // namespace Engine
