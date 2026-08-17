#include "ViewportCameraController.hpp"
#include "ViewportSelectionGeometry.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include <cmath>
#include <limits>

namespace Engine {

namespace {

constexpr float SCROLL_ZOOM_SENSITIVITY{0.04f};

bool collect_bounds(EntityManager& entity_manager, const std::unordered_set<EntityID>& entities, glm::vec2& bounds_min, glm::vec2& bounds_max) {
    bounds_min = glm::vec2(std::numeric_limits<float>::max());
    bounds_max = glm::vec2(std::numeric_limits<float>::lowest());
    bool found_bounds = false;

    for (EntityID id : entities) {
        if (!entity_manager.is_alive(id) || !entity_manager.has_component<TransformComponent>(id)) continue;
        const auto& transform = entity_manager.get_component<TransformComponent>(id);
        if (!transform.world_visible) continue;

        ViewportSelectionQuad quad;
        if (!get_selection_quad(entity_manager, id, quad)) continue;
        for (const glm::vec2& corner : quad.corners) {
            bounds_min = glm::min(bounds_min, corner);
            bounds_max = glm::max(bounds_max, corner);
        }
        found_bounds = true;
    }
    return found_bounds;
}

bool collect_scene_bounds(EntityManager& entity_manager, glm::vec2& bounds_min, glm::vec2& bounds_max) {
    std::unordered_set<EntityID> visible_entities;
    entity_manager.for_each<TransformComponent>([&](EntityID id, const TransformComponent&) {
        visible_entities.insert(id);
    });
    return collect_bounds(entity_manager, visible_entities, bounds_min, bounds_max);
}

} // namespace

void ViewportCameraController::handle_input(
    GenericScene* scene,
    const EditorSelection& selection,
    bool is_viewport_hovered,
    bool is_viewport_focused,
    const glm::vec2& viewport_mouse_position,
    const glm::vec2& viewport_mouse_delta
) {
    if (!scene) return;

    ImGuiIO& io = ImGui::GetIO();
    if (is_viewport_hovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            // Input is normalized by the ViewportPanel from ImGui's logical
            // display coordinates into the fixed render-target coordinates.
            scene->pan_editor_camera(viewport_mouse_delta);
        }
        if (io.MouseWheel != 0.0f) {
            const float zoom_factor = std::exp(io.MouseWheel * SCROLL_ZOOM_SENSITIVITY);
            scene->zoom_editor_camera_at(viewport_mouse_position, zoom_factor);
        }
    }

    if (!is_viewport_focused || io.WantTextInput) return;

    EntityManager& entity_manager = scene->get_entity_manager();
    glm::vec2 bounds_min;
    glm::vec2 bounds_max;
    if (ShortcutManager::get().is_triggered("editor.focus_camera") &&
        collect_bounds(entity_manager, selection.get_selected(), bounds_min, bounds_max)) {
        scene->frame_editor_camera(bounds_min, bounds_max);
    }
    if (ShortcutManager::get().is_triggered("editor.frame_all") &&
        collect_scene_bounds(entity_manager, bounds_min, bounds_max)) {
        scene->frame_editor_camera(bounds_min, bounds_max);
    }
    if (ShortcutManager::get().is_triggered("editor.reset_view")) {
        scene->reset_editor_camera();
    }
}

} // namespace Engine
