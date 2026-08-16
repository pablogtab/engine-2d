#include "InspectorPanel.hpp"
#include "SceneHierarchyPanel.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../renderer/Texture.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"
#include <imgui.h>
#include <cstring>

namespace Engine {

InspectorPanel::InspectorPanel(SceneManager* scene_manager, std::shared_ptr<EditorSelection> selection, SDL_Renderer* renderer)
    : m_scene_manager(scene_manager), m_selection(selection), m_renderer(renderer) {}

void InspectorPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        if (!m_scene_manager || !m_selection) {
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

        const auto& selected_set = m_selection->get_selected();
        EntityManager& em = active_scene->get_entity_manager();

        if (selected_set.empty()) {
            ImGui::TextDisabled("No Entity Selected.");
            ImGui::Separator();
            ImGui::TextWrapped("Select an entity from the Scene Hierarchy panel or drag-select in the Viewport to inspect components.");
            ImGui::End();
            return;
        }

        if (selected_set.size() > 1) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Multi-Selection: %zu Entities", selected_set.size());
            ImGui::Separator();
            ImGui::TextWrapped("You have %zu entities selected. Use the Viewport Gizmo to translate them together as a group.", selected_set.size());
            ImGui::Spacing();
            if (ImGui::Button(" Delete All Selected ", ImVec2(180, 26))) {
                for (EntityID id : selected_set) {
                    em.destroy_entity(id);
                }
                m_selection->clear();
            }
            ImGui::End();
            return;
        }

        EntityID selected_id = *selected_set.begin();
        if (!em.is_alive(selected_id)) {
            m_selection->clear();
            ImGui::End();
            return;
        }

        draw_components(em, selected_id);
    }
    ImGui::End();
}

void InspectorPanel::draw_components(EntityManager& em, EntityID entity) {
    bool is_locked_tilemap_collider = false;
    if (em.has_component<TagComponent>(entity)) {
        const auto& tag = em.get_component<TagComponent>(entity).tag;
        if (tag.find("Tilemap_Composite_Collider") != std::string::npos ||
            tag.find("Tilemap_Collider") != std::string::npos) {
            is_locked_tilemap_collider = true;
        }
    }
    if (em.has_component<HierarchyComponent>(entity)) {
        EntityID p_id = em.get_component<HierarchyComponent>(entity).parent;
        if (p_id != 0 && em.is_alive(p_id) && em.has_component<TilemapComponent>(p_id)) {
            is_locked_tilemap_collider = true;
        }
    }

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
            if (is_locked_tilemap_collider) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.75f, 1.0f, 1.0f));
                ImGui::TextWrapped("🔒 Auto-generated Tilemap Collider (Managed by Tilemap)");
                ImGui::PopStyleColor();
                ImGui::TextDisabled("Transform is locked to Tilemap grid geometry.");
                ImGui::Spacing();
            }

            ImGui::BeginDisabled(is_locked_tilemap_collider);

            auto& tx = em.get_component<TransformComponent>(entity);

            // Position
            float pos[3] = {tx.position.x, tx.position.y, tx.position.z};
            if (ImGui::DragFloat3("Position", pos, 1.0f)) {
                tx.position = glm::vec3(pos[0], pos[1], pos[2]);
                tx.world_position = tx.position;
                PhysicsSystem::set_position(em, entity, {tx.position.x, tx.position.y});
            }
            ImGui::SameLine();
            if (ImGui::Button("R##pos")) {
                tx.position = glm::vec3(0.0f);
                tx.world_position = tx.position;
                PhysicsSystem::set_position(em, entity, {0.0f, 0.0f});
            }

            // Scale
            float scl[2] = {tx.scale.x, tx.scale.y};
            if (ImGui::DragFloat2("Scale", scl, 0.05f, 0.01f, 100.0f)) {
                tx.scale = glm::vec2(scl[0], scl[1]);
                tx.world_scale = tx.scale;
            }
            ImGui::SameLine();
            if (ImGui::Button("R##scl")) {
                tx.scale = glm::vec2(1.0f);
                tx.world_scale = tx.scale;
            }

            // Rotation
            if (ImGui::SliderFloat("Rotation", &tx.rotation, -360.0f, 360.0f, "%.1f deg")) {
                tx.world_rotation = tx.rotation;
            }
            ImGui::SameLine();
            if (ImGui::Button("R##rot")) {
                tx.rotation = 0.0f;
                tx.world_rotation = 0.0f;
            }

            // Visibility
            ImGui::Checkbox("Visible", &tx.visible);

            ImGui::EndDisabled();
        }
    }

    // --- 3. SpriteComponent ---
    if (em.has_component<SpriteComponent>(entity)) {
        if (ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sprite = em.get_component<SpriteComponent>(entity);

            // Ensure fallback texture if null
            if (!sprite.texture && m_renderer) {
                sprite.texture = Texture::create_solid(m_renderer, 8, 8, 255, 255, 255, 255);
            }

            // Texture Path
            std::string tex_path = sprite.texture ? sprite.texture->get_filepath() : "";
            char tex_buf[256];
            std::strncpy(tex_buf, tex_path.c_str(), sizeof(tex_buf) - 1);
            tex_buf[sizeof(tex_buf) - 1] = '\0';

            if (ImGui::InputText("Texture", tex_buf, sizeof(tex_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (std::strlen(tex_buf) > 0 && m_renderer) {
                    sprite.texture = Texture::load_from_file(m_renderer, tex_buf);
                }
            }

            // Quick Texture Presets
            ImGui::TextDisabled("Presets: ");
            ImGui::SameLine();
            if (ImGui::SmallButton("Player")) {
                if (m_renderer) sprite.texture = Texture::load_from_file(m_renderer, "assets/textures/player.png");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Knight")) {
                if (m_renderer) sprite.texture = Texture::load_from_file(m_renderer, "assets/textures/knight_sheet.png");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Solid")) {
                if (m_renderer) sprite.texture = Texture::create_solid(m_renderer, 8, 8, 255, 255, 255, 255);
            }

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
            ImGui::SliderFloat("Restitution", &rb.restitution, 0.0f, 1.0f);
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

    // --- 5. TilemapComponent ---
    if (em.has_component<TilemapComponent>(entity)) {
        if (ImGui::CollapsingHeader("Tilemap Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& tilemap = em.get_component<TilemapComponent>(entity);

            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Tile Count: %zu painted tiles", tilemap.tile_count());
            if (!tilemap.empty()) {
                int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
                tilemap.get_bounds(min_x, min_y, max_x, max_y);
                ImGui::TextDisabled("Bounds: [%d, %d] to [%d, %d] (%dx%d)", min_x, min_y, max_x, max_y, max_x - min_x + 1, max_y - min_y + 1);
            } else {
                ImGui::TextDisabled("Map is empty (Size: 0). Paint tiles in Viewport to expand.");
            }

            ImGui::DragFloat("Tile Size (px)", &tilemap.tile_size, 1.0f, 8.0f, 256.0f);
            ImGui::DragInt("Tile Pixel Size", &tilemap.tile_pixel_size, 1, 8, 128);

            char path_buf[256];
            snprintf(path_buf, sizeof(path_buf), "%s", tilemap.tileset_path.c_str());
            if (ImGui::InputText("Tileset Path", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                tilemap.tileset_path = path_buf;
                if (m_renderer) {
                    tilemap.tileset_texture = Texture::load_from_file(m_renderer, tilemap.tileset_path);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "COLLIDER & PHYSICS SETTINGS");

            bool needs_rebuild = false;

            if (ImGui::Checkbox("Generate Physics Colliders", &tilemap.generate_colliders)) {
                needs_rebuild = true;
            }

            if (tilemap.generate_colliders) {
                if (ImGui::Checkbox("Is Trigger (Sensor)", &tilemap.is_trigger)) {
                    needs_rebuild = true;
                }
                if (ImGui::Checkbox("One-Way Platform", &tilemap.is_one_way)) {
                    needs_rebuild = true;
                }

                if (ImGui::SliderFloat("Friction", &tilemap.friction, 0.0f, 1.0f, "%.2f")) {
                    needs_rebuild = true;
                }

                if (ImGui::SliderFloat("Restitution (Bounce)", &tilemap.restitution, 0.0f, 1.0f, "%.2f")) {
                    needs_rebuild = true;
                }

                // Layer / Category selector
                const char* categories[] = {"Default (0x1)", "Player (0x2)", "Ground / Platforms (0x4)", "Collectibles (0x8)", "Enemy / Hazard (0x10)"};
                uint16_t cat_values[] = {CollisionCategory::DEFAULT, CollisionCategory::PLAYER, CollisionCategory::GROUND, CollisionCategory::COIN, CollisionCategory::ENEMY};
                
                int current_cat_idx = 2; // Default to Ground
                for (int i = 0; i < 5; ++i) {
                    if (tilemap.category_bits == cat_values[i]) {
                        current_cat_idx = i;
                        break;
                    }
                }

                if (ImGui::Combo("Collision Layer", &current_cat_idx, categories, IM_ARRAYSIZE(categories))) {
                    tilemap.category_bits = cat_values[current_cat_idx];
                    needs_rebuild = true;
                }
            }

            if (needs_rebuild) {
                TilemapColliderSystem::build_composite_colliders(em, entity);
            }

            ImGui::Spacing();
            if (ImGui::Button("⚡ Rebuild Box2D Colliders", ImVec2(ImGui::GetContentRegionAvail().x, 24))) {
                TilemapColliderSystem::build_composite_colliders(em, entity);
            }

            if (ImGui::Button("🗑️ Clear Tilemap", ImVec2(ImGui::GetContentRegionAvail().x, 24))) {
                tilemap.clear();
                TilemapColliderSystem::build_composite_colliders(em, entity);
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
            Texture* tex = m_renderer ? Texture::create_solid(m_renderer, 8, 8, 255, 255, 255, 255) : nullptr;
            em.add_component<SpriteComponent>(entity, SpriteComponent{tex, glm::vec2(64.0f, 64.0f)});
        }
        if (!em.has_component<TilemapComponent>(entity) && ImGui::MenuItem("Tilemap Grid")) {
            em.add_component<TilemapComponent>(entity, TilemapComponent{});
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
