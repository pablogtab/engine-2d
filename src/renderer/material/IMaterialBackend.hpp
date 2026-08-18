#pragma once

#include <memory>

struct SDL_Renderer;
struct SDL_Texture;

namespace Engine {

class Material;

struct MaterialRenderContext {
    int output_width{1};
    int output_height{1};
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
};

} // namespace Engine
