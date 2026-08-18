#pragma once

#include <algorithm>
#include <cstdint>

namespace Engine::SpriteSortKey {

inline constexpr uint64_t SCREEN_SPACE_FLAG = 1ULL << 63;

inline uint64_t world(int16_t sorting_layer, float depth, float y, int16_t order_in_layer) {
    // Bit 63 is exclusively reserved for screen-space content. World keys use
    // 16 bits for layer, 15 for depth, 16 for Y and 16 for explicit order.
    const uint64_t layer_key = static_cast<uint16_t>(
        static_cast<int32_t>(sorting_layer) + 32768
    );
    const uint64_t depth_key = static_cast<uint64_t>(std::clamp(
        static_cast<int32_t>((depth + 1000.0f) * 8.0f),
        0,
        32767
    ));
    const uint64_t y_key = static_cast<uint64_t>(std::clamp(
        static_cast<int32_t>(y + 32768.0f),
        0,
        65535
    ));
    const uint64_t order_key = static_cast<uint16_t>(
        static_cast<int32_t>(order_in_layer) + 32768
    );

    return (layer_key << 47) | (depth_key << 32) | (y_key << 16) | order_key;
}

inline uint64_t screen(float z_layer, const void* texture) {
    const uint64_t z_key = static_cast<uint64_t>(std::clamp(
        static_cast<int64_t>(z_layer + 1000.0f),
        int64_t{0},
        int64_t{0x7FFFFFFF}
    ));
    const uint64_t texture_key = reinterpret_cast<uintptr_t>(texture) & 0xFFFFFFFFULL;
    return SCREEN_SPACE_FLAG | (z_key << 32) | texture_key;
}

inline bool is_screen_space(uint64_t key) {
    return (key & SCREEN_SPACE_FLAG) != 0;
}

} // namespace Engine::SpriteSortKey
