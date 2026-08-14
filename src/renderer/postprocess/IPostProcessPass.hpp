#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace Engine {

// ---------------------------------------------------------------------------
// IPostProcessPass
// ---------------------------------------------------------------------------
// Interfaz base pura para todos los pases de post-procesado en espacio de pantalla.
// Soporta ciclo de vida, actualización temporal para animaciones/tweens y
// hook para inspección visual automática en Dear ImGui (Fase 8).
// ---------------------------------------------------------------------------
class IPostProcessPass {
public:
    virtual ~IPostProcessPass() = default;

    virtual const std::string& get_name() const = 0;
    virtual bool is_enabled() const = 0;
    virtual void set_enabled(bool enabled) = 0;

    // Actualización temporal de animaciones, decaimientos y ruidos procedurales
    virtual void update(float /*delta_time*/) {}

    // Aplicar el pase desde la textura origen hacia el render target de destino
    virtual void apply(
        SDL_Renderer* renderer, 
        SDL_Texture* source, 
        SDL_Texture* destination, 
        int width, 
        int height
    ) = 0;

    // Hook para que herramientas y editores (Dear ImGui) inspeccionen sus controles
    virtual void on_gui() {}

    // Reiniciar valores por defecto
    virtual void reset() {}
};

} // namespace Engine
