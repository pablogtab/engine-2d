#include "ViewportReferenceOverlay.hpp"
#include <algorithm>
#include <cmath>

namespace Engine {

void ViewportReferenceOverlay::render_settings_menu() {
    ImGui::SeparatorText("Reference guides");
    ImGui::Checkbox("Show rulers", &m_show_rulers);
    ImGui::Checkbox("Show guides", &m_show_guides);
    if (ImGui::Button("Clear guides")) {
        m_vertical_guides.clear();
        m_horizontal_guides.clear();
        m_active_axis = -1;
    }
}

bool ViewportReferenceOverlay::render(const Camera2D& camera, const ImVec2& image_min, const ImVec2& image_size,
                                      const glm::vec2& viewport_mouse_position, float snap_step, bool accept_input) {
    constexpr float ruler_size = 18.0f;
    constexpr float hit_radius = 5.0f;
    if (image_size.x <= 0.0f || image_size.y <= 0.0f) return false;
    const ImVec2 image_max(image_min.x + image_size.x, image_min.y + image_size.y);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool mouse_in_image = accept_input && mouse.x >= image_min.x && mouse.x <= image_max.x && mouse.y >= image_min.y && mouse.y <= image_max.y;
    const glm::vec2 viewport = camera.get_viewport_size();
    const float sx = image_size.x / viewport.x, sy = image_size.y / viewport.y;
    const auto to_screen = [&](glm::vec2 world) { const glm::vec2 p = camera.world_to_screen(glm::vec3(world, 0)); return ImVec2(image_min.x + p.x * sx, image_min.y + p.y * sy); };
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (m_show_guides) {
        dl->PushClipRect(image_min, image_max, true);
        for (float x : m_vertical_guides) { const float px = to_screen({x, 0}).x; dl->AddLine({px, image_min.y}, {px, image_max.y}, IM_COL32(56,189,248,220)); }
        for (float y : m_horizontal_guides) { const float py = to_screen({0, y}).y; dl->AddLine({image_min.x, py}, {image_max.x, py}, IM_COL32(56,189,248,220)); }
        dl->PopClipRect();
    }
    if (m_show_rulers) {
        dl->AddRectFilled(image_min, {image_max.x, image_min.y + ruler_size}, IM_COL32(21,24,33,238));
        dl->AddRectFilled(image_min, {image_min.x + ruler_size, image_max.y}, IM_COL32(21,24,33,238));
        dl->AddLine({image_min.x, image_min.y + ruler_size}, {image_max.x, image_min.y + ruler_size}, IM_COL32(70,78,103,230));
        dl->AddLine({image_min.x + ruler_size, image_min.y}, {image_min.x + ruler_size, image_max.y}, IM_COL32(70,78,103,230));
        const glm::vec2 a = camera.screen_to_world({0,0}), b = camera.screen_to_world(viewport);
        const float cell = std::max(snap_step, 0.001f);
        const float px_cell = std::max(std::abs((to_screen({cell,0}).x - to_screen({0,0}).x)), 0.001f);
        const float step = cell * std::max(1, static_cast<int>(std::ceil(80.0f / px_cell)));
        dl->PushClipRect(image_min, image_max, true);
        for (int i=static_cast<int>(std::floor(std::min(a.x,b.x)/step)); i<=static_cast<int>(std::ceil(std::max(a.x,b.x)/step)); ++i) { float v=i*step, x=to_screen({v,0}).x; dl->AddLine({x,image_min.y+13},{x,image_min.y+18},IM_COL32(70,78,103,230)); dl->AddText({x+3,image_min.y+2},IM_COL32(148,163,184,210),std::to_string(static_cast<int>(std::round(v))).c_str()); }
        for (int i=static_cast<int>(std::floor(std::min(a.y,b.y)/step)); i<=static_cast<int>(std::ceil(std::max(a.y,b.y)/step)); ++i) { float v=i*step, y=to_screen({0,v}).y; dl->AddLine({image_min.x+13,y},{image_min.x+18,y},IM_COL32(70,78,103,230)); }
        dl->PopClipRect();
    }
    const bool top = m_show_rulers && mouse.x >= image_min.x+ruler_size && mouse.x <= image_max.x && mouse.y >= image_min.y && mouse.y <= image_min.y+ruler_size;
    const bool left = m_show_rulers && mouse.x >= image_min.x && mouse.x <= image_min.x+ruler_size && mouse.y >= image_min.y+ruler_size && mouse.y <= image_max.y;
    int axis=-1; size_t index=0; float closest=hit_radius;
    if (m_show_guides) { for(size_t i=0;i<m_vertical_guides.size();++i){float d=std::abs(mouse.x-to_screen({m_vertical_guides[i],0}).x);if(d<=closest){closest=d;axis=0;index=i;}} for(size_t i=0;i<m_horizontal_guides.size();++i){float d=std::abs(mouse.y-to_screen({0,m_horizontal_guides[i]}).y);if(d<=closest){closest=d;axis=1;index=i;}} }
    if (m_active_axis >= 0) { if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_active_axis=-1; else { glm::vec2 w=camera.screen_to_world(viewport_mouse_position); if(m_active_axis==0) m_vertical_guides[m_active_index]=w.x; else m_horizontal_guides[m_active_index]=w.y; } return true; }
    if (mouse_in_image && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && axis>=0) { if(axis==0)m_vertical_guides.erase(m_vertical_guides.begin()+index);else m_horizontal_guides.erase(m_horizontal_guides.begin()+index);return true; }
    if (mouse_in_image && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { glm::vec2 w=camera.screen_to_world(viewport_mouse_position); if(top){m_horizontal_guides.push_back(w.y);m_active_axis=1;m_active_index=m_horizontal_guides.size()-1;return true;} if(left){m_vertical_guides.push_back(w.x);m_active_axis=0;m_active_index=m_vertical_guides.size()-1;return true;} if(axis>=0){m_active_axis=axis;m_active_index=index;return true;} }
    return false;
}

} // namespace Engine
