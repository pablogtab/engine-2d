#include "ViewportCameraController.hpp"
#include "../../ecs/Components.hpp"

namespace Engine {

void ViewportCameraController::handle_input(GenericScene* scene, bool is_viewport_hovered, EntityID focus_target) {
    if (!scene || !is_viewport_hovered) return;

    ImGuiIO& io = ImGui::GetIO();

    // 1. Pan (Right Drag or Middle Drag)
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 drag_delta = io.MouseDelta;
        scene->pan_editor_camera(glm::vec2(drag_delta.x, drag_delta.y));
    }

    // 2. Zoom (Mouse Wheel)
    float wheel = io.MouseWheel;
    if (wheel != 0.0f) {
        float factor = (wheel > 0.0f) ? 1.15f : (1.0f / 1.15f);
        scene->zoom_editor_camera(factor);
    }

    // 3. Focus on target entity ('F' key)
    if (ImGui::IsKeyPressed(ImGuiKey_F) && focus_target != 0) {
        EntityManager& em = scene->get_entity_manager();
        if (em.is_alive(focus_target) && em.has_component<TransformComponent>(focus_target)) {
            const auto& tx = em.get_component<TransformComponent>(focus_target);
            scene->focus_editor_camera(glm::vec2(tx.position.x, tx.position.y));
        }
    }
}

} // namespace Engine
