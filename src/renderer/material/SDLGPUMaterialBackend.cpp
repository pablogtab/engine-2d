#include "SDLGPUMaterialBackend.hpp"
#include "Material.hpp"
#include "core/Log.hpp"

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
}

const char* SDLGPUMaterialBackend::backend_name() const {
    return m_device ? SDL_GetGPUDeviceDriver(m_device) : "unavailable";
}

SDLGPUMaterialBackend::CachedState* SDLGPUMaterialBackend::state_for(
    const std::shared_ptr<const FragmentShader>& shader
) {
    if (!m_device || !m_renderer) return nullptr;
    if (auto found = m_states.find(shader); found != m_states.end()) return &found->second;
    if (shader->sampler_count() != 1) {
        ENGINE_LOG_ERROR(
            "[MATERIAL] Shader '{}' requests {} samplers; the first material slice supports the sprite source sampler only",
            shader->name(), shader->sampler_count());
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

    SDL_GPURenderStateCreateInfo state_info{};
    state_info.fragment_shader = state.shader;
    state.render_state = SDL_CreateGPURenderState(m_renderer, &state_info);
    if (!state.render_state) {
        ENGINE_LOG_ERROR("[MATERIAL] Failed to create render state '{}': {}", shader->name(), SDL_GetError());
        SDL_ReleaseGPUShader(m_device, state.shader);
        return nullptr;
    }

    ENGINE_LOG_INFO(
        "[MATERIAL] Loaded shader '{}' for backend '{}'",
        shader->name(),
        backend_name()
    );

    return &m_states.emplace(shader, state).first->second;
}

bool SDLGPUMaterialBackend::bind(const Material& material, const MaterialRenderContext&) {
    CachedState* state = state_for(material.shader_ptr());
    if (!state) return false;

    const std::uint32_t uniform_count = material.shader().uniform_buffer_count();
    for (std::uint32_t slot = 0; slot < uniform_count; ++slot) {
        const auto bytes = material.uniform_bytes(slot);
        if (bytes.empty()) {
            ENGINE_LOG_ERROR("[MATERIAL] Material '{}' is missing uniform slot {}", material.name(), slot);
            return false;
        }
        if (!SDL_SetGPURenderStateFragmentUniforms(
                state->render_state,
                slot,
                bytes.data(),
                static_cast<Uint32>(bytes.size()))) {
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
    }
    m_states.clear();
}

} // namespace Engine
