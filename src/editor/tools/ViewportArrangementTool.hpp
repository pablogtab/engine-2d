#pragma once

#include "../../ecs/EntityManager.hpp"
#include <unordered_set>

namespace Engine {

class EditorCommandHistory;

enum class ArrangeAction {
    AlignLeft,
    AlignCenter,
    AlignRight,
    AlignBottom,
    AlignMiddle,
    AlignTop,
    DistributeHorizontal,
    DistributeVertical
};

class ViewportArrangementTool {
public:
    static bool apply(
        EntityManager& entity_manager,
        const std::unordered_set<EntityID>& selected_entities,
        ArrangeAction action,
        EditorCommandHistory* command_history
    );

    static const char* get_label(ArrangeAction action);
};

} // namespace Engine
