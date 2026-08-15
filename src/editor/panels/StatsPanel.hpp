#pragma once

#include "../EditorPanel.hpp"

namespace Engine {

class StatsPanel : public EditorPanel {
public:
    StatsPanel();
    ~StatsPanel() override = default;

    void on_gui() override;
    void on_update(float delta_time) override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

private:
    std::string m_name{"Engine & Metrics"};
    float m_frametime_history[60]{};
    int m_history_offset{0};
    float m_accumulated_time{0.0f};
};

} // namespace Engine
