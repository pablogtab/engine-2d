#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Engine {

using EntityID = std::uint32_t;
using ComponentTypeID = std::uint8_t;
inline constexpr std::size_t MAX_COMPONENTS = 64;
using ComponentMask = std::bitset<MAX_COMPONENTS>;

inline ComponentTypeID get_next_type_id() {
    static std::size_t last_id = 0;
    if (last_id >= MAX_COMPONENTS) {
        throw std::runtime_error("ECS component type limit exceeded (64)");
    }
    return static_cast<ComponentTypeID>(last_id++);
}

template<typename T>
inline ComponentTypeID get_component_type_id() {
    static ComponentTypeID type_id = get_next_type_id();
    return type_id;
}

template<typename... Ts>
struct exclude_t {};

template<typename... Ts>
inline constexpr exclude_t<Ts...> exclude{};

} // namespace Engine
