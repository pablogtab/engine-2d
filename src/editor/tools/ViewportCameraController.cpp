#include "ViewportCameraController.hpp"
#include "ViewportNavigation.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include <cmath>

namespace Engine {

namespace {
constexpr float SCROLL_ZOOM_SENSITIVITY{0.04f};
} // namespace

void ViewportCameraController::handle_input(
    Scene* scene,
    Camera2D* camera,
    const EditorSelection& selection,
    bool is_viewport_hovered,
    bool is_viewport_focused,
    const glm::vec2& viewport_mouse_position,
    const glm::vec2& viewport_mouse_delta
) {
    if (!scene || !camera) return;

    ImGuiIO& io = ImGui::GetIO();
    if (is_viewport_hovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            // Input is normalized by ViewportPanel from ImGui logical
            // coordinates into the fixed render-target coordinates.
            static_cast<void>(ViewportNavigation::pan_pixels(camera, viewport_mouse_delta));
        }
        if (io.MouseWheel != 0.0f) {
            const float zoom_factor = std::exp(io.MouseWheel * SCROLL_ZOOM_SENSITIVITY);
            static_cast<void>(ViewportNavigation::zoom_at(camera, viewport_mouse_position, zoom_factor));
        }
    }

    if ((!is_viewport_focused && !is_viewport_hovered) || io.WantTextInput) return;

    if (ShortcutManager::get().is_triggered("editor.focus_camera")) {
        const std::vector<EntityID> selected(
            selection.get_selected().begin(), selection.get_selected().end()
        );
        static_cast<void>(ViewportNavigation::frame_entities(scene, camera, selected));
    }
    if (ShortcutManager::get().is_triggered("editor.frame_all")) {
        static_cast<void>(ViewportNavigation::frame_all(scene, camera));
    }
    if (ShortcutManager::get().is_triggered("editor.reset_view")) {
        static_cast<void>(ViewportNavigation::reset(camera));
    }
}

} // namespace Engine
