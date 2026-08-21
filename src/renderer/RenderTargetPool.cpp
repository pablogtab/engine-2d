#include "RenderTargetPool.hpp"
#include <algorithm>

namespace Engine {

RenderTargetPool::~RenderTargetPool() { clear(); }

void RenderTargetPool::set_renderer(SDL_Renderer* renderer) {
    if (m_renderer == renderer) return;
    clear();
    m_renderer = renderer;
}

SDL_Texture* RenderTargetPool::acquire(int width, int height) {
    if (!m_renderer || width <= 0 || height <= 0) return nullptr;
    for (auto& entry : m_entries) {
        if (!entry.leased && entry.width == width && entry.height == height) {
            entry.leased = true;
            ++m_metrics.leased_targets;
            return entry.texture;
        }
    }
    SDL_Texture* texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    if (!texture) return nullptr;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    m_entries.push_back({texture, width, height, true});
    ++m_metrics.allocated_targets;
    ++m_metrics.leased_targets;
    m_metrics.allocated_bytes += static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    return texture;
}

void RenderTargetPool::release(SDL_Texture* target) {
    const auto found = std::find_if(m_entries.begin(), m_entries.end(), [target](const Entry& entry) { return entry.texture == target; });
    if (found == m_entries.end() || !found->leased) return;
    found->leased = false;
    --m_metrics.leased_targets;
}

void RenderTargetPool::clear() {
    for (auto& entry : m_entries) if (entry.texture) SDL_DestroyTexture(entry.texture);
    m_entries.clear();
    m_metrics = {};
}

} // namespace Engine
