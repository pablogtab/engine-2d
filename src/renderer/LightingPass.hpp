#pragma once

#include "RenderLayer.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>

namespace Engine {

class Camera2D;
class RenderTargetPool;

struct LightingContext {
    SDL_Renderer* renderer{nullptr};
    SDL_Texture* scene_target{nullptr};
    RenderTargetPool* render_targets{nullptr};
    const Camera2D* camera{nullptr};
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};
    int width{0};
    int height{0};
    // Converts native camera screen coordinates into this pass' raster.
    glm::vec2 raster_scale{1.0f, 1.0f};
    // Layers already processed by this same pass during the current frame.
    // A light may contribute to uncovered pixels on its first matching layer;
    // subsequent matching layers must keep the contribution inside geometry.
    RenderLayerMask previously_composited_layers{0};
    // Alpha coverage of sprites that explicitly receive projected shadows.
    SDL_Texture* shadow_receiver_mask{nullptr};
};

// Lighting passes compose directly onto the captured scene before global
// post-processing. They own neither the scene target nor its lifetime.
class ILightingPass {
public:
    virtual ~ILightingPass() = default;
    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual bool is_enabled() const = 0;
    [[nodiscard]] virtual RenderLayerMask affected_layers() const = 0;
    [[nodiscard]] virtual bool requires_shadow_receiver_mask() const { return false; }
    virtual bool apply(const LightingContext& context) = 0;
    virtual void invalidate_resources() {}
};

} // namespace Engine
