#pragma once

#include "IMaterialBackend.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

class FragmentShader;
class Texture;

class SDLGPUMaterialBackend final : public IMaterialBackend {
public:
    explicit SDLGPUMaterialBackend(SDL_Renderer* renderer);
    ~SDLGPUMaterialBackend() override;

    SDLGPUMaterialBackend(const SDLGPUMaterialBackend&) = delete;
    SDLGPUMaterialBackend& operator=(const SDLGPUMaterialBackend&) = delete;

    [[nodiscard]] bool is_available() const override { return m_device != nullptr; }
    [[nodiscard]] const char* backend_name() const override;
    [[nodiscard]] static bool supports(
        SDL_Renderer* renderer, const FragmentShader& shader, std::string* error = nullptr
    );
    bool bind(const Material& material, const MaterialRenderContext& context) override;
    void unbind() override;
    bool draw_fullscreen(
        const Material& material,
        SDL_Texture* source,
        const MaterialRenderContext& context
    ) override;
    void discard_unused() override;
    void invalidate_device_resources() override;
    void restore_device_resources() override;

private:
    struct CachedState {
        SDL_GPUShader* shader{nullptr};
        SDL_GPURenderState* render_state{nullptr};
        std::uint64_t sampler_revision{0};
        const Material* sampler_material{nullptr};
        std::vector<SDL_GPUTexture*> sampler_textures;
    };

    CachedState* state_for(const Material& material);
    void destroy_states();
    void destroy_sampler_textures(CachedState& state);
    [[nodiscard]] SDL_GPUTexture* create_gpu_texture(const Texture& texture);
    [[nodiscard]] SDL_GPUTexture* create_gpu_texture(const std::uint32_t* pixels, int width, int height);

    SDL_Renderer* m_renderer{nullptr};
    SDL_GPUDevice* m_device{nullptr};
    std::unordered_map<std::shared_ptr<const FragmentShader>, CachedState> m_states;
    SDL_GPUSampler* m_default_sampler{nullptr};
};

} // namespace Engine
