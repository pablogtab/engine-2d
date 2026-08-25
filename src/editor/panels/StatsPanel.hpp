#pragma once

#include "../EditorPanel.hpp"

namespace Engine {

class FrameProfiler;

class StatsPanel : public EditorPanel {
public:
    explicit StatsPanel(FrameProfiler* profiler = nullptr, std::string project_root = {});
    ~StatsPanel() override = default;

    void on_gui() override;
    void on_update(float delta_time) override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

private:
    std::string m_name{"Engine & Metrics"};
    float m_frametime_history[60]{};
    int m_history_offset{0};
    float m_accumulated_time{0.0f};
    FrameProfiler* m_profiler{nullptr};
    int m_history_window{120};
    int m_selected_frame{-1};
    bool m_follow_latest_frame{true};
    std::string m_project_root;
    std::string m_export_feedback;
    std::string m_stress_feedback;
};

} // namespace Engine
