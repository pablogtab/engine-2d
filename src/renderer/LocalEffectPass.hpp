#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace Engine {

struct LocalEffectContext {
    SDL_Renderer* renderer{nullptr};
    SDL_Texture* source{nullptr};
    SDL_Texture* destination{nullptr};
    int width{0};
    int height{0};
};

// A local effect owns no render target. Renderer2D supplies pooled surfaces so
// effects remain reusable for entities, layers, or future lighting passes.
class ILocalEffectPass {
public:
    virtual ~ILocalEffectPass() = default;
    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual bool is_enabled() const = 0;
    virtual bool apply(const LocalEffectContext& context) = 0;
};

} // namespace Engine
