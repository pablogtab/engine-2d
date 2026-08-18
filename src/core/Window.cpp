#include "Window.hpp"
#include "Log.hpp"
#include "Input.hpp"
#include <algorithm>
#include <fstream>

namespace Engine {

namespace {
    const char* STATE_FILE_PRIMARY = "build/.window_state";
    const char* STATE_FILE_FALLBACK = ".window_state";
}

void Window::save_state() {
    if (!m_window || !m_persist_editor_state) return;

    int x = 0, y = 0;
    int w = 0, h = 0;
    SDL_GetWindowPosition(m_window, &x, &y);
    SDL_GetWindowSize(m_window, &w, &h);

    if (w <= 100 || h <= 100) return;

    std::ofstream out(STATE_FILE_PRIMARY);
    if (!out.is_open()) {
        out.open(STATE_FILE_FALLBACK);
    }
    if (out.is_open()) {
        out << x << " " << y << " " << w << " " << h << "\n";
    }
}

bool Window::load_state(int& x, int& y, int& w, int& h) {
    std::ifstream in(STATE_FILE_PRIMARY);
    if (!in.is_open()) {
        in.open(STATE_FILE_FALLBACK);
    }
    if (in.is_open()) {
        if (in >> x >> y >> w >> h) {
            if (w > 100 && h > 100) {
                return true;
            }
        }
    }
    return false;
}

Window::Window(const std::string& title, int width, int height, bool persist_editor_state)
    : m_title(title),
      m_width(width),
      m_height(height),
      m_persist_editor_state(persist_editor_state) {
    
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        ENGINE_LOG_ERROR("Failed to initialize SDL3: {}", SDL_GetError());
        return;
    }

    int saved_x = 0, saved_y = 0, saved_w = 0, saved_h = 0;
    bool has_saved_state = m_persist_editor_state && load_state(saved_x, saved_y, saved_w, saved_h);

    if (has_saved_state) {
        m_width = saved_w;
        m_height = saved_h;
    }

    m_window = SDL_CreateWindow(
        m_title.c_str(), 
        m_width, 
        m_height, 
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!m_window) {
        ENGINE_LOG_ERROR("Could not create SDL3 window: {}", SDL_GetError());
        return;
    }

    if (has_saved_state) {
        SDL_SetWindowPosition(m_window, saved_x, saved_y);
    }

    // Bring window to front and give it input focus on macOS when launched from scripts
    SDL_RaiseWindow(m_window);

    // The SDL GPU-backed 2D renderer preserves the SDL_Renderer API used by
    // the engine and editor while enabling custom fragment shaders. Keep a
    // regular renderer fallback for unsupported hardware and headless setups.
    m_renderer = SDL_CreateGPURenderer(nullptr, m_window);
    if (!m_renderer) {
        ENGINE_LOG_WARN("GPU-backed SDL renderer unavailable, using standard renderer: {}", SDL_GetError());
        m_renderer = SDL_CreateRenderer(m_window, nullptr);
    }
    if (!m_renderer) {
        ENGINE_LOG_ERROR("Could not create SDL3 renderer: {}", SDL_GetError());
        return;
    }

    // Enable VSync by default
    SDL_SetRenderVSync(m_renderer, 1);

    // Disable logical presentation so Renderer2D & Camera2D render in crisp 1:1 physical pixels (Retina & 1080p)
    SDL_SetRenderLogicalPresentation(
        m_renderer, 
        0, 
        0, 
        SDL_LOGICAL_PRESENTATION_DISABLED
    );

    update_sizes();

    ENGINE_LOG_INFO("Window & SDL3 renderer created. Backend: {} | Window: {}x{} | Pixels: {}x{}",
                    SDL_GetRendererName(m_renderer), m_width, m_height, m_pixel_width, m_pixel_height);
}

Window::~Window() {
    save_state();
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    SDL_Quit();
}

void Window::update_sizes() {
    SDL_GetWindowSize(m_window, &m_width, &m_height);
    SDL_GetRenderOutputSize(m_renderer, &m_pixel_width, &m_pixel_height);
}

void Window::poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (m_uses_logical_presentation) SDL_ConvertEventToRenderCoordinates(m_renderer, &event);
        bool display_changed = false;
        if (event.type == SDL_EVENT_QUIT) {
            m_should_close = true;
        }
        else if (event.type == SDL_EVENT_WINDOW_RESIZED || 
                 event.type == SDL_EVENT_WINDOW_MOVED ||
                 event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                 event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                 event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                 event.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED ||
                 event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                 event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN ||
                 event.type == SDL_EVENT_WINDOW_RESTORED ||
                 event.type == SDL_EVENT_WINDOW_MAXIMIZED ||
                 event.type == SDL_EVENT_WINDOW_EXPOSED) {
            
            int prev_w = m_pixel_width;
            int prev_h = m_pixel_height;
            update_sizes();
            save_state();

            if (m_pixel_width != prev_w || m_pixel_height != prev_h ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                event.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED ||
                event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN ||
                event.type == SDL_EVENT_WINDOW_RESIZED || 
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                float dpi = get_dpi_scale();
                ENGINE_LOG_INFO("[WINDOW DPI/RESIZE] Render target updated: {}x{} (Physical: {}x{}, DPI Scale: {:.2f})",
                                m_width, m_height, m_pixel_width, m_pixel_height, dpi);
                display_changed = true;
            }
        }
        else if (event.type >= SDL_EVENT_DISPLAY_FIRST && event.type <= SDL_EVENT_DISPLAY_LAST) {
            update_sizes();
            display_changed = true;
        }

        if (display_changed && m_display_change_callback) m_display_change_callback();

        if (m_event_callback) {
            m_event_callback(event);
        } else {
            Input::process_event(event);
        }
    }
}

void Window::set_logical_presentation(int reference_width, int reference_height, PresentationMode mode) {
    if (mode == PresentationMode::Responsive) {
        disable_logical_presentation();
        return;
    }
    SDL_RendererLogicalPresentation sdl_mode = SDL_LOGICAL_PRESENTATION_LETTERBOX;
    switch (mode) {
        case PresentationMode::Fit: sdl_mode = SDL_LOGICAL_PRESENTATION_LETTERBOX; break;
        case PresentationMode::Fill: sdl_mode = SDL_LOGICAL_PRESENTATION_OVERSCAN; break;
        case PresentationMode::Stretch: sdl_mode = SDL_LOGICAL_PRESENTATION_STRETCH; break;
        case PresentationMode::Responsive: break;
    }
    m_uses_logical_presentation = SDL_SetRenderLogicalPresentation(
        m_renderer,
        std::max(reference_width, 1),
        std::max(reference_height, 1),
        sdl_mode
    );
    if (!m_uses_logical_presentation) ENGINE_LOG_ERROR("Failed to configure logical presentation: {}", SDL_GetError());
}

void Window::disable_logical_presentation() {
    if (!m_renderer) return;
    if (!SDL_SetRenderLogicalPresentation(m_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED)) {
        ENGINE_LOG_ERROR("Failed to disable logical presentation: {}", SDL_GetError());
        return;
    }
    m_uses_logical_presentation = false;
}

void Window::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_RenderClear(m_renderer);
}

void Window::present() {
    SDL_RenderPresent(m_renderer);
}

} // namespace Engine
