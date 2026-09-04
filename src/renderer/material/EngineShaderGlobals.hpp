#pragma once

namespace Engine {

// Backend-neutral layout available to fragment shaders that opt into an
// engine-owned uniform buffer. Keep this as two float4 rows so HLSL, SPIR-V,
// DXIL, and Metal agree on the packing without backend-specific padding.
struct alignas(16) EngineShaderGlobals {
    float game_time_seconds{0.0f};
    float real_time_seconds{0.0f};
    float delta_time_seconds{0.0f};
    float frame_index{0.0f};

    float output_width{1.0f};
    float output_height{1.0f};
    float inverse_output_width{1.0f};
    float inverse_output_height{1.0f};
};

static_assert(sizeof(EngineShaderGlobals) == 32);

} // namespace Engine
