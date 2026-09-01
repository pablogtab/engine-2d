#include "Window.hpp"
#include "Log.hpp"
#include "Input.hpp"
#include <algorithm>
#include <fstream>

namespace Engine {

namespace {
    const char* STATE_FILE_PRIMARY = "build/.window_state";
    const char* STATE_FILE_FALLBACK = ".window_state";

    constexpr std::uint32_t lifecycle_event_bit(ApplicationLifecycleEvent event) {
        return 1U << static_cast<std::uint32_t>(event);
    }
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
    
    // A dev build is commonly relaunched by a background shell. Make the
    // foreground/focus intent explicit instead of relying on environment or
    // platform defaults inherited by that shell.
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "1");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        ENGINE_LOG_ERROR("Failed to initialize SDL3: {}", SDL_GetError());
        return;
    }
    m_lifecycle_watch_registered = SDL_AddEventWatch(&Window::lifecycle_event_watch, this);
    if (!m_lifecycle_watch_registered) {
        ENGINE_LOG_WARN("[LIFECYCLE] Could not register SDL event watch: {}", SDL_GetError());
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
    if (m_lifecycle_watch_registered) {
        SDL_RemoveEventWatch(&Window::lifecycle_event_watch, this);
        m_lifecycle_watch_registered = false;
    }
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    SDL_Quit();
}

bool SDLCALL Window::lifecycle_event_watch(void* userdata, SDL_Event* event) {
    auto* window = static_cast<Window*>(userdata);
    if (!window || !event) return true;
    const auto lifecycle_event = application_lifecycle_event_from_sdl(event->type);
    if (lifecycle_event) {
        window->m_pending_lifecycle_events.fetch_or(
            lifecycle_event_bit(*lifecycle_event),
            std::memory_order_release
        );
    }
    return true;
}

void Window::drain_lifecycle_events() {
    const std::uint32_t pending = m_pending_lifecycle_events.exchange(
        0,
        std::memory_order_acquire
    );
    if (!m_lifecycle_callback || pending == 0) return;
    constexpr ApplicationLifecycleEvent ordered_events[]{
        ApplicationLifecycleEvent::LowMemory,
        ApplicationLifecycleEvent::WillEnterBackground,
        ApplicationLifecycleEvent::DidEnterBackground,
        ApplicationLifecycleEvent::WillEnterForeground,
        ApplicationLifecycleEvent::DidEnterForeground,
        ApplicationLifecycleEvent::Terminating
    };
    for (const ApplicationLifecycleEvent event : ordered_events) {
        if ((pending & lifecycle_event_bit(event)) != 0) m_lifecycle_callback(event);
    }
}

void Window::update_sizes() {
    SDL_GetWindowSize(m_window, &m_width, &m_height);
    SDL_GetRenderOutputSize(m_renderer, &m_pixel_width, &m_pixel_height);
}

void Window::set_title(const std::string& title) {
    m_title = title;
    if (m_window) {
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

void Window::request_close() {
    if (m_close_request_callback) {
        if (m_close_request_callback()) {
            m_should_close = true;
        }
    } else {
        m_should_close = true;
    }
}

void Window::request_input_focus() {
    if (!m_window) return;

    if (SDL_GetKeyboardFocus() == m_window) return;

    if (!SDL_SetWindowFocusable(m_window, true)) {
        ENGINE_LOG_WARN("[WINDOW FOCUS] Could not mark window focusable: {}", SDL_GetError());
    }
    if (!SDL_RaiseWindow(m_window)) {
        ENGINE_LOG_WARN("[WINDOW FOCUS] Could not request keyboard focus: {}", SDL_GetError());
    }
}

void Window::poll_events() {
    drain_lifecycle_events();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        SDL_Event input_event = event;
        // SDL pointer events use window coordinates, which differ from render
        // coordinates on Retina/high-DPI displays even when logical
        // presentation is disabled (for example, responsive fullscreen). Keep
        // the original event for ImGui, which applies its own DPI scaling.
        static_cast<void>(SDL_ConvertEventToRenderCoordinates(m_renderer, &input_event));
        bool display_changed = false;
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            if (m_close_request_callback) {
                if (m_close_request_callback()) {
                    m_should_close = true;
                }
            } else {
                m_should_close = true;
            }
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

        // Some window managers deliver the activation click before assigning
        // keyboard focus to a freshly relaunched process. The click is direct
        // user intent, so retrying the focus request here is safe and avoids a
        // mouse-active but keyboard-inactive editor window.
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.windowID == SDL_GetWindowID(m_window) &&
            SDL_GetKeyboardFocus() != m_window) {
            request_input_focus();
        }

        if (m_event_callback) {
            m_event_callback(event, input_event);
        } else {
            Input::process_event(input_event);
        }
    }
    if (m_startup_focus_attempts_remaining > 0) {
        request_input_focus();
        --m_startup_focus_attempts_remaining;
    }
    drain_lifecycle_events();
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

} // namespace Engine
