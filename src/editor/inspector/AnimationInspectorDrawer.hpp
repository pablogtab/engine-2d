#pragma once

#include "ComponentInspectorTransaction.hpp"

namespace Engine {

class SpriteAnimationStudioPanel;

struct AnimationPreviewState {
    EntityID entity{0};
    int clip_index{0};
    int current_frame{0};
    float timer{0.0f};
    bool playing{false};
    bool finished{false};

    void reset(EntityID target, const AnimationComponent& animation, int requested_clip = -1);
};

class AnimationInspectorDrawer {
public:
    struct Context {
        EditorCommandHistory* command_history{nullptr};
        EditorPropertyTransaction* transactions{nullptr};
        AnimationPreviewState* preview{nullptr};
        SpriteAnimationStudioPanel* studio{nullptr};
    };

    static void draw(EntityManager& em, EntityID entity, const Context& context);
};

} // namespace Engine
