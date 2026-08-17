#pragma once

#include "../../scene/Camera2D.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

// Session-only rulers and guides. Scene data and undo history are untouched.
class ViewportReferenceOverlay {
public:
    bool render(const Camera2D& camera, const ImVec2& image_min, const ImVec2& image_size,
                const glm::vec2& viewport_mouse_position, float snap_step, bool accept_input);
    void render_settings_menu();

private:
    bool m_show_rulers{true};
    bool m_show_guides{true};
    std::vector<float> m_vertical_guides;
    std::vector<float> m_horizontal_guides;
    int m_active_axis{-1};
    size_t m_active_index{0};
};

} // namespace Engine
