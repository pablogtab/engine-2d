#include "PostProcessPipeline.hpp"
#include "../../core/Log.hpp"
#include <algorithm>

namespace Engine {

PostProcessPipeline::~PostProcessPipeline() {
    destroy_targets();
}

void PostProcessPipeline::init(SDL_Renderer* renderer, int width, int height) {
    m_renderer = renderer;
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    create_targets();
}

void PostProcessPipeline::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_width == width && m_height == height && m_target_a != nullptr) return;

    m_width = width;
    m_height = height;
    create_targets();
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

    ENGINE_LOG_INFO("[POST-PROCESS] Created Framebuffers (Full: {}x{}, Half: {}x{}, Quarter: {}x{})",
                    m_width, m_height, half_w, half_h, quarter_w, quarter_h);
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

void PostProcessPipeline::begin_capture() {
    if (!m_renderer) return;

    if (!m_target_a) {
        create_targets();
    }
    if (!m_target_a) return;

    m_is_capturing = true;
    SDL_SetRenderTarget(m_renderer, m_target_a);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);
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
        SDL_SetRenderTarget(m_renderer, final_destination);
        SDL_RenderTexture(m_renderer, m_target_a, nullptr, nullptr);
        return;
    }

    // Ping-Pong execution across active passes strictly within VRAM texture coordinates
    SDL_Texture* current_src = m_target_a;
    SDL_Texture* current_dst = m_target_b;

    for (size_t i = 0; i < active_passes.size(); ++i) {
        SDL_SetRenderTarget(m_renderer, current_dst);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
        SDL_RenderClear(m_renderer);

        active_passes[i]->apply(m_renderer, current_src, current_dst, m_width, m_height);

        // Swap ping-pong pointers for next pass
        std::swap(current_src, current_dst);
    }

    // Blit final processed texture to destination (window framebuffer)
    SDL_SetRenderTarget(m_renderer, final_destination);
    SDL_RenderTexture(m_renderer, current_src, nullptr, nullptr);
}

} // namespace Engine
