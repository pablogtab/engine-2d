#include "SDLGPUMaterialBackend.hpp"
#include "Material.hpp"
#include "../Texture.hpp"
#include "core/Log.hpp"
#include <cstring>

namespace Engine {

namespace {

SDL_GPUShaderFormat to_sdl_format(ShaderBinaryFormat format) {
    switch (format) {
        case ShaderBinaryFormat::SpirV: return SDL_GPU_SHADERFORMAT_SPIRV;
        case ShaderBinaryFormat::Dxil: return SDL_GPU_SHADERFORMAT_DXIL;
        case ShaderBinaryFormat::Msl: return SDL_GPU_SHADERFORMAT_MSL;
        case ShaderBinaryFormat::MetalLibrary: return SDL_GPU_SHADERFORMAT_METALLIB;
    }
    return SDL_GPU_SHADERFORMAT_INVALID;
}

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

} // namespace

SDLGPUMaterialBackend::SDLGPUMaterialBackend(SDL_Renderer* renderer)
    : m_renderer(renderer),
      m_device(renderer ? SDL_GetGPURendererDevice(renderer) : nullptr) {
    if (!m_device) {
        ENGINE_LOG_WARN("[MATERIAL] SDL GPU shaders unavailable for renderer '{}': {}",
                        renderer ? SDL_GetRendererName(renderer) : "none", SDL_GetError());
    } else {
        ENGINE_LOG_INFO("[MATERIAL] GPU shader backend ready: {}", SDL_GetGPUDeviceDriver(m_device));
    }
}

SDLGPUMaterialBackend::~SDLGPUMaterialBackend() {
    destroy_states();
    if (m_default_sampler && m_device) SDL_ReleaseGPUSampler(m_device, m_default_sampler);
}

const char* SDLGPUMaterialBackend::backend_name() const {
    return m_device ? SDL_GetGPUDeviceDriver(m_device) : "unavailable";
}

bool SDLGPUMaterialBackend::supports(
    SDL_Renderer* renderer, const FragmentShader& shader, std::string* error
) {
    SDL_GPUDevice* device = renderer ? SDL_GetGPURendererDevice(renderer) : nullptr;
    if (!device) {
        set_error(error, "The current renderer does not provide an SDL GPU shader backend");
        return false;
    }
    // SDL GPU render states accept the sampler count declared by the fragment
    // shader. The old one-sampler restriction was an engine policy, not a
    // backend limitation, and prevented normal/emissive material variants.
    if (shader.sampler_count() > 8) {
        set_error(error, "This renderer supports at most eight fragment samplers");
        return false;
    }

    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    for (const auto& variant : shader.variants()) {
        const SDL_GPUShaderFormat format = to_sdl_format(variant.format);
        if ((supported & format) != 0 && !variant.code.empty()) return true;
    }
    set_error(error, std::string("No non-empty shader variant is supported by ") + SDL_GetGPUDeviceDriver(device));
    return false;
}

SDLGPUMaterialBackend::CachedState* SDLGPUMaterialBackend::state_for(const Material& material) {
    if (!m_device || !m_renderer) return nullptr;
    const auto& shader = material.shader_ptr();
    if (auto found = m_states.find(shader); found != m_states.end()) {
        CachedState& cached = found->second;
        if (shader->sampler_count() <= 1 ||
            (cached.sampler_material == &material && cached.sampler_revision == material.sampler_revision())) {
            return &cached;
        }
        if (cached.render_state) SDL_DestroyGPURenderState(cached.render_state);
        if (cached.shader) SDL_ReleaseGPUShader(m_device, cached.shader);
        destroy_sampler_textures(cached);
        m_states.erase(found);
    }
    std::string support_error;
    if (!supports(m_renderer, *shader, &support_error)) {
        ENGINE_LOG_ERROR("[MATERIAL] Shader '{}' is unavailable: {}", shader->name(), support_error);
        return nullptr;
    }

    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(m_device);
    const FragmentShaderVariant* selected = nullptr;
    SDL_GPUShaderFormat selected_format = SDL_GPU_SHADERFORMAT_INVALID;
    for (const auto& variant : shader->variants()) {
        const SDL_GPUShaderFormat format = to_sdl_format(variant.format);
        if ((supported & format) != 0 && !variant.code.empty()) {
            selected = &variant;
            selected_format = format;
            break;
        }
    }
    if (!selected) {
        ENGINE_LOG_ERROR("[MATERIAL] Shader '{}' has no variant supported by {}",
                         shader->name(), backend_name());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo shader_info{};
    shader_info.code_size = selected->code.size();
    shader_info.code = selected->code.data();
    shader_info.entrypoint = selected->entry_point.c_str();
    shader_info.format = selected_format;
    shader_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    shader_info.num_samplers = shader->sampler_count();
    shader_info.num_uniform_buffers = shader->uniform_buffer_count();

    CachedState state;
    state.shader = SDL_CreateGPUShader(m_device, &shader_info);
    if (!state.shader) {
        ENGINE_LOG_ERROR("[MATERIAL] Failed to create shader '{}': {}", shader->name(), SDL_GetError());
        return nullptr;
    }

    const std::uint32_t additional_sampler_count = shader->sampler_count() > 0
        ? shader->sampler_count() - 1
        : 0;
    std::vector<SDL_GPUTextureSamplerBinding> sampler_bindings;
    if (additional_sampler_count > 0) {
        if (!m_default_sampler) {
            SDL_GPUSamplerCreateInfo sampler_info{};
            sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
            sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
            sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            m_default_sampler = SDL_CreateGPUSampler(m_device, &sampler_info);
        }
        if (!m_default_sampler) {
            ENGINE_LOG_ERROR("[MATERIAL] Failed to create sampler: {}", SDL_GetError());
            SDL_ReleaseGPUShader(m_device, state.shader);
            return nullptr;
        }
        sampler_bindings.reserve(additional_sampler_count);
        for (std::uint32_t slot = 1; slot <= additional_sampler_count; ++slot) {
            Texture* texture = material.sampler_texture(slot);
            const std::uint32_t fallback_pixel = 0xffffffffu;
            SDL_GPUTexture* gpu_texture = texture ? create_gpu_texture(*texture) : create_gpu_texture(&fallback_pixel, 1, 1);
            if (!gpu_texture) {
                ENGINE_LOG_ERROR("[MATERIAL] GPU texture for sampler slot {} is unavailable", slot);
                SDL_ReleaseGPUShader(m_device, state.shader);
                destroy_sampler_textures(state);
                return nullptr;
            }
            state.sampler_textures.push_back(gpu_texture);
            sampler_bindings.push_back({gpu_texture, m_default_sampler});
        }
    }

    SDL_GPURenderStateCreateInfo state_info{};
    state_info.fragment_shader = state.shader;
    state_info.num_sampler_bindings = static_cast<Sint32>(sampler_bindings.size());
    state_info.sampler_bindings = sampler_bindings.empty() ? nullptr : sampler_bindings.data();
    state.render_state = SDL_CreateGPURenderState(m_renderer, &state_info);
    if (!state.render_state) {
        ENGINE_LOG_ERROR("[MATERIAL] Failed to create render state '{}': {}", shader->name(), SDL_GetError());
        SDL_ReleaseGPUShader(m_device, state.shader);
        destroy_sampler_textures(state);
        return nullptr;
    }

    ENGINE_LOG_INFO(
        "[MATERIAL] Loaded shader '{}' for backend '{}'",
        shader->name(),
        backend_name()
    );

    state.sampler_material = &material;
    state.sampler_revision = material.sampler_revision();
    auto [inserted, added] = m_states.emplace(shader, state);
    (void)added;
    return &inserted->second;
}

bool SDLGPUMaterialBackend::bind(const Material& material, const MaterialRenderContext& context) {
    CachedState* state = state_for(material);
    if (!state) return false;

    const std::uint32_t uniform_count = material.shader().uniform_buffer_count();
    const auto engine_slot = material.shader().engine_uniform_slot();
    const EngineShaderGlobals engine_globals = context.engine_globals();
    for (std::uint32_t slot = 0; slot < uniform_count; ++slot) {
        const auto material_bytes = material.uniform_bytes(slot);
        const bool uses_engine_globals = engine_slot && *engine_slot == slot;
        const void* bytes = uses_engine_globals
            ? static_cast<const void*>(&engine_globals)
            : static_cast<const void*>(material_bytes.data());
        const std::size_t byte_count = uses_engine_globals
            ? sizeof(engine_globals)
            : material_bytes.size();
        if (byte_count == 0) {
            ENGINE_LOG_ERROR("[MATERIAL] Material '{}' is missing uniform slot {}", material.name(), slot);
            return false;
        }
        if (!SDL_SetGPURenderStateFragmentUniforms(
                state->render_state,
                slot,
                bytes,
                static_cast<Uint32>(byte_count))) {
            ENGINE_LOG_ERROR("[MATERIAL] Failed to upload '{}' uniform slot {}: {}",
                             material.name(), slot, SDL_GetError());
            return false;
        }
    }

    if (!SDL_SetGPURenderState(m_renderer, state->render_state)) {
        ENGINE_LOG_ERROR("[MATERIAL] Failed to bind '{}': {}", material.name(), SDL_GetError());
        return false;
    }
    return true;
}

void SDLGPUMaterialBackend::unbind() {
    if (m_renderer && m_device && !SDL_SetGPURenderState(m_renderer, nullptr)) {
        ENGINE_LOG_ERROR("[MATERIAL] Failed to clear GPU render state: {}", SDL_GetError());
    }
}

void SDLGPUMaterialBackend::discard_unused() {
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (it->first.use_count() != 1) { ++it; continue; }
        if (it->second.render_state) SDL_DestroyGPURenderState(it->second.render_state);
        if (it->second.shader && m_device) SDL_ReleaseGPUShader(m_device, it->second.shader);
        destroy_sampler_textures(it->second);
        it = m_states.erase(it);
    }
}

void SDLGPUMaterialBackend::invalidate_device_resources() {
    destroy_states();
    if (m_default_sampler && m_device) SDL_ReleaseGPUSampler(m_device, m_default_sampler);
    m_default_sampler = nullptr;
    m_device = nullptr;
}

void SDLGPUMaterialBackend::restore_device_resources() {
    m_device = m_renderer ? SDL_GetGPURendererDevice(m_renderer) : nullptr;
    if (!m_device) {
        ENGINE_LOG_WARN("[MATERIAL] GPU shader backend unavailable after surface restore: {}", SDL_GetError());
    }
}

bool SDLGPUMaterialBackend::draw_fullscreen(
    const Material& material,
    SDL_Texture* source,
    const MaterialRenderContext& context
) {
    if (!source || !bind(material, context)) return false;
    const bool rendered = SDL_RenderTexture(m_renderer, source, nullptr, nullptr);
    unbind();
    if (!rendered) {
        ENGINE_LOG_ERROR("[MATERIAL] Fullscreen draw failed for '{}': {}", material.name(), SDL_GetError());
    }
    return rendered;
}

void SDLGPUMaterialBackend::destroy_states() {
    if (m_renderer && m_device) SDL_SetGPURenderState(m_renderer, nullptr);
    for (auto& [shader_asset, state] : m_states) {
        (void)shader_asset;
        if (state.render_state) SDL_DestroyGPURenderState(state.render_state);
        if (state.shader && m_device) SDL_ReleaseGPUShader(m_device, state.shader);
        destroy_sampler_textures(state);
    }
    m_states.clear();
}

void SDLGPUMaterialBackend::destroy_sampler_textures(CachedState& state) {
    for (SDL_GPUTexture* texture : state.sampler_textures) if (texture && m_device) SDL_ReleaseGPUTexture(m_device, texture);
    state.sampler_textures.clear();
}

SDL_GPUTexture* SDLGPUMaterialBackend::create_gpu_texture(const Texture& texture) {
    const auto pixels = texture.pixels();
    return pixels.empty() ? nullptr : create_gpu_texture(pixels.data(), texture.get_width(), texture.get_height());
}

SDL_GPUTexture* SDLGPUMaterialBackend::create_gpu_texture(const std::uint32_t* pixels, int width, int height) {
    if (!m_device || !pixels || width <= 0 || height <= 0) return nullptr;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> rgba(pixel_count * 4);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        rgba[i * 4] = static_cast<std::uint8_t>(pixels[i] >> 24);
        rgba[i * 4 + 1] = static_cast<std::uint8_t>(pixels[i] >> 16);
        rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixels[i] >> 8);
        rgba[i * 4 + 3] = static_cast<std::uint8_t>(pixels[i]);
    }
    SDL_GPUTextureCreateInfo texture_info{};
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = static_cast<Uint32>(width); texture_info.height = static_cast<Uint32>(height);
    texture_info.layer_count_or_depth = 1; texture_info.num_levels = 1; texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* result = SDL_CreateGPUTexture(m_device, &texture_info);
    if (!result) return nullptr;
    SDL_GPUTransferBufferCreateInfo transfer_info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(rgba.size()), 0};
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_device, &transfer_info);
    if (!transfer) { SDL_ReleaseGPUTexture(m_device, result); return nullptr; }
    void* mapped = SDL_MapGPUTransferBuffer(m_device, transfer, false);
    if (!mapped) { SDL_ReleaseGPUTransferBuffer(m_device, transfer); SDL_ReleaseGPUTexture(m_device, result); return nullptr; }
    std::memcpy(mapped, rgba.data(), rgba.size()); SDL_UnmapGPUTransferBuffer(m_device, transfer);
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(m_device);
    if (!command) { SDL_ReleaseGPUTransferBuffer(m_device, transfer); SDL_ReleaseGPUTexture(m_device, result); return nullptr; }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    const SDL_GPUTextureTransferInfo source{transfer, 0, static_cast<Uint32>(width), static_cast<Uint32>(height)};
    const SDL_GPUTextureRegion destination{result, 0, 0, 0, static_cast<Uint32>(width), static_cast<Uint32>(height), 1};
    SDL_UploadToGPUTexture(copy, &source, &destination, false); SDL_EndGPUCopyPass(copy);
    if (!SDL_SubmitGPUCommandBuffer(command) || !SDL_WaitForGPUIdle(m_device)) {
        SDL_ReleaseGPUTransferBuffer(m_device, transfer); SDL_ReleaseGPUTexture(m_device, result); return nullptr;
    }
    SDL_ReleaseGPUTransferBuffer(m_device, transfer);
    return result;
}

} // namespace Engine
