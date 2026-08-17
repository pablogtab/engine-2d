#pragma once

#include <imgui.h>

namespace Engine {

// Compact contextual toolbar that floats over, and docks to, a panel-owned
// host rectangle. Position is session-only editor view state.
class EditorToolbox {
public:
    bool begin(const char* id, const ImVec2& size, ImVec2 host_min, ImVec2 host_max);
    void render_drag_handle(const char* id);
    void end();
    [[nodiscard]] bool is_hovered() const { return m_hovered; }

private:
    enum class Anchor { Floating, Top, Bottom };
    ImVec2 m_position{-1.0f, -1.0f};
    ImVec2 m_size{0.0f, 0.0f};
    ImVec2 m_host_min{0.0f, 0.0f};
    ImVec2 m_host_max{0.0f, 0.0f};
    Anchor m_anchor{Anchor::Top};
    bool m_hovered{false};
};

} // namespace Engine
