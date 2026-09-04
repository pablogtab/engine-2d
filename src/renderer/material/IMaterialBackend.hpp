#pragma once

#include "EngineShaderGlobals.hpp"
#include "../../core/Time.hpp"
#include <algorithm>
#include <memory>

struct SDL_Renderer;
struct SDL_Texture;

namespace Engine {

class Material;

struct MaterialRenderContext {
    int output_width{1};
    int output_height{1};
    float game_time_seconds{0.0f};
    float real_time_seconds{0.0f};
    float delta_time_seconds{0.0f};
    float frame_index{0.0f};

    [[nodiscard]] static MaterialRenderContext current(int width, int height) {
        MaterialRenderContext context;
        context.output_width = std::max(width, 1);
        context.output_height = std::max(height, 1);
        context.game_time_seconds = Time::get_time();
        context.real_time_seconds = Time::get_real_time();
        context.delta_time_seconds = Time::get_delta_time();
        context.frame_index = static_cast<float>(Time::get_frame_count());
        return context;
    }

    [[nodiscard]] EngineShaderGlobals engine_globals() const {
        const float width = static_cast<float>(std::max(output_width, 1));
        const float height = static_cast<float>(std::max(output_height, 1));
        return {
            game_time_seconds,
            real_time_seconds,
            delta_time_seconds,
            frame_index,
            width,
            height,
            1.0f / width,
            1.0f / height
        };
    }
};

class IMaterialBackend {
public:
    virtual ~IMaterialBackend() = default;

    [[nodiscard]] virtual bool is_available() const = 0;
    [[nodiscard]] virtual const char* backend_name() const = 0;

    // Applies a material to subsequent SDL_Renderer draw commands.
    virtual bool bind(const Material& material, const MaterialRenderContext& context) = 0;
    virtual void unbind() = 0;

    // Uses exactly the same material contract as sprite rendering, but draws
    // a source render target over the current fullscreen destination.
    virtual bool draw_fullscreen(
        const Material& material,
        SDL_Texture* source,
        const MaterialRenderContext& context
    ) = 0;

    // Releases cached states whose material instances are no longer referenced
    // by scenes after an asset reload.
    virtual void discard_unused() {}
    // Surface loss invalidates backend-owned GPU state without changing the
    // authored Material instances that reference shader assets.
    virtual void invalidate_device_resources() {}
    virtual void restore_device_resources() {}
};

} // namespace Engine
