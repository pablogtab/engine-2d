#include "ColliderInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../ecs/Components.hpp"
#include "../../physics/PhysicsSystem.hpp"
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

    const auto header = InspectorWidgets::component_header(
        "Collider 2D", "ColliderHeader", true, true, EditorIcon::Collider
    );
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
}

} // namespace Engine
