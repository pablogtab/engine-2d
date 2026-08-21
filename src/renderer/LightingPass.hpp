#pragma once

#include <SDL3/SDL.h>

namespace Engine {

struct LightingContext {
    SDL_Renderer* renderer{nullptr};
    SDL_Texture* scene_target{nullptr};
    int width{0};
    int height{0};
};

// Lighting passes compose directly onto the captured scene before global
// post-processing. They own neither the scene target nor its lifetime.
class ILightingPass {
public:
    virtual ~ILightingPass() = default;
    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual bool is_enabled() const = 0;
    virtual bool apply(const LightingContext& context) = 0;
};

} // namespace Engine
