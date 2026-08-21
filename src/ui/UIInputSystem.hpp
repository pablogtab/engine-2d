#pragma once

#include "CanvasLayout.hpp"
#include "../ecs/EcsTypes.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

class EntityManager;
class EventDispatcher;

struct UIInputFrame {
    glm::vec2 pointer_position{0.0f};
    bool pointer_down{false};
    bool pointer_pressed{false};
    bool pointer_released{false};
    bool focus_next{false};
    bool focus_previous{false};
    bool activate_down{false};
    bool activate_pressed{false};
    bool activate_released{false};
};

enum class UIInteractionState {
    Normal,
    Hovered,
    Pressed,
    Disabled
};

struct UIInputResult {
    EntityID hovered{0};
    EntityID captured{0};
    EntityID focused{0};
    std::vector<EntityID> clicked;
};

class UIInputSystem {
public:
    [[nodiscard]] UIInputResult update(
        EntityManager& entities,
        const glm::vec2& output_size,
        const SafeAreaInsets& safe_area,
        const UIInputFrame& input,
        EventDispatcher* dispatcher = nullptr
    );

    void reset();

    [[nodiscard]] EntityID hovered_entity() const { return m_hovered; }
    [[nodiscard]] EntityID captured_entity() const { return m_captured; }
    [[nodiscard]] EntityID focused_entity() const { return m_focused; }
    [[nodiscard]] UIInteractionState state_for(const EntityManager& entities, EntityID entity) const;

private:
    EntityID m_hovered{0};
    EntityID m_captured{0};
    EntityID m_focused{0};
    EntityID m_keyboard_pressed{0};
};

} // namespace Engine
