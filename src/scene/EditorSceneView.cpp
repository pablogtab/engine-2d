#include "EditorSceneView.hpp"

namespace Engine {

EditorSceneView::EditorSceneView() : m_camera(1920.0f, 1080.0f) {
    m_camera.set_position({0.0f, 0.0f});
    m_camera.set_zoom(1.0f);
}

void EditorSceneView::configure(const RenderViewConfiguration& configuration) {
    m_safe_area = configuration.safe_area;
    m_show_safe_area_overlay = configuration.show_safe_area_overlay;
    m_camera.set_viewport_size(
        static_cast<float>(configuration.output_width),
        static_cast<float>(configuration.output_height)
    );
    m_camera.set_reference_size(
        static_cast<float>(configuration.reference_width),
        static_cast<float>(configuration.reference_height)
    );
}

} // namespace Engine
