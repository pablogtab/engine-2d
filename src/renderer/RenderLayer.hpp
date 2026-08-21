#pragma once

#include <cstdint>

namespace Engine {

using RenderLayer = std::uint8_t;
using RenderLayerMask = std::uint32_t;

inline constexpr RenderLayer DEFAULT_RENDER_LAYER = 0;
inline constexpr RenderLayerMask ALL_RENDER_LAYERS = ~RenderLayerMask{0};

[[nodiscard]] constexpr RenderLayerMask render_layer_mask(RenderLayer layer) {
    return layer < 32 ? (RenderLayerMask{1} << layer) : RenderLayerMask{0};
}

[[nodiscard]] constexpr bool renders_layer(RenderLayerMask mask, RenderLayer layer) {
    return (mask & render_layer_mask(layer)) != 0;
}

} // namespace Engine
