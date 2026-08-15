#include "InspectorPanel.hpp"
#include "SceneHierarchyPanel.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
#include <imgui.h>
#include <cstring>

namespace Engine {

InspectorPanel::InspectorPanel(SceneManager* scene_manager, SceneHierarchyPanel* hierarchy_panel)
    : m_scene_manager(scene_manager), m_hierarchy_panel(hierarchy_panel) {}

void InspectorPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        if (!m_scene_manager || !m_hierarchy_panel) {
            ImGui::TextDisabled("Inspector unavailable.");
            ImGui::End();
            return;
        }

        Scene* active_scene = m_scene_manager->get_active_scene();
        if (!active_scene) {
            ImGui::TextDisabled("No active scene.");
            ImGui::End();
            return;
        }

        EntityID selected_id = m_hierarchy_panel->get_selected_entity();
        EntityManager& em = active_scene->get_entity_manager();

        if (selected_id == 0 || !em.is_alive(selected_id)) {
            ImGui::TextDisabled("No Entity Selected.");
            ImGui::Separator();
            ImGui::TextWrapped("Select an entity from the Scene Hierarchy panel to inspect and edit its components.");
            ImGui::End();
            return;
        }

        draw_components(em, selected_id);
    }
    ImGui::End();
}

void InspectorPanel::draw_components(EntityManager& em, EntityID entity) {
    // --- 1. Header & TagComponent ---
    if (em.has_component<TagComponent>(entity)) {
        auto& tag_comp = em.get_component<TagComponent>(entity);
        char buffer[128];
        std::strncpy(buffer, tag_comp.tag.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        
        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "ENTITY ID: #%u", entity);
        if (ImGui::InputText("Tag", buffer, sizeof(buffer))) {
            tag_comp.tag = buffer;
        }
    } else {
        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "ENTITY ID: #%u", entity);
    }

    ImGui::Separator();

    // --- 2. TransformComponent ---
    if (em.has_component<TransformComponent>(entity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& tx = em.get_component<TransformComponent>(entity);

            // Position
            float pos[3] = {tx.position.x, tx.position.y, tx.position.z};
            if (ImGui::DragFloat3("Position", pos, 1.0f)) {
                tx.position = glm::vec3(pos[0], pos[1], pos[2]);
            }
            ImGui::SameLine();
            if (ImGui::Button("R##pos")) { tx.position = glm::vec3(0.0f); }

            // Scale
            float scl[2] = {tx.scale.x, tx.scale.y};
            if (ImGui::DragFloat2("Scale", scl, 0.05f, 0.01f, 100.0f)) {
                tx.scale = glm::vec2(scl[0], scl[1]);
            }
            ImGui::SameLine();
            if (ImGui::Button("R##scl")) { tx.scale = glm::vec2(1.0f); }

            // Rotation
            ImGui::SliderFloat("Rotation", &tx.rotation, -360.0f, 360.0f, "%.1f deg");
            ImGui::SameLine();
            if (ImGui::Button("R##rot")) { tx.rotation = 0.0f; }

            // Visibility
            ImGui::Checkbox("Visible", &tx.visible);
        }
    }

    // --- 3. SpriteComponent ---
    if (em.has_component<SpriteComponent>(entity)) {
        if (ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sprite = em.get_component<SpriteComponent>(entity);

            float sz[2] = {sprite.size.x, sprite.size.y};
            if (ImGui::DragFloat2("Size", sz, 1.0f, 1.0f, 2048.0f)) {
                sprite.size = glm::vec2(sz[0], sz[1]);
            }

            float col[4] = {
                sprite.color.r / 255.0f,
                sprite.color.g / 255.0f,
                sprite.color.b / 255.0f,
                sprite.color.a / 255.0f
            };
            if (ImGui::ColorEdit4("Tint Color", col)) {
                sprite.color.r = static_cast<uint8_t>(col[0] * 255.0f);
                sprite.color.g = static_cast<uint8_t>(col[1] * 255.0f);
                sprite.color.b = static_cast<uint8_t>(col[2] * 255.0f);
                sprite.color.a = static_cast<uint8_t>(col[3] * 255.0f);
            }

            ImGui::Checkbox("Flip X", &sprite.flip_x);
            ImGui::SameLine();
            ImGui::Checkbox("Flip Y", &sprite.flip_y);
            ImGui::SameLine();
            ImGui::Checkbox("Visible##sprite", &sprite.visible);
        }
    }

    // --- 4. RigidBodyComponent ---
    if (em.has_component<RigidBodyComponent>(entity)) {
        if (ImGui::CollapsingHeader("RigidBody 2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& rb = em.get_component<RigidBodyComponent>(entity);

            const char* body_types[] = { "Dynamic", "Static", "Kinematic" };
            int current_type = static_cast<int>(rb.body_type);
            if (ImGui::Combo("Body Type", &current_type, body_types, 3)) {
                rb.body_type = static_cast<BodyType>(current_type);
            }

            ImGui::DragFloat("Mass", &rb.mass, 0.1f, 0.01f, 1000.0f);
            ImGui::DragFloat("Gravity Scale", &rb.gravity_scale, 0.1f, -10.0f, 10.0f);
            ImGui::SliderFloat("Friction", &rb.friction, 0.0f, 1.0f);
            ImGui::SliderFloat("Restitution (Bounciness)", &rb.restitution, 0.0f, 1.0f);
            ImGui::Checkbox("Fixed Rotation", &rb.fixed_rotation);

            ImGui::Text("Velocity: (%.1f, %.1f)", rb.velocity.x, rb.velocity.y);
        }
    }

    // --- 5. ColliderComponent ---
    if (em.has_component<ColliderComponent>(entity)) {
        if (ImGui::CollapsingHeader("Collider 2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& col = em.get_component<ColliderComponent>(entity);
            for (size_t i = 0; i < col.shapes.size(); ++i) {
                auto& shape = col.shapes[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("Shape #%zu: %s", i + 1, shape.type == ColliderShapeType::Box ? "Box" : "Circle");
                
                if (shape.type == ColliderShapeType::Box) {
                    float sz[2] = {shape.size.x, shape.size.y};
                    if (ImGui::DragFloat2("Size", sz, 1.0f)) {
                        shape.size = glm::vec2(sz[0], sz[1]);
                    }
                } else if (shape.type == ColliderShapeType::Circle) {
                    ImGui::DragFloat("Radius", &shape.radius, 1.0f, 1.0f, 500.0f);
                }

                float off[2] = {shape.offset.x, shape.offset.y};
                if (ImGui::DragFloat2("Offset", off, 1.0f)) {
                    shape.offset = glm::vec2(off[0], off[1]);
                }

                ImGui::Checkbox("Is Trigger", &shape.is_trigger);
                ImGui::SameLine();
                ImGui::Checkbox("Is One-Way", &shape.is_one_way);
                ImGui::PopID();
                ImGui::Separator();
            }
        }
    }

    // --- Bottom Add Component Popup ---
    ImGui::Spacing();
    if (ImGui::Button(" + Add Component... ", ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!em.has_component<TransformComponent>(entity) && ImGui::MenuItem("Transform")) {
            em.add_component<TransformComponent>(entity, TransformComponent{glm::vec3(0.0f)});
        }
        if (!em.has_component<SpriteComponent>(entity) && ImGui::MenuItem("Sprite Renderer")) {
            em.add_component<SpriteComponent>(entity, SpriteComponent{nullptr, glm::vec2(64.0f, 64.0f)});
        }
        if (!em.has_component<RigidBodyComponent>(entity) && ImGui::MenuItem("RigidBody 2D")) {
            em.add_component<RigidBodyComponent>(entity, RigidBodyComponent::Dynamic());
        }
        if (!em.has_component<ColliderComponent>(entity) && ImGui::MenuItem("Collider 2D")) {
            em.add_component<ColliderComponent>(entity, ColliderComponent::Box(glm::vec2(64.0f, 64.0f)));
        }
        ImGui::EndPopup();
    }
}

} // namespace Engine
