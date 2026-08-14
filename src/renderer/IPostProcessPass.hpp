#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace Engine {

// ---------------------------------------------------------------------------
// IPostProcessPass
// ---------------------------------------------------------------------------
// Pure abstract interface for all screen-space post-processing shader passes.
// The engine core is 100% agnostic to what each pass does: each pass takes
// an input texture from VRAM, performs GPU-accelerated drawing, and writes
// to a destination target.
// ---------------------------------------------------------------------------
class IPostProcessPass {
public:
    virtual ~IPostProcessPass() = default;

    virtual const std::string& get_name() const = 0;
    virtual bool is_enabled() const = 0;
    virtual void set_enabled(bool enabled) = 0;

    // Apply the pass from source texture to destination render target
    virtual void apply(
        SDL_Renderer* renderer, 
        SDL_Texture* source, 
        SDL_Texture* destination, 
        int width, 
        int height
    ) = 0;
};

} // namespace Engine
