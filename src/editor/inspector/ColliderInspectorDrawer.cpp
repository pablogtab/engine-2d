#include "ColliderInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "MultiComponentInspectorTransaction.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include <imgui.h>
#include <string>


namespace Engine {

namespace {

bool shape_equal(const ColliderShape& lhs, const ColliderShape& rhs) {
    return lhs.type == rhs.type && lhs.offset == rhs.offset && lhs.size == rhs.size &&
        lhs.radius == rhs.radius && lhs.vertices == rhs.vertices &&
        lhs.is_trigger == rhs.is_trigger && lhs.is_one_way == rhs.is_one_way &&
        lhs.category_bits == rhs.category_bits && lhs.mask_bits == rhs.mask_bits;
}

bool collider_equal(const ColliderComponent& lhs, const ColliderComponent& rhs) {
    if (lhs.shapes.size() != rhs.shapes.size()) return false;
    for (std::size_t index = 0; index < lhs.shapes.size(); ++index) {
        if (!shape_equal(lhs.shapes[index], rhs.shapes[index])) return false;
    }
    return true;
}

void rebuild_physics(EntityManager& em, EntityID entity) {
    PhysicsSystem::rebuild_body(em, entity);
}

void rebuild_selected_collider_physics(EntityManager& em, const std::vector<EntityID>& entities) {
    for (const EntityID entity : entities) {
        if (em.is_alive(entity)) {
            PhysicsSystem::rebuild_body(em, entity);
        }
    }
}

void track(
    EntityManager& em,
    EntityID entity,
    const ColliderComponent& before,
    const std::string& property,
    const char* description,
    const ComponentTransactionContext& context,
    bool force_commit = false
) {
    track_component_edit<ColliderComponent>(
        em,
        entity,
        before,
        property.c_str(),
        description,
        context,
        collider_equal,
        rebuild_physics,
        force_commit
    );
}

std::string shape_property(std::size_t index, const char* property) {
    return "Collider.Shape." + std::to_string(index) + "." + property;
}

} // namespace

void ColliderInspectorDrawer::draw(
    EntityManager& em,
    EntityID entity,
    const ComponentTransactionContext& context
) {
    if (!em.has_component<ColliderComponent>(entity)) return;

    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, "collider");
    const auto header = InspectorWidgets::registered_component_header("collider", true, true, has_override);
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, "collider", nullptr
        ));
        return;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, "collider", nullptr
        ));
        return;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ColliderComponent>>(
            &em,
            entity,
            em.get_component<ColliderComponent>(entity),
            std::nullopt,
            "Remove Collider 2D",
            rebuild_physics
        ));
        return;
    }
    if (!header.open) return;
    ImGui::PushID("ColliderComponentDrawer");


    using namespace InspectorWidgets;
    auto& collider = em.get_component<ColliderComponent>(entity);
    int shape_to_remove = -1;

    for (std::size_t index = 0; index < collider.shapes.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        auto& shape = collider.shapes[index];
        ImGui::Text("Shape #%zu", index + 1);
        ImGui::SameLine();
        if (editor_icon_button(
            "RemoveShape",
            EditorIcon::Trash,
            "Remove collider shape",
            false,
            ImVec2(18.0f, 18.0f)
        )) {
            shape_to_remove = static_cast<int>(index);
        }

        const char* shape_types[] = {"Box", "Circle", "Polygon"};
        int shape_type = static_cast<int>(shape.type);
        const ColliderComponent type_before = collider;
        begin_property_row("Type");
        begin_property_interaction("Collider.ShapeType");
        const bool type_changed = ImGui::Combo("##shape_type", &shape_type, shape_types, IM_ARRAYSIZE(shape_types));
        capture_property_interaction();
        if (type_changed) {
            shape.type = static_cast<ColliderShapeType>(shape_type);
            if (shape.type == ColliderShapeType::Polygon && shape.vertices.size() < 3) {
                shape.vertices = {{-16.0f, -16.0f}, {16.0f, -16.0f}, {0.0f, 16.0f}};
            }
        }
        track(em, entity, type_before, shape_property(index, "Type"), "Change Collider Shape Type", context, type_changed);

        if (shape.type == ColliderShapeType::Box) {
            const ColliderComponent size_before = collider;
            float size[2] = {shape.size.x, shape.size.y};
            if (property_vec2("Size", size, InspectorPropertyPolicies::Collider::Size)) {
                shape.size = {size[0], size[1]};
            }
            track(em, entity, size_before, shape_property(index, "Size"), "Edit Collider Size", context);
        } else if (shape.type == ColliderShapeType::Circle) {
            const ColliderComponent radius_before = collider;
            property_float("Radius", &shape.radius, InspectorPropertyPolicies::Collider::Radius);
            track(em, entity, radius_before, shape_property(index, "Radius"), "Edit Collider Radius", context);
        } else {
            for (std::size_t vertex = 0; vertex < shape.vertices.size(); ++vertex) {
                const ColliderComponent vertex_before = collider;
                float position[2] = {shape.vertices[vertex].x, shape.vertices[vertex].y};
                const std::string label = "Vertex " + std::to_string(vertex + 1);
                if (property_vec2(label.c_str(), position, InspectorPropertyPolicies::Collider::Coordinate)) {
                    shape.vertices[vertex] = {position[0], position[1]};
                }
                track(
                    em,
                    entity,
                    vertex_before,
                    shape_property(index, ("Vertex." + std::to_string(vertex)).c_str()),
                    "Edit Collider Vertex",
                    context
                );
            }
        }

        const ColliderComponent offset_before = collider;
        float offset[2] = {shape.offset.x, shape.offset.y};
        if (property_vec2("Offset", offset, InspectorPropertyPolicies::Collider::Coordinate)) {
            shape.offset = {offset[0], offset[1]};
        }
        track(em, entity, offset_before, shape_property(index, "Offset"), "Edit Collider Offset", context);

        const ColliderComponent trigger_before = collider;
        property_bool("Is Trigger", &shape.is_trigger);
        track(em, entity, trigger_before, shape_property(index, "IsTrigger"), "Toggle Collider Trigger", context);

        const ColliderComponent one_way_before = collider;
        property_bool("Is One-Way", &shape.is_one_way);
        track(em, entity, one_way_before, shape_property(index, "IsOneWay"), "Toggle One-Way Collider", context);

        int category = shape.category_bits;
        const ColliderComponent category_before = collider;
        if (property_int("Category Bits", &category, InspectorPropertyPolicies::Collider::CollisionBits)) {
            shape.category_bits = static_cast<std::uint16_t>(category);
        }
        track(em, entity, category_before, shape_property(index, "Category"), "Edit Collider Category", context);

        int mask = shape.mask_bits;
        const ColliderComponent mask_before = collider;
        if (property_int("Mask Bits", &mask, InspectorPropertyPolicies::Collider::CollisionBits)) {
            shape.mask_bits = static_cast<std::uint16_t>(mask);
        }
        track(em, entity, mask_before, shape_property(index, "Mask"), "Edit Collider Mask", context);

        ImGui::Separator();
        ImGui::PopID();
    }

    if (shape_to_remove >= 0 && shape_to_remove < static_cast<int>(collider.shapes.size())) {
        const ColliderComponent before = collider;
        collider.shapes.erase(collider.shapes.begin() + shape_to_remove);
        track(em, entity, before, "Collider.RemoveShape", "Remove Collider Shape", context, true);
    }

    const std::string add_shape_label = editor_icon_label(EditorIcon::Plus, "Add Shape");
    if (ImGui::Button(add_shape_label.c_str(), ImVec2(-1.0f, 0.0f))) ImGui::OpenPopup("AddColliderShape");
    if (ImGui::BeginPopup("AddColliderShape")) {
        auto add_shape = [&](ColliderShape shape, const char* description) {
            const ColliderComponent before = collider;
            collider.shapes.push_back(std::move(shape));
            track(em, entity, before, "Collider.AddShape", description, context, true);
        };
        if (ImGui::MenuItem("Box")) add_shape(ColliderComponent::Box({32.0f, 32.0f}).shapes.front(), "Add Box Collider Shape");
        if (ImGui::MenuItem("Circle")) add_shape(ColliderComponent::Circle(16.0f).shapes.front(), "Add Circle Collider Shape");
        if (ImGui::MenuItem("Polygon")) add_shape(ColliderComponent::Slope(32.0f, 32.0f).shapes.front(), "Add Polygon Collider Shape");
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void ColliderInspectorDrawer::draw_multiple(
    EntityManager& em,
    const std::vector<EntityID>& entities,
    const ComponentTransactionContext& context
) {
    const auto before = capture_selected_components<ColliderComponent>(em, entities);
    if (before.empty()) return;

    const auto header = InspectorWidgets::registered_component_header("collider", false);
    if (!header.open) return;
    ImGui::PushID("ColliderComponentDrawer");

    using namespace InspectorWidgets;

    // Topology Compatibility check:
    // 1. All must have at least 1 shape.
    // 2. All must have exactly 1 shape.
    // 3. All must have the same ColliderShapeType.
    bool compatible = true;
    for (const auto& pair : before) {
        if (pair.second.shapes.size() != 1) {
            compatible = false;
            break;
        }
    }
    if (compatible) {
        ColliderShapeType first_type = before.front().second.shapes.front().type;
        for (const auto& pair : before) {
            if (pair.second.shapes.front().type != first_type) {
                compatible = false;
                break;
            }
        }
    }

    if (!compatible) {
        ImGui::TextDisabled("Collider shapes are incompatible for bulk editing.");
        ImGui::TextDisabled("(All selected entities must have exactly 1 shape of the same type)");
        ImGui::PopID();
        return;
    }

    // Now all entities have exactly 1 shape of type `current_shape_type`
    ColliderShapeType current_shape_type = before.front().second.shapes.front().type;

    // Type
    const char* const shape_types[] = {"Box", "Circle", "Polygon"};
    int shape_type = static_cast<int>(current_shape_type);
    const bool type_mixed = selected_values_mixed<ColliderComponent, ColliderShapeType>(
        before, [](const ColliderComponent& c) { return c.shapes.front().type; }
    );
    const bool type_changed = property_enum("Shape Type", &shape_type, shape_types, 3, type_mixed);
    if (type_changed) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<ColliderComponent>(entity)) {
                auto& shape = em.get_component<ColliderComponent>(entity).shapes.front();
                shape.type = static_cast<ColliderShapeType>(shape_type);
                if (shape.type == ColliderShapeType::Polygon && shape.vertices.size() < 3) {
                    shape.vertices = {{-16.0f, -16.0f}, {16.0f, -16.0f}, {0.0f, 16.0f}};
                }
            }
        }
    }
    track_selected_component_edit<ColliderComponent>(
        em, entities, before, "Collider.Shape.0.Type", "Change Collider Shape Type",
        context, collider_equal, rebuild_selected_collider_physics, type_changed
    );

    if (current_shape_type == ColliderShapeType::Box) {
        float size[2] = {before.front().second.shapes.front().size.x, before.front().second.shapes.front().size.y};
        const bool mixed_x = selected_values_mixed<ColliderComponent, float>(
            before, [](const ColliderComponent& c) { return c.shapes.front().size.x; }
        );
        const bool mixed_y = selected_values_mixed<ColliderComponent, float>(
            before, [](const ColliderComponent& c) { return c.shapes.front().size.y; }
        );
        bool changed_x = false, changed_y = false;
        if (property_vec2("Size", size, InspectorPropertyPolicies::Collider::Size, false, mixed_x, mixed_y, &changed_x, &changed_y)) {
            for (const EntityID entity : entities) {
                if (em.is_alive(entity) && em.has_component<ColliderComponent>(entity)) {
                    auto& shape = em.get_component<ColliderComponent>(entity).shapes.front();
                    if (changed_x) shape.size.x = size[0];
                    if (changed_y) shape.size.y = size[1];
                }
            }
        }
        track_selected_component_edit<ColliderComponent>(
            em, entities, before, "Collider.Shape.0.Size", "Edit Collider Size",
            context, collider_equal, rebuild_selected_collider_physics
        );
    } else if (current_shape_type == ColliderShapeType::Circle) {
        float radius = before.front().second.shapes.front().radius;
        const bool radius_mixed = selected_values_mixed<ColliderComponent, float>(
            before, [](const ColliderComponent& c) { return c.shapes.front().radius; }
        );
        if (property_float("Radius", &radius, InspectorPropertyPolicies::Collider::Radius, radius_mixed)) {
            for (const EntityID entity : entities) {
                if (em.is_alive(entity) && em.has_component<ColliderComponent>(entity)) {
                    em.get_component<ColliderComponent>(entity).shapes.front().radius = radius;
                }
            }
        }
        track_selected_component_edit<ColliderComponent>(
            em, entities, before, "Collider.Shape.0.Radius", "Edit Collider Radius",
            context, collider_equal, rebuild_selected_collider_physics
        );
    }

    // Offset
    float offset[2] = {before.front().second.shapes.front().offset.x, before.front().second.shapes.front().offset.y};
    const bool offset_mixed_x = selected_values_mixed<ColliderComponent, float>(
        before, [](const ColliderComponent& c) { return c.shapes.front().offset.x; }
    );
    const bool offset_mixed_y = selected_values_mixed<ColliderComponent, float>(
        before, [](const ColliderComponent& c) { return c.shapes.front().offset.y; }
    );
    bool offset_changed_x = false, offset_changed_y = false;
    if (property_vec2("Offset", offset, InspectorPropertyPolicies::Collider::Coordinate, false, offset_mixed_x, offset_mixed_y, &offset_changed_x, &offset_changed_y)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<ColliderComponent>(entity)) {
                auto& shape = em.get_component<ColliderComponent>(entity).shapes.front();
                if (offset_changed_x) shape.offset.x = offset[0];
                if (offset_changed_y) shape.offset.y = offset[1];
            }
        }
    }
    track_selected_component_edit<ColliderComponent>(
        em, entities, before, "Collider.Shape.0.Offset", "Edit Collider Offset",
        context, collider_equal, rebuild_selected_collider_physics
    );

    // Is Trigger
    bool is_trigger = before.front().second.shapes.front().is_trigger;
    const bool trigger_mixed = selected_values_mixed<ColliderComponent, bool>(
        before, [](const ColliderComponent& c) { return c.shapes.front().is_trigger; }
    );
    const bool trigger_changed = property_bool("Is Trigger", &is_trigger, trigger_mixed);
    if (trigger_changed) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<ColliderComponent>(entity)) {
                em.get_component<ColliderComponent>(entity).shapes.front().is_trigger = is_trigger;
            }
        }
    }
    track_selected_component_edit<ColliderComponent>(
        em, entities, before, "Collider.Shape.0.IsTrigger", "Toggle Collider Trigger",
        context, collider_equal, rebuild_selected_collider_physics, trigger_changed
    );

    // Is One-Way
    bool is_one_way = before.front().second.shapes.front().is_one_way;
    const bool one_way_mixed = selected_values_mixed<ColliderComponent, bool>(
        before, [](const ColliderComponent& c) { return c.shapes.front().is_one_way; }
    );
    const bool one_way_changed = property_bool("Is One-Way", &is_one_way, one_way_mixed);
    if (one_way_changed) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<ColliderComponent>(entity)) {
                em.get_component<ColliderComponent>(entity).shapes.front().is_one_way = is_one_way;
            }
        }
    }
    track_selected_component_edit<ColliderComponent>(
        em, entities, before, "Collider.Shape.0.IsOneWay", "Toggle One-Way Collider",
        context, collider_equal, rebuild_selected_collider_physics, one_way_changed
    );

    // Category Bits
    int category = before.front().second.shapes.front().category_bits;
    const bool category_mixed = selected_values_mixed<ColliderComponent, std::uint16_t>(
        before, [](const ColliderComponent& c) { return c.shapes.front().category_bits; }
    );
    if (property_int("Category Bits", &category, InspectorPropertyPolicies::Collider::CollisionBits, category_mixed)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<ColliderComponent>(entity)) {
                em.get_component<ColliderComponent>(entity).shapes.front().category_bits = static_cast<std::uint16_t>(category);
            }
        }
    }
    track_selected_component_edit<ColliderComponent>(
        em, entities, before, "Collider.Shape.0.Category", "Edit Collider Category",
        context, collider_equal, rebuild_selected_collider_physics
    );

    // Mask Bits
    int mask = before.front().second.shapes.front().mask_bits;
    const bool mask_mixed = selected_values_mixed<ColliderComponent, std::uint16_t>(
        before, [](const ColliderComponent& c) { return c.shapes.front().mask_bits; }
    );
    if (property_int("Mask Bits", &mask, InspectorPropertyPolicies::Collider::CollisionBits, mask_mixed)) {
        for (const EntityID entity : entities) {
            if (em.is_alive(entity) && em.has_component<ColliderComponent>(entity)) {
                em.get_component<ColliderComponent>(entity).shapes.front().mask_bits = static_cast<std::uint16_t>(mask);
            }
        }
    }
    track_selected_component_edit<ColliderComponent>(
        em, entities, before, "Collider.Shape.0.Mask", "Edit Collider Mask",
        context, collider_equal, rebuild_selected_collider_physics
    );

    ImGui::PopID();
}

} // namespace Engine
