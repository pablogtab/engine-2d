#include "ViewportArrangementTool.hpp"
#include "EditorTransformUtils.hpp"
#include "ViewportSelectionGeometry.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Engine {

namespace {

struct ArrangementItem {
    EntityID id{0};
    TransformComponent before;
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    [[nodiscard]] glm::vec2 center() const { return (min + max) * 0.5f; }
    [[nodiscard]] glm::vec2 size() const { return max - min; }
};

bool collect_items(EntityManager& entity_manager, const std::unordered_set<EntityID>& selected_entities, std::vector<ArrangementItem>& items) {
    items.clear();
    for (EntityID id : selected_entities) {
        if (!entity_manager.is_alive(id) || !entity_manager.has_component<TransformComponent>(id)) continue;
        if (has_selected_ancestor(entity_manager, id, selected_entities)) continue;

        const auto& transform = entity_manager.get_component<TransformComponent>(id);
        if (!transform.world_visible) continue;
        ViewportSelectionQuad quad;
        if (!get_selection_quad(entity_manager, id, quad)) continue;

        ArrangementItem item;
        item.id = id;
        item.before = transform;
        item.min = quad.corners.front();
        item.max = quad.corners.front();
        for (const glm::vec2& corner : quad.corners) {
            item.min = glm::min(item.min, corner);
            item.max = glm::max(item.max, corner);
        }
        items.push_back(item);
    }
    return items.size() >= 2;
}

void move_item(EntityManager& entity_manager, const ArrangementItem& item, const glm::vec2& world_delta) {
    auto& transform = entity_manager.get_component<TransformComponent>(item.id);
    transform.position = item.before.position + glm::vec3(world_delta_to_local(entity_manager, item.id, world_delta), 0.0f);
}

} // namespace

const char* ViewportArrangementTool::get_label(ArrangeAction action) {
    switch (action) {
        case ArrangeAction::AlignLeft: return "Align Left";
        case ArrangeAction::AlignCenter: return "Align Center";
        case ArrangeAction::AlignRight: return "Align Right";
        case ArrangeAction::AlignBottom: return "Align Bottom";
        case ArrangeAction::AlignMiddle: return "Align Middle";
        case ArrangeAction::AlignTop: return "Align Top";
        case ArrangeAction::DistributeHorizontal: return "Distribute Horizontally";
        case ArrangeAction::DistributeVertical: return "Distribute Vertically";
    }
    return "Arrange Entities";
}

bool ViewportArrangementTool::apply(
    EntityManager& entity_manager,
    const std::unordered_set<EntityID>& selected_entities,
    ArrangeAction action,
    EditorCommandHistory* command_history
) {
    if (!command_history) return false;

    std::vector<ArrangementItem> items;
    if (!collect_items(entity_manager, selected_entities, items)) return false;
    if ((action == ArrangeAction::DistributeHorizontal || action == ArrangeAction::DistributeVertical) && items.size() < 3) return false;

    glm::vec2 group_min = items.front().min;
    glm::vec2 group_max = items.front().max;
    for (const ArrangementItem& item : items) {
        group_min = glm::min(group_min, item.min);
        group_max = glm::max(group_max, item.max);
    }

    if (action == ArrangeAction::DistributeHorizontal) {
        std::sort(items.begin(), items.end(), [](const ArrangementItem& lhs, const ArrangementItem& rhs) { return lhs.min.x < rhs.min.x; });
        float occupied_width = 0.0f;
        for (const ArrangementItem& item : items) occupied_width += item.size().x;
        const float gap = (group_max.x - group_min.x - occupied_width) / static_cast<float>(items.size() - 1);
        float next_left = group_min.x;
        for (const ArrangementItem& item : items) {
            move_item(entity_manager, item, glm::vec2(next_left - item.min.x, 0.0f));
            next_left += item.size().x + gap;
        }
    } else if (action == ArrangeAction::DistributeVertical) {
        std::sort(items.begin(), items.end(), [](const ArrangementItem& lhs, const ArrangementItem& rhs) { return lhs.min.y < rhs.min.y; });
        float occupied_height = 0.0f;
        for (const ArrangementItem& item : items) occupied_height += item.size().y;
        const float gap = (group_max.y - group_min.y - occupied_height) / static_cast<float>(items.size() - 1);
        float next_bottom = group_min.y;
        for (const ArrangementItem& item : items) {
            move_item(entity_manager, item, glm::vec2(0.0f, next_bottom - item.min.y));
            next_bottom += item.size().y + gap;
        }
    } else {
        for (const ArrangementItem& item : items) {
            glm::vec2 delta(0.0f);
            switch (action) {
                case ArrangeAction::AlignLeft: delta.x = group_min.x - item.min.x; break;
                case ArrangeAction::AlignCenter: delta.x = (group_min.x + group_max.x) * 0.5f - item.center().x; break;
                case ArrangeAction::AlignRight: delta.x = group_max.x - item.max.x; break;
                case ArrangeAction::AlignBottom: delta.y = group_min.y - item.min.y; break;
                case ArrangeAction::AlignMiddle: delta.y = (group_min.y + group_max.y) * 0.5f - item.center().y; break;
                case ArrangeAction::AlignTop: delta.y = group_max.y - item.max.y; break;
                default: break;
            }
            move_item(entity_manager, item, delta);
        }
    }

    TransformSystem::update(entity_manager);
    std::unordered_map<EntityID, TransformComponent> before;
    std::unordered_map<EntityID, TransformComponent> after;
    bool changed = false;
    for (const ArrangementItem& item : items) {
        const auto& current = entity_manager.get_component<TransformComponent>(item.id);
        before[item.id] = item.before;
        after[item.id] = current;
        changed |= glm::distance(item.before.position, current.position) > 0.001f;
        PhysicsSystem::set_position(entity_manager, item.id, glm::vec2(current.world_position));
    }
    if (!changed) return false;

    command_history->push_executed_command(std::make_unique<TransformEntitiesCommand>(
        &entity_manager,
        std::move(before),
        std::move(after),
        get_label(action)
    ));
    return true;
}

} // namespace Engine
