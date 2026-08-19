#pragma once

#include "../ecs/EcsTypes.hpp"

namespace Engine {

class IEntityRenderFilter {
public:
    virtual ~IEntityRenderFilter() = default;
    [[nodiscard]] virtual bool should_render(EntityID id) const = 0;
};

} // namespace Engine
