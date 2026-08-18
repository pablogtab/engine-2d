#include "PostProcessPipeline.hpp"
#include "../../core/Log.hpp"
#include <algorithm>

namespace Engine {

namespace {

void log_render_target_pixel(
    SDL_Renderer* renderer,
    SDL_Texture* target,
    int width,
    int height,
    const std::string& label,
    const char* stage
) {
    if (!SDL_SetRenderTarget(renderer, target)) {
        ENGINE_LOG_ERROR("[POST-PROCESS PROBE] '{}' {} target bind failed: {}", label, stage, SDL_GetError());
        return;
    }

    int sample_width = width;
    int sample_height = height;
    if (!target && !SDL_GetCurrentRenderOutputSize(renderer, &sample_width, &sample_height)) {
        ENGINE_LOG_ERROR("[POST-PROCESS PROBE] '{}' {} output-size query failed: {}", label, stage, SDL_GetError());
        return;
    }
    const SDL_Rect sample_rect{
        std::max(0, sample_width / 2),
        std::max(0, sample_height / 2),
        1,
        1
    };
    SDL_Surface* sample = SDL_RenderReadPixels(renderer, &sample_rect);
    if (!sample) {
        ENGINE_LOG_ERROR("[POST-PROCESS PROBE] '{}' {} readback failed: {}", label, stage, SDL_GetError());
        return;
    }

    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    Uint8 alpha = 0;
    if (SDL_ReadSurfacePixel(sample, 0, 0, &red, &green, &blue, &alpha)) {
        ENGINE_LOG_INFO(
            "[POST-PROCESS PROBE] '{}' {} center RGBA: ({}, {}, {}, {})",
            label,
            stage,
            static_cast<int>(red),
            static_cast<int>(green),
            static_cast<int>(blue),
            static_cast<int>(alpha)
        );
    } else {
        ENGINE_LOG_ERROR("[POST-PROCESS PROBE] '{}' {} pixel decode failed: {}", label, stage, SDL_GetError());
    }
    SDL_DestroySurface(sample);
}

} // namespace

PostProcessPipeline::~PostProcessPipeline() {
    destroy_targets();
}

void PostProcessPipeline::init(
    SDL_Renderer* renderer,
    int width,
    int height,
    int authoring_width,
    int authoring_height
) {
    m_renderer = renderer;
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    m_context = {m_width, m_height, std::max(1, authoring_width), std::max(1, authoring_height)};
    create_targets();
}

void PostProcessPipeline::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_width == width && m_height == height && m_target_a != nullptr) return;

    m_width = width;
    m_height = height;
    m_context.output_width = width;
    m_context.output_height = height;
    create_targets();
}

void PostProcessPipeline::set_effect_reference_size(int width, int height) {
    m_context.authoring_width = std::max(1, width);
    m_context.authoring_height = std::max(1, height);
}

void PostProcessPipeline::destroy_targets() {
    auto destroy_tex = [](SDL_Texture*& tex) {
        if (tex) {
            SDL_DestroyTexture(tex);
            tex = nullptr;
        }
    };

    destroy_tex(m_target_a);
    destroy_tex(m_target_b);
    destroy_tex(m_half_target_a);
    destroy_tex(m_half_target_b);
    destroy_tex(m_quarter_target);
}

void PostProcessPipeline::create_targets() {
    destroy_targets();
    if (!m_renderer) return;

    auto create_rt = [this](int w, int h) -> SDL_Texture* {
        SDL_Texture* tex = SDL_CreateTexture(
            m_renderer, 
            SDL_PIXELFORMAT_RGBA8888, 
            SDL_TEXTUREACCESS_TARGET, 
            std::max(1, w), 
            std::max(1, h)
        );
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        }
        return tex;
    };

    // Full resolution Ping-Pong pair
    m_target_a = create_rt(m_width, m_height);
    m_target_b = create_rt(m_width, m_height);

    // Downscaled auxiliary buffers (1/2 and 1/4 size for Bloom & Gaussian Blur)
    int half_w = std::max(1, m_width / 2);
    int half_h = std::max(1, m_height / 2);
    m_half_target_a = create_rt(half_w, half_h);
    m_half_target_b = create_rt(half_w, half_h);

    int quarter_w = std::max(1, m_width / 4);
    int quarter_h = std::max(1, m_height / 4);
    m_quarter_target = create_rt(quarter_w, quarter_h);

    ENGINE_LOG_INFO(
        "[POST-PROCESS] Created framebuffers: output {}x{}, effect reference {}x{}, scale {:.2f}x",
        m_width,
        m_height,
        m_context.authoring_width,
        m_context.authoring_height,
        m_context.pixel_scale()
    );
}

void PostProcessPipeline::update(float delta_time) {
    for (auto& pass : m_passes) {
        if (pass) {
            pass->update(delta_time);
        }
    }
}

void PostProcessPipeline::add_pass(std::shared_ptr<IPostProcessPass> pass) {
    if (!pass) return;
    remove_pass(pass->get_name());
    m_passes.push_back(pass);
    ENGINE_LOG_INFO("[POST-PROCESS] Registered pass: '{}'", pass->get_name());
}

void PostProcessPipeline::remove_pass(const std::string& name) {
    auto it = std::remove_if(m_passes.begin(), m_passes.end(),
        [&name](const std::shared_ptr<IPostProcessPass>& p) {
            return p && p->get_name() == name;
        });
    if (it != m_passes.end()) {
        m_passes.erase(it, m_passes.end());
    }
}

void PostProcessPipeline::set_pass_enabled(const std::string& name, bool enabled) {
    for (auto& pass : m_passes) {
        if (pass && pass->get_name() == name) {
            pass->set_enabled(enabled);
            return;
        }
    }
}

bool PostProcessPipeline::is_pass_enabled(const std::string& name) const {
    for (const auto& pass : m_passes) {
        if (pass && pass->get_name() == name) {
            return pass->is_enabled();
        }
    }
    return false;
}

void PostProcessPipeline::clear_passes() {
    m_passes.clear();
}

size_t PostProcessPipeline::get_active_pass_count() const {
    size_t count = 0;
    for (const auto& pass : m_passes) {
        if (pass && pass->is_enabled()) {
            count++;
        }
    }
    return count;
}

void PostProcessPipeline::begin_capture(SDL_Texture* backdrop, SDL_Color clear_color) {
    if (!m_renderer) return;

    if (!m_target_a) {
        create_targets();
    }
    if (!m_target_a) return;

    m_is_capturing = true;
    if (!SDL_SetRenderTarget(m_renderer, m_target_a)) {
        ENGINE_LOG_ERROR("[POST-PROCESS] Failed to bind capture target: {}", SDL_GetError());
        m_is_capturing = false;
        return;
    }
    SDL_SetRenderDrawColor(
        m_renderer,
        backdrop ? 0 : clear_color.r,
        backdrop ? 0 : clear_color.g,
        backdrop ? 0 : clear_color.b,
        backdrop ? 0 : clear_color.a
    );
    SDL_RenderClear(m_renderer);

    if (backdrop && backdrop != m_target_a) {
        SDL_SetTextureColorMod(backdrop, 255, 255, 255);
        SDL_SetTextureAlphaMod(backdrop, 255);
        if (!SDL_RenderTexture(m_renderer, backdrop, nullptr, nullptr)) {
            ENGINE_LOG_ERROR("[POST-PROCESS] Failed to seed capture from camera backdrop: {}", SDL_GetError());
        }
    }
}

void PostProcessPipeline::end_capture_and_process(SDL_Texture* final_destination) {
    if (!m_is_capturing || !m_renderer || !m_target_a) return;
    m_is_capturing = false;

    // Collect active passes
    std::vector<IPostProcessPass*> active_passes;
    active_passes.reserve(m_passes.size());
    for (const auto& pass : m_passes) {
        if (pass && pass->is_enabled()) {
            active_passes.push_back(pass.get());
        }
    }

    if (active_passes.empty()) {
        // Direct blit to destination without post-processing
        if (!SDL_SetRenderTarget(m_renderer, final_destination)) {
            ENGINE_LOG_ERROR("[POST-PROCESS] Failed to bind final destination: {}", SDL_GetError());
            return;
        }
        if (!SDL_RenderTexture(m_renderer, m_target_a, nullptr, nullptr)) {
            ENGINE_LOG_ERROR("[POST-PROCESS] Failed to present unprocessed capture: {}", SDL_GetError());
        }
        if (!m_pending_probe_label.empty()) {
            log_render_target_pixel(m_renderer, final_destination, m_width, m_height, m_pending_probe_label, "presented");
            m_pending_probe_label.clear();
        }
        return;
    }

    if (!m_pending_probe_label.empty()) {
        log_render_target_pixel(m_renderer, m_target_a, m_width, m_height, m_pending_probe_label, "captured");
    }

    // Ping-Pong execution across active passes strictly within VRAM texture coordinates
    SDL_Texture* current_src = m_target_a;
    SDL_Texture* current_dst = m_target_b;

    for (size_t i = 0; i < active_passes.size(); ++i) {
        if (!SDL_SetRenderTarget(m_renderer, current_dst)) {
            ENGINE_LOG_ERROR("[POST-PROCESS] Failed to bind pass target for '{}': {}", active_passes[i]->get_name(), SDL_GetError());
            return;
        }
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
        SDL_RenderClear(m_renderer);

        active_passes[i]->apply(m_renderer, current_src, current_dst, m_context);

        // Swap ping-pong pointers for next pass
        std::swap(current_src, current_dst);
    }

    if (!m_pending_probe_label.empty()) {
        log_render_target_pixel(m_renderer, current_src, m_width, m_height, m_pending_probe_label, "processed");
    }

    // Blit final processed texture to destination (window framebuffer)
    if (!SDL_SetRenderTarget(m_renderer, final_destination)) {
        ENGINE_LOG_ERROR("[POST-PROCESS] Failed to bind processed-frame destination: {}", SDL_GetError());
        return;
    }
    if (!SDL_RenderTexture(m_renderer, current_src, nullptr, nullptr)) {
        ENGINE_LOG_ERROR("[POST-PROCESS] Failed to present processed frame: {}", SDL_GetError());
    }
    if (!m_pending_probe_label.empty()) {
        log_render_target_pixel(m_renderer, final_destination, m_width, m_height, m_pending_probe_label, "presented");
        m_pending_probe_label.clear();
    }
}

} // namespace Engine
