#pragma once

#include <cstdint>

namespace Engine {

using EntityID = uint32_t;

// Interface for type-erased Component Arrays
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void entity_destroyed(EntityID entity) = 0;
};

} // namespace Engine
