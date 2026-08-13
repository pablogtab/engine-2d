#include "Window.hpp"
#include <iostream>

namespace Engine {

Window::Window(const std::string& title, int width, int height)
    : m_title(title), m_width(width), m_height(height) {
    
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "[ERROR] Failed to initialize SDL3: " << SDL_GetError() << std::endl;
        return;
    }

    m_window = SDL_CreateWindow(
        m_title.c_str(), 
        m_width, 
        m_height, 
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!m_window) {
        std::cerr << "[ERROR] Could not create SDL3 window: " << SDL_GetError() << std::endl;
        return;
    }

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) {
        std::cerr << "[ERROR] Could not create SDL3 renderer: " << SDL_GetError() << std::endl;
        return;
    }

    // Enable VSync by default
    SDL_SetRenderVSync(m_renderer, 1);

    // Set fixed virtual logical presentation resolution so game FOV is identical on Retina (2x) and 1080p (1x) displays
    SDL_SetRenderLogicalPresentation(
        m_renderer, 
        m_width, 
        m_height, 
        SDL_LOGICAL_PRESENTATION_LETTERBOX
    );

    update_sizes();

    std::cout << "[INFO] Window and SDL3 renderer successfully created. Backend: " 
              << SDL_GetRendererName(m_renderer) 
              << " | Logical: " << m_width << "x" << m_height 
              << " | Pixels: " << m_pixel_width << "x" << m_pixel_height << std::endl;
}

Window::~Window() {
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
        if (event.type == SDL_EVENT_QUIT) {
            m_should_close = true;
        }
        else if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                m_should_close = true;
            }
        }
        else if (event.type == SDL_EVENT_WINDOW_RESIZED || 
                 event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                 event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
            
            update_sizes();

            if (m_resize_callback) {
                // Pass current render output dimensions to callback
                m_resize_callback(m_pixel_width, m_pixel_height);
            }
        }
    }
}

void Window::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_RenderClear(m_renderer);
}

void Window::present() {
    SDL_RenderPresent(m_renderer);
}

} // namespace Engine
