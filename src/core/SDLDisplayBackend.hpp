#pragma once

#include "DisplayBackend.hpp"

struct SDL_Window;
struct SDL_Renderer;

namespace Engine {

class SDLDisplayBackend final : public IDisplayBackend {
public:
    SDLDisplayBackend(SDL_Window* window, SDL_Renderer* renderer);

    [[nodiscard]] DisplayCapabilities capabilities() const override;
    [[nodiscard]] std::vector<DisplayInfo> available_displays() const override;
    [[nodiscard]] std::vector<DisplayMode> available_modes(DisplayId display_id) const override;
    [[nodiscard]] DisplayState query_state() const override;
    bool apply(const DisplaySettings& settings, std::string& error) override;

private:
    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
};

} // namespace Engine
