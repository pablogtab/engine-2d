#include "ViewportNavigation.hpp"
#include "ViewportSelectionGeometry.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../scene/Camera2D.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Engine {
namespace {

constexpr glm::vec2 DEFAULT_ENTITY_FRAME_HALF_SIZE{16.0f, 16.0f};

Camera2D* editor_camera(Scene* scene) {
    return scene ? scene->get_render_camera(RenderViewKind::Editor) : nullptr;
}

void frame_camera(Camera2D& camera, const glm::vec2& bounds_min, const glm::vec2& bounds_max, float padding) {
    const glm::vec2 center = (bounds_min + bounds_max) * 0.5f;
    glm::vec2 extent = glm::max(bounds_max - bounds_min, glm::vec2(1.0f));
    extent *= 1.0f + std::max(0.0f, padding) * 2.0f;

    const glm::vec2 viewport = camera.get_viewport_size();
    camera.set_position(center);
    camera.set_zoom(std::min(viewport.x / extent.x, viewport.y / extent.y));
}

bool collect_bounds(
    EntityManager& entity_manager,
    const std::unordered_set<EntityID>& entities,
    glm::vec2& bounds_min,
    glm::vec2& bounds_max
) {
    bounds_min = glm::vec2(std::numeric_limits<float>::max());
    bounds_max = glm::vec2(std::numeric_limits<float>::lowest());
    bool found_bounds = false;

    for (EntityID id : entities) {
        if (!entity_manager.is_alive(id) || !entity_manager.has_component<TransformComponent>(id)) continue;
        const auto& transform = entity_manager.get_component<TransformComponent>(id);
        if (!transform.world_visible) continue;

        ViewportSelectionQuad quad;
        if (get_selection_quad(entity_manager, id, quad)) {
            for (const glm::vec2& corner : quad.corners) {
                bounds_min = glm::min(bounds_min, corner);
                bounds_max = glm::max(bounds_max, corner);
            }
        } else {
            const glm::vec2 center(transform.world_position.x, transform.world_position.y);
            bounds_min = glm::min(bounds_min, center - DEFAULT_ENTITY_FRAME_HALF_SIZE);
            bounds_max = glm::max(bounds_max, center + DEFAULT_ENTITY_FRAME_HALF_SIZE);
        }
        found_bounds = true;
    }
    return found_bounds;
}

} // namespace

bool ViewportNavigation::frame_entities(Scene* scene, const std::vector<EntityID>& entities, float padding) {
    Camera2D* camera = editor_camera(scene);
    if (!scene || !camera || entities.empty()) return false;
    EntityManager& entity_manager = scene->get_entity_manager();
    TransformSystem::update(entity_manager);
    const std::unordered_set<EntityID> unique_entities(entities.begin(), entities.end());
    glm::vec2 bounds_min;
    glm::vec2 bounds_max;
    if (!collect_bounds(entity_manager, unique_entities, bounds_min, bounds_max)) return false;
    frame_camera(*camera, bounds_min, bounds_max, padding);
    return true;
}

bool ViewportNavigation::frame_all(Scene* scene, float padding) {
    Camera2D* camera = editor_camera(scene);
    if (!scene || !camera) return false;
    EntityManager& entity_manager = scene->get_entity_manager();
    TransformSystem::update(entity_manager);
    std::unordered_set<EntityID> entities;
    entity_manager.for_each<TransformComponent>([&](EntityID id, const TransformComponent&) {
        entities.insert(id);
    });
    glm::vec2 bounds_min;
    glm::vec2 bounds_max;
    if (!collect_bounds(entity_manager, entities, bounds_min, bounds_max)) return false;
    frame_camera(*camera, bounds_min, bounds_max, padding);
    return true;
}

bool ViewportNavigation::pan_world(Scene* scene, const glm::vec2& world_delta) {
    Camera2D* camera = editor_camera(scene);
    if (!camera || !std::isfinite(world_delta.x) || !std::isfinite(world_delta.y)) return false;
    camera->move(world_delta);
    return true;
}

bool ViewportNavigation::pan_pixels(Scene* scene, const glm::vec2& pixel_delta) {
    Camera2D* camera = editor_camera(scene);
    if (!camera || !std::isfinite(pixel_delta.x) || !std::isfinite(pixel_delta.y)) return false;
    const float zoom = std::max(camera->get_zoom(), 0.001f);
    camera->move(glm::vec2(-pixel_delta.x, pixel_delta.y) / zoom);
    return true;
}

bool ViewportNavigation::zoom(Scene* scene, float factor) {
    Camera2D* camera = editor_camera(scene);
    if (!camera || !std::isfinite(factor) || factor <= 0.0f) return false;
    camera->set_zoom(camera->get_zoom() * factor);
    return true;
}

bool ViewportNavigation::zoom_at(Scene* scene, const glm::vec2& viewport_position, float factor) {
    Camera2D* camera = editor_camera(scene);
    if (!camera || !std::isfinite(viewport_position.x) || !std::isfinite(viewport_position.y)) return false;
    const glm::vec2 world_before = camera->screen_to_world(viewport_position);
    if (!zoom(scene, factor)) return false;
    const glm::vec2 world_after = camera->screen_to_world(viewport_position);
    camera->move(world_before - world_after);
    return true;
}

bool ViewportNavigation::reset(Scene* scene) {
    Camera2D* camera = editor_camera(scene);
    if (!camera) return false;
    camera->set_position(glm::vec2(0.0f));
    camera->set_zoom(1.0f);
    return true;
}

} // namespace Engine
