#pragma once

#include "../EditorPanel.hpp"
#include "../tools/SurfaceGenerationService.hpp"

#include <array>
#include <future>
#include <memory>
#include <string>

struct SDL_Renderer;

namespace Engine {

class Texture;

class SurfaceGeneratorPanel final : public EditorPanel {
public:
    explicit SurfaceGeneratorPanel(SDL_Renderer* renderer);
    ~SurfaceGeneratorPanel() override;

    void on_gui() override;
    void on_render_resources_invalidated() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

private:
    struct SourceRow {
        std::array<char, 512> path{};
        float weight{1.0f};
        bool flip_x{true};
        bool flip_y{true};
        bool rotate_quarter_turns{false};
    };

    [[nodiscard]] bool build_settings(SurfaceGenerationSettings& settings, std::string& error) const;
    void start_generation();
    void poll_generation();
    void rebuild_preview();
    void save_result();
    void save_profile(const std::filesystem::path& output_path);
    void load_profile();
    [[nodiscard]] std::filesystem::path resolved_output_path(std::string& error) const;
    [[nodiscard]] std::filesystem::path profile_path_for(const std::filesystem::path& output) const;

    std::string m_name{"Surface Generator"};
    SDL_Renderer* m_renderer{nullptr};
    std::array<SourceRow, 6> m_sources{};
    int m_source_count{1};
    int m_width{2432};
    int m_height{1408};
    int m_patch_size{128};
    int m_overlap{32};
    int m_candidate_count{48};
    int m_repetition_radius{3};
    std::uint32_t m_seed{1337};
    float m_luminance_normalization{0.25f};
    float m_preview_zoom{0.35f};
    std::array<char, 512> m_output_path{};

    bool m_running{false};
    bool m_confirm_replace{false};
    std::shared_ptr<SurfaceGenerationControl> m_control;
    std::future<SurfaceGenerationResult> m_future;
    SurfaceGenerationResult m_result;
    SurfaceGenerationSettings m_generated_settings;
    std::array<SourceRow, 6> m_generated_sources{};
    int m_generated_source_count{0};
    std::unique_ptr<Texture> m_preview;
    std::string m_status;
    std::string m_error;
};

} // namespace Engine
