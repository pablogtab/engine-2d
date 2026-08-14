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

    void init(SDL_Renderer* renderer, int width, int height);
    void resize(int width, int height);
    void destroy_targets();

    // Pipeline registration
    void add_pass(std::shared_ptr<IPostProcessPass> pass);
    void remove_pass(const std::string& name);
    void set_pass_enabled(const std::string& name, bool enabled);
    bool is_pass_enabled(const std::string& name) const;
    void clear_passes();

    template<typename T>
    std::shared_ptr<T> get_pass(const std::string& name) const {
        for (const auto& pass : m_passes) {
            if (pass->get_name() == name) {
                return std::dynamic_pointer_cast<T>(pass);
            }
        }
        return nullptr;
    }

    const std::vector<std::shared_ptr<IPostProcessPass>>& get_passes() const { return m_passes; }
    size_t get_active_pass_count() const;

    // Render target redirection
    void begin_capture();
    void end_capture_and_process(SDL_Texture* final_destination = nullptr);

    SDL_Texture* get_capture_target() const { return m_target_a; }
    int get_width() const { return m_width; }
    int get_height() const { return m_height; }

private:
    void create_targets();

    SDL_Renderer* m_renderer{nullptr};
    SDL_Texture* m_target_a{nullptr};
    SDL_Texture* m_target_b{nullptr};
    int m_width{1280};
    int m_height{720};

    std::vector<std::shared_ptr<IPostProcessPass>> m_passes;
    bool m_is_capturing{false};
};

} // namespace Engine
