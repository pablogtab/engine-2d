#pragma once

#include "IPostProcessPass.hpp"
#include "../material/IMaterialBackend.hpp"
#include "../material/Material.hpp"
#include <memory>
#include <string>

namespace Engine {

// Adapts a regular sprite material to the fullscreen post-process chain. The
// shader and uniform contract are identical in both scopes; only the geometry
// submitted by the renderer changes.
class MaterialPostProcessPass final : public IPostProcessPass {
public:
    MaterialPostProcessPass(
        std::shared_ptr<const Material> material,
        std::shared_ptr<IMaterialBackend> backend
    ) : m_material(std::move(material)), m_backend(std::move(backend)) {}

    const std::string& get_name() const override { return m_material->name(); }
    bool is_enabled() const override { return m_enabled; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void apply(
        SDL_Renderer* renderer,
        SDL_Texture* source,
        SDL_Texture*,
        const PostProcessContext& context
    ) override {
        const MaterialRenderContext material_context{context.output_width, context.output_height};
        if (!m_backend || !m_backend->draw_fullscreen(*m_material, source, material_context)) {
            // Unsupported renderers keep the scene visible instead of turning
            // a missing optional GPU feature into a black frame.
            SDL_RenderTexture(renderer, source, nullptr, nullptr);
        }
    }

private:
    std::shared_ptr<const Material> m_material;
    std::shared_ptr<IMaterialBackend> m_backend;
    bool m_enabled{true};
};

} // namespace Engine
