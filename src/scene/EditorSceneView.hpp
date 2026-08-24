#pragma once

#include "Camera2D.hpp"
#include "../renderer/RenderView.hpp"

namespace Engine {

class IEntityRenderFilter;

class EditorSceneView {
public:
    EditorSceneView();

    void configure(const RenderViewConfiguration& configuration);
    [[nodiscard]] Camera2D& camera() { return m_camera; }
    [[nodiscard]] const Camera2D& camera() const { return m_camera; }
    void set_render_filter(const IEntityRenderFilter* filter) { m_render_filter = filter; }
    [[nodiscard]] const IEntityRenderFilter* render_filter() const { return m_render_filter; }
    [[nodiscard]] const SafeAreaInsets& safe_area() const { return m_safe_area; }
    [[nodiscard]] bool show_safe_area_overlay() const { return m_show_safe_area_overlay; }

private:
    Camera2D m_camera;
    const IEntityRenderFilter* m_render_filter{nullptr};
    SafeAreaInsets m_safe_area{};
    bool m_show_safe_area_overlay{false};
};

} // namespace Engine
