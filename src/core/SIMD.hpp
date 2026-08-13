#pragma once

#include <glm/glm.hpp>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define ENGINE_SIMD_ARM_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #include <emmintrin.h>
    #define ENGINE_SIMD_SSE2 1
#endif

namespace Engine {

class SIMD {
public:
    // SIMD accelerated 2D quad rotation and translation
    static inline void transform_quad_corners(
        const glm::vec2* src_corners, // 4 corners relative to center
        glm::vec2* dst_corners,       // 4 transformed output screen corners
        const glm::vec2& screen_pos,
        float rotation
    ) {
#if defined(ENGINE_SIMD_ARM_NEON)
        if (rotation == 0.0f) {
            // Load screen pos into 2x 64-bit NEON registers
            float32x4_t pos_vec = vcombine_f32(vdup_n_f32(screen_pos.x), vdup_n_f32(screen_pos.y));
            // Interleave X and Y corners: X[0..3], Y[0..3]
            float32x4_t corners_x = { src_corners[0].x, src_corners[1].x, src_corners[2].x, src_corners[3].x };
            float32x4_t corners_y = { src_corners[0].y, src_corners[1].y, src_corners[2].y, src_corners[3].y };

            float32x4_t out_x = vaddq_f32(corners_x, vdupq_n_f32(screen_pos.x));
            float32x4_t out_y = vaddq_f32(corners_y, vdupq_n_f32(screen_pos.y));

            alignas(16) float res_x[4];
            alignas(16) float res_y[4];
            vst1q_f32(res_x, out_x);
            vst1q_f32(res_y, out_y);

            for (int i = 0; i < 4; ++i) {
                dst_corners[i].x = res_x[i];
                dst_corners[i].y = res_y[i];
            }
            return;
        }

        float rad = glm::radians(rotation);
        float cos_a = std::cos(rad);
        float sin_a = std::sin(rad);

        float32x4_t corners_x = { src_corners[0].x, src_corners[1].x, src_corners[2].x, src_corners[3].x };
        float32x4_t corners_y = { src_corners[0].y, src_corners[1].y, src_corners[2].y, src_corners[3].y };

        float32x4_t v_cos = vdupq_n_f32(cos_a);
        float32x4_t v_sin = vdupq_n_f32(sin_a);
        float32x4_t v_px  = vdupq_n_f32(screen_pos.x);
        float32x4_t v_py  = vdupq_n_f32(screen_pos.y);

        // rx = x * cos - y * sin + pos.x
        float32x4_t rx = vaddq_f32(vsubq_f32(vmulq_f32(corners_x, v_cos), vmulq_f32(corners_y, v_sin)), v_px);
        // ry = x * sin + y * cos + pos.y
        float32x4_t ry = vaddq_f32(vaddq_f32(vmulq_f32(corners_x, v_sin), vmulq_f32(corners_y, v_cos)), v_py);

        alignas(16) float res_x[4];
        alignas(16) float res_y[4];
        vst1q_f32(res_x, rx);
        vst1q_f32(res_y, ry);

        for (int i = 0; i < 4; ++i) {
            dst_corners[i].x = res_x[i];
            dst_corners[i].y = res_y[i];
        }
#else
        // Portable C++20 / x86 Fallback
        if (rotation == 0.0f) {
            for (int i = 0; i < 4; ++i) {
                dst_corners[i] = src_corners[i] + screen_pos;
            }
        } else {
            float rad = glm::radians(rotation);
            float cos_a = std::cos(rad);
            float sin_a = std::sin(rad);

            for (int i = 0; i < 4; ++i) {
                float rx = src_corners[i].x * cos_a - src_corners[i].y * sin_a + screen_pos.x;
                float ry = src_corners[i].x * sin_a + src_corners[i].y * cos_a + screen_pos.y;
                dst_corners[i] = glm::vec2(rx, ry);
            }
        }
#endif
    }
};

} // namespace Engine
