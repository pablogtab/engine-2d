#include "PostProcessPipeline.hpp"
#include "../core/Log.hpp"
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
    if (m_target_a) {
        SDL_DestroyTexture(m_target_a);
        m_target_a = nullptr;
    }
    if (m_target_b) {
        SDL_DestroyTexture(m_target_b);
        m_target_b = nullptr;
    }
}

void PostProcessPipeline::create_targets() {
    destroy_targets();
    if (!m_renderer) return;

    m_target_a = SDL_CreateTexture(
        m_renderer, 
        SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_TARGET, 
        m_width, 
        m_height
    );
    m_target_b = SDL_CreateTexture(
        m_renderer, 
        SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_TARGET, 
        m_width, 
        m_height
    );

    if (m_target_a) {
        SDL_SetTextureBlendMode(m_target_a, SDL_BLENDMODE_BLEND);
    }
    if (m_target_b) {
        SDL_SetTextureBlendMode(m_target_b, SDL_BLENDMODE_BLEND);
    }

    ENGINE_LOG_INFO("[POST-PROCESS] Framebuffer Render Targets created ({}x{} px)", m_width, m_height);
}

void PostProcessPipeline::add_pass(std::shared_ptr<IPostProcessPass> pass) {
    if (!pass) return;
    // Prevent duplicate pass names
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

    // Auto-detect Retina / High-DPI framebuffer resolution
    int out_w = 0, out_h = 0;
    SDL_GetCurrentRenderOutputSize(m_renderer, &out_w, &out_h);
    if (out_w > 0 && out_h > 0 && (out_w != m_width || out_h != m_height || !m_target_a)) {
        resize(out_w, out_h);
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

    // Ping-Pong execution across active passes
    SDL_Texture* current_src = m_target_a;
    SDL_Texture* current_dst = m_target_b;

    for (size_t i = 0; i < active_passes.size(); ++i) {
        bool is_last_pass = (i == active_passes.size() - 1);
        SDL_Texture* pass_output = is_last_pass ? final_destination : current_dst;

        SDL_SetRenderTarget(m_renderer, pass_output);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
        SDL_RenderClear(m_renderer);

        active_passes[i]->apply(m_renderer, current_src, pass_output, m_width, m_height);

        // Swap ping-pong pointers for next pass
        std::swap(current_src, current_dst);
    }

    // Restore final target (default is window backbuffer nullptr)
    SDL_SetRenderTarget(m_renderer, final_destination);
}

} // namespace Engine
