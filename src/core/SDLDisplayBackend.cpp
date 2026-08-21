#include "SDLDisplayBackend.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

DisplayOrientation from_sdl_orientation(SDL_DisplayOrientation orientation) {
    switch (orientation) {
        case SDL_ORIENTATION_LANDSCAPE: return DisplayOrientation::Landscape;
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return DisplayOrientation::LandscapeFlipped;
        case SDL_ORIENTATION_PORTRAIT: return DisplayOrientation::Portrait;
        case SDL_ORIENTATION_PORTRAIT_FLIPPED: return DisplayOrientation::PortraitFlipped;
        case SDL_ORIENTATION_UNKNOWN: return DisplayOrientation::Unknown;
    }
    return DisplayOrientation::Unknown;
}

DisplayRect from_sdl_rect(const SDL_Rect& rect) {
    return {rect.x, rect.y, rect.w, rect.h};
}

bool same_mode(const DisplayMode& lhs, const DisplayMode& rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height &&
           std::abs(lhs.refresh_rate - rhs.refresh_rate) <= 0.01f &&
           std::abs(lhs.pixel_density - rhs.pixel_density) <= 0.01f;
}

} // namespace

SDLDisplayBackend::SDLDisplayBackend(SDL_Window* window, SDL_Renderer* renderer)
    : m_window(window), m_renderer(renderer) {}

DisplayCapabilities SDLDisplayBackend::capabilities() const {
#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
    return {};
#else
    return {true, true, true, true, true};
#endif
}

std::vector<DisplayInfo> SDLDisplayBackend::available_displays() const {
    std::vector<DisplayInfo> result;
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays) return result;
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const SDL_DisplayID id = displays[i];
        SDL_Rect bounds{};
        SDL_Rect usable{};
        SDL_GetDisplayBounds(id, &bounds);
        SDL_GetDisplayUsableBounds(id, &usable);
        const char* name = SDL_GetDisplayName(id);
        result.push_back({
            static_cast<DisplayId>(id),
            name ? name : "Display",
            from_sdl_rect(bounds),
            from_sdl_rect(usable),
            std::max(SDL_GetDisplayContentScale(id), 0.1f),
            from_sdl_orientation(SDL_GetCurrentDisplayOrientation(id)),
            {},
            id == primary
        });
    }
    SDL_free(displays);
    return result;
}

std::vector<DisplayMode> SDLDisplayBackend::available_modes(DisplayId display_id) const {
    std::vector<DisplayMode> result;
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(static_cast<SDL_DisplayID>(display_id), &count);
    if (!modes) return result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (!modes[i] || modes[i]->w <= 0 || modes[i]->h <= 0) continue;
        DisplayMode candidate{
            modes[i]->w,
            modes[i]->h,
            modes[i]->refresh_rate,
            std::max(modes[i]->pixel_density, 0.1f)
        };
        if (std::none_of(result.begin(), result.end(), [&candidate](const DisplayMode& existing) {
            return same_mode(existing, candidate);
        })) {
            result.push_back(candidate);
        }
    }
    SDL_free(modes);
    std::sort(result.begin(), result.end(), [](const DisplayMode& lhs, const DisplayMode& rhs) {
        if (lhs.width != rhs.width) return lhs.width < rhs.width;
        if (lhs.height != rhs.height) return lhs.height < rhs.height;
        if (std::abs(lhs.refresh_rate - rhs.refresh_rate) > 0.01f) return lhs.refresh_rate > rhs.refresh_rate;
        return lhs.pixel_density < rhs.pixel_density;
    });
    return result;
}

DisplayState SDLDisplayBackend::query_state() const {
    DisplayState state;
    if (!m_window || !m_renderer) return state;

    state.settings.display_id = static_cast<DisplayId>(SDL_GetDisplayForWindow(m_window));
    SDL_GetWindowSize(m_window, &state.logical_width, &state.logical_height);
    SDL_GetRenderOutputSize(m_renderer, &state.pixel_width, &state.pixel_height);
    state.dpi_scale = state.logical_height > 0
        ? static_cast<float>(state.pixel_height) / static_cast<float>(state.logical_height)
        : 1.0f;
    state.orientation = from_sdl_orientation(
        SDL_GetCurrentDisplayOrientation(static_cast<SDL_DisplayID>(state.settings.display_id))
    );
    SDL_Rect safe_area{};
    if (SDL_GetWindowSafeArea(m_window, &safe_area)) {
        const float scale_x = state.logical_width > 0
            ? static_cast<float>(state.pixel_width) / static_cast<float>(state.logical_width)
            : 1.0f;
        const float scale_y = state.logical_height > 0
            ? static_cast<float>(state.pixel_height) / static_cast<float>(state.logical_height)
            : 1.0f;
        state.safe_area = {
            std::max(static_cast<float>(safe_area.x) * scale_x, 0.0f),
            std::max(static_cast<float>(safe_area.y) * scale_y, 0.0f),
            std::max(static_cast<float>(state.logical_width - safe_area.x - safe_area.w) * scale_x, 0.0f),
            std::max(static_cast<float>(state.logical_height - safe_area.y - safe_area.h) * scale_y, 0.0f)
        };
    }

    const SDL_WindowFlags flags = SDL_GetWindowFlags(m_window);
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0) {
        const SDL_DisplayMode* fullscreen_mode = SDL_GetWindowFullscreenMode(m_window);
        if (fullscreen_mode) {
            state.settings.window_mode = WindowMode::ExclusiveFullscreen;
            state.settings.window_width = fullscreen_mode->w;
            state.settings.window_height = fullscreen_mode->h;
            state.settings.refresh_rate = fullscreen_mode->refresh_rate;
        } else {
            state.settings.window_mode = WindowMode::BorderlessFullscreen;
            state.settings.window_width = state.logical_width;
            state.settings.window_height = state.logical_height;
        }
    } else {
        state.settings.window_mode = WindowMode::Windowed;
        state.settings.window_width = state.logical_width;
        state.settings.window_height = state.logical_height;
    }

    int vsync = 0;
    state.settings.vsync = SDL_GetRenderVSync(m_renderer, &vsync) && vsync != 0;
    return state;
}

bool SDLDisplayBackend::apply(const DisplaySettings& settings, std::string& error) {
    if (!m_window || !m_renderer) {
        error = "SDL window or renderer is not available";
        return false;
    }

    const SDL_DisplayID display = static_cast<SDL_DisplayID>(settings.display_id);
    auto fail = [&error](const char* operation) {
        error = std::string(operation) + ": " + SDL_GetError();
        return false;
    };

    const DisplayState current = query_state();
    const bool structural_change = settings.display_id != current.settings.display_id ||
        settings.window_mode != current.settings.window_mode ||
        (settings.window_mode == WindowMode::Windowed &&
            (settings.window_width != current.logical_width || settings.window_height != current.logical_height)) ||
        (settings.window_mode == WindowMode::ExclusiveFullscreen &&
            (settings.window_width != current.settings.window_width ||
             settings.window_height != current.settings.window_height ||
             std::abs(settings.refresh_rate - current.settings.refresh_rate) > 0.01f));

    if (structural_change) {
        if (!SDL_SetWindowFullscreen(m_window, false)) return fail("Failed to leave fullscreen");
        if (!SDL_SyncWindow(m_window)) return fail("Failed to synchronize windowed state");
        if (!SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display))) {
            return fail("Failed to move window to display");
        }

        if (settings.window_mode == WindowMode::Windowed) {
            if (!SDL_SetWindowFullscreenMode(m_window, nullptr)) return fail("Failed to clear fullscreen mode");
            if (!SDL_SetWindowSize(m_window, settings.window_width, settings.window_height)) {
                return fail("Failed to resize window");
            }
        } else if (settings.window_mode == WindowMode::BorderlessFullscreen) {
            if (!SDL_SetWindowFullscreenMode(m_window, nullptr)) return fail("Failed to select borderless mode");
            if (!SDL_SetWindowFullscreen(m_window, true)) return fail("Failed to enter borderless fullscreen");
        } else {
            int count = 0;
            SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
            if (!modes) return fail("Failed to enumerate fullscreen modes");
            const SDL_DisplayMode* selected = nullptr;
            for (int i = 0; i < count; ++i) {
                if (modes[i] && modes[i]->w == settings.window_width && modes[i]->h == settings.window_height &&
                    std::abs(modes[i]->refresh_rate - settings.refresh_rate) <= 0.01f) {
                    selected = modes[i];
                    break;
                }
            }
            const bool mode_found = selected != nullptr;
            const bool mode_applied = mode_found && SDL_SetWindowFullscreenMode(m_window, selected);
            SDL_free(modes);
            if (!mode_applied) {
                error = mode_found ? std::string("Failed to select exclusive mode: ") + SDL_GetError()
                                   : "Requested exclusive fullscreen mode is unavailable";
                return false;
            }
            if (!SDL_SetWindowFullscreen(m_window, true)) return fail("Failed to enter exclusive fullscreen");
        }
        if (!SDL_SyncWindow(m_window)) return fail("Failed to synchronize display settings");
    }

    if (!SDL_SetRenderVSync(m_renderer, settings.vsync ? 1 : 0)) return fail("Failed to configure VSync");
    return true;
}

} // namespace Engine
