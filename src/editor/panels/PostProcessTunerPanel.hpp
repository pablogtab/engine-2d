#pragma once

#include "../EditorPanel.hpp"
#include <string>

namespace Engine {

class PostProcessPipeline;

class PostProcessTunerPanel : public EditorPanel {
public:
    explicit PostProcessTunerPanel(PostProcessPipeline* pipeline = nullptr);
    ~PostProcessTunerPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_pipeline(PostProcessPipeline* pipeline) { m_pipeline = pipeline; }

private:
    std::string m_name{"Post-Processing Tuner"};
    PostProcessPipeline* m_pipeline{nullptr};

    float m_bloom_threshold{0.7f};
    float m_bloom_intensity{1.2f};

    float m_chromatic_offset{3.0f};

    float m_vignette_radius{0.85f};
    float m_vignette_smoothness{0.45f};
    float m_vignette_color[3]{0.0f, 0.0f, 0.0f};

    float m_film_grain_intensity{0.15f};
    float m_scanlines_intensity{0.25f};

    float m_exposure{1.0f};
    float m_contrast{1.05f};
    float m_saturation{1.1f};
};

} // namespace Engine
