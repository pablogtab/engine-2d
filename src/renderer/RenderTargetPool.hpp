#pragma once

#include <SDL3/SDL.h>
#include <cstddef>
#include <vector>

namespace Engine {

struct RenderTargetPoolMetrics {
    std::size_t allocated_targets{0};
    std::size_t leased_targets{0};
    std::size_t allocated_bytes{0};
};

class RenderTargetPool {
public:
    explicit RenderTargetPool(SDL_Renderer* renderer = nullptr) : m_renderer(renderer) {}
    ~RenderTargetPool();
    RenderTargetPool(const RenderTargetPool&) = delete;
    RenderTargetPool& operator=(const RenderTargetPool&) = delete;

    void set_renderer(SDL_Renderer* renderer);
    [[nodiscard]] SDL_Texture* acquire(int width, int height);
    void release(SDL_Texture* target);
    void clear();
    [[nodiscard]] const RenderTargetPoolMetrics& metrics() const { return m_metrics; }

private:
    struct Entry { SDL_Texture* texture{nullptr}; int width{0}; int height{0}; bool leased{false}; };
    SDL_Renderer* m_renderer{nullptr};
    std::vector<Entry> m_entries;
    RenderTargetPoolMetrics m_metrics{};
};

} // namespace Engine
