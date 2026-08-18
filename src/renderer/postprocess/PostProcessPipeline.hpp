#pragma once

#include "IPostProcessPass.hpp"
#include <vector>
#include <memory>
#include <string>

namespace Engine {

class PostProcessPipeline {
public:
    PostProcessPipeline() = default;
    ~PostProcessPipeline();

    PostProcessPipeline(const PostProcessPipeline&) = delete;
    PostProcessPipeline& operator=(const PostProcessPipeline&) = delete;

    void init(
        SDL_Renderer* renderer,
        int width,
        int height,
        int authoring_width = 1280,
        int authoring_height = 720
    );
    void resize(int width, int height);
    void set_effect_reference_size(int width, int height);
    void destroy_targets();

    // Actualizar todos los pases registrados (animaciones, tweens, ruidos)
    void update(float delta_time);

    // Registro y gestión de pases
    void add_pass(std::shared_ptr<IPostProcessPass> pass);
    void remove_pass(const std::string& name);
    void set_pass_enabled(const std::string& name, bool enabled);
    bool is_pass_enabled(const std::string& name) const;
    void clear_passes();

    template<typename T>
    std::shared_ptr<T> get_pass(const std::string& name) const {
        for (const auto& pass : m_passes) {
            if (pass && pass->get_name() == name) {
                return std::dynamic_pointer_cast<T>(pass);
            }
        }
        return nullptr;
    }

    const std::vector<std::shared_ptr<IPostProcessPass>>& get_passes() const { return m_passes; }
    size_t get_active_pass_count() const;

    // Redirección de Render Targets
    // Starts a full-frame capture. When the destination was already cleared or
    // populated before the scene begins (for example, the editor Viewport),
    // seed the capture with that texture so post-processing sees the complete
    // camera image instead of only subsequently submitted sprites.
    void begin_capture(
        SDL_Texture* backdrop = nullptr,
        SDL_Color clear_color = SDL_Color{0, 0, 0, 0}
    );
    void end_capture_and_process(SDL_Texture* final_destination = nullptr);

    SDL_Texture* get_capture_target() const { return m_target_a; }
    int get_width() const { return m_width; }
    int get_height() const { return m_height; }
    [[nodiscard]] const PostProcessContext& get_context() const { return m_context; }
    void request_frame_probe(std::string label) { m_pending_probe_label = std::move(label); }

    // Buffers auxiliares de resolución reducida (para Bloom / Gaussian Blur)
    SDL_Texture* get_half_target_a() const { return m_half_target_a; }
    SDL_Texture* get_half_target_b() const { return m_half_target_b; }
    SDL_Texture* get_quarter_target() const { return m_quarter_target; }

private:
    void create_targets();

    SDL_Renderer* m_renderer{nullptr};
    SDL_Texture* m_target_a{nullptr};
    SDL_Texture* m_target_b{nullptr};

    // Render targets de Downsampling para optimización de efectos densos
    SDL_Texture* m_half_target_a{nullptr};
    SDL_Texture* m_half_target_b{nullptr};
    SDL_Texture* m_quarter_target{nullptr};

    int m_width{1280};
    int m_height{720};
    PostProcessContext m_context;

    std::vector<std::shared_ptr<IPostProcessPass>> m_passes;
    bool m_is_capturing{false};
    std::string m_pending_probe_label;
};

} // namespace Engine
