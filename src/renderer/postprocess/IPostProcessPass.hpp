#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace Engine {

struct PostProcessContext {
    int output_width{1};
    int output_height{1};
    int authoring_width{1280};
    int authoring_height{720};

    [[nodiscard]] float horizontal_scale() const {
        return static_cast<float>(std::max(output_width, 1)) /
               static_cast<float>(std::max(authoring_width, 1));
    }

    [[nodiscard]] float vertical_scale() const {
        return static_cast<float>(std::max(output_height, 1)) /
               static_cast<float>(std::max(authoring_height, 1));
    }

    [[nodiscard]] float pixel_scale() const {
        return std::sqrt(horizontal_scale() * vertical_scale());
    }

    [[nodiscard]] float scale_pixels(float pixels) const {
        return pixels * pixel_scale();
    }
};

// ---------------------------------------------------------------------------
// IPostProcessPass
// ---------------------------------------------------------------------------
// Base interface for screen-space post-processing passes. Supports lifecycle,
// temporal updates for animation, and editor inspection hooks.
// ---------------------------------------------------------------------------
class IPostProcessPass {
public:
    virtual ~IPostProcessPass() = default;

    virtual const std::string& get_name() const = 0;
    virtual bool is_enabled() const = 0;
    virtual void set_enabled(bool enabled) = 0;

    // Update animations, decays, and procedural noise.
    virtual void update(float /*delta_time*/) {}

    // Apply this pass from the source texture into the current render target.
    virtual void apply(
        SDL_Renderer* renderer, 
        SDL_Texture* source, 
        SDL_Texture* destination, 
        const PostProcessContext& context
    ) = 0;

    // Optional editor inspection hook.
    virtual void on_gui() {}

    // Restore default runtime state.
    virtual void reset() {}
};

} // namespace Engine
