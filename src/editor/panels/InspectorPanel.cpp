#include "InspectorPanel.hpp"
#include "SceneHierarchyPanel.hpp"
#include "SpriteAnimationStudioPanel.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
#include "../../ecs/CameraSystem.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../renderer/Texture.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"
#include <imgui.h>
#include <cfloat>
#include <cstring>

namespace Engine {

namespace {

constexpr float PROPERTY_LABEL_WIDTH = 104.0f;
constexpr float PROPERTY_GAP = 6.0f;
constexpr float RESET_BUTTON_WIDTH = 28.0f;

struct PropertyInteraction {
    ImGuiID id{0};
    bool activated{false};
    bool deactivated_after_edit{false};
};

PropertyInteraction g_property_interaction;

void begin_property_interaction(const char* label) {
    g_property_interaction = {ImGui::GetID(label), false, false};
}

void capture_property_interaction() {
    g_property_interaction.activated |= ImGui::IsItemActivated();
    g_property_interaction.deactivated_after_edit |= ImGui::IsItemDeactivatedAfterEdit();
}

void begin_property_row(const char* label, bool reserve_reset = false) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::SameLine(PROPERTY_LABEL_WIDTH);
    const float reset_space = reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reset_space);
}

bool property_float(const char* label, float* value, float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f") {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const bool changed = ImGui::DragFloat("##value", value, speed, min, max, format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_axis_float(const char* label, const char* axis, float* value, float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool reserve_reset = false) {
    begin_property_row(label, reserve_reset);
    begin_property_interaction(label);
    ImGui::PushID(label);
    ImGui::TextDisabled("%s", axis);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    const float reset_space = reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f;
    // A scalar Z value shares the vector grid but only needs half of the
    // unused Y-axis gutter before the reset control.
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reset_space - 11.0f);
    const bool changed = ImGui::DragFloat("##value", value, speed, min, max, format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_int(const char* label, int* value, float speed, int min = 0, int max = 0, const char* format = "%d") {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const bool changed = ImGui::DragInt("##value", value, speed, min, max, format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_bool(const char* label, bool* value) {
    begin_property_row(label);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const bool changed = ImGui::Checkbox("##value", value);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_vec2(const char* label, float values[2], float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool reserve_reset = false) {
    begin_property_row(label, reserve_reset);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - (reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f) - spacing * 3.0f - 24.0f) * 0.5f;
    ImGui::TextDisabled("X");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::DragFloat("##x", &values[0], speed, min, max, format);
    capture_property_interaction();
    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("Y");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat("##y", &values[1], speed, min, max, format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_vec3(const char* label, float values[3], float speed, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", bool reserve_reset = false) {
    begin_property_row(label, reserve_reset);
    begin_property_interaction(label);
    ImGui::PushID(label);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - (reserve_reset ? RESET_BUTTON_WIDTH + PROPERTY_GAP : 0.0f) - spacing * 5.0f - 36.0f) / 3.0f;
    ImGui::TextDisabled("X");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::DragFloat("##x", &values[0], speed, min, max, format);
    capture_property_interaction();
    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("Y");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat("##y", &values[1], speed, min, max, format);
    capture_property_interaction();
    ImGui::SameLine(0.0f, spacing);
    ImGui::TextDisabled("Z");
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat("##z", &values[2], speed, min, max, format);
    capture_property_interaction();
    ImGui::PopID();
    return changed;
}

bool property_reset_button(const char* id) {
    ImGui::SameLine(0.0f, PROPERTY_GAP);
    return ImGui::Button(id, ImVec2(RESET_BUTTON_WIDTH, 0.0f));
}

} // namespace

InspectorPanel::InspectorPanel(SceneManager* scene_manager, std::shared_ptr<EditorSelection> selection, SDL_Renderer* renderer)
    : m_scene_manager(scene_manager), m_selection(selection), m_renderer(renderer) {}

void InspectorPanel::push_transform_change(EntityManager& em, EntityID entity, const TransformComponent& before, const char* description) {
    if (!m_command_history || !em.is_alive(entity) || !em.has_component<TransformComponent>(entity)) return;

    const TransformComponent after = em.get_component<TransformComponent>(entity);
    m_command_history->push_executed_command(std::make_unique<ModifyEntityCommand>(
        &em,
        entity,
        [before](EntityManager& command_em, EntityID id) {
            if (!command_em.has_component<TransformComponent>(id)) return;
            command_em.get_component<TransformComponent>(id) = before;
            PhysicsSystem::set_position(command_em, id, glm::vec2(before.position.x, before.position.y));
        },
        [after](EntityManager& command_em, EntityID id) {
            if (!command_em.has_component<TransformComponent>(id)) return;
            command_em.get_component<TransformComponent>(id) = after;
            PhysicsSystem::set_position(command_em, id, glm::vec2(after.position.x, after.position.y));
        },
        description
    ));
}

void InspectorPanel::track_transform_edit(EntityManager& em, EntityID entity, const TransformComponent& before, const char* description) {
    if (g_property_interaction.activated) {
        m_pending_transform_edits.try_emplace(g_property_interaction.id, before);
    }

    if (g_property_interaction.deactivated_after_edit) {
        auto pending = m_pending_transform_edits.find(g_property_interaction.id);
        if (pending != m_pending_transform_edits.end()) {
            push_transform_change(em, entity, pending->second, description);
            m_pending_transform_edits.erase(pending);
        }
    }
}

void InspectorPanel::push_camera_change(EntityManager& em, EntityID entity, const CameraComponent& before, const char* description) {
    if (!m_command_history || !em.is_alive(entity) || !em.has_component<CameraComponent>(entity)) return;
    const CameraComponent after = em.get_component<CameraComponent>(entity);
    if (before == after) return;
    m_command_history->push_executed_command(std::make_unique<ModifyEntityCommand>(
        &em,
        entity,
        [before](EntityManager& command_em, EntityID id) {
            if (command_em.has_component<CameraComponent>(id)) command_em.get_component<CameraComponent>(id) = before;
        },
        [after](EntityManager& command_em, EntityID id) {
            if (command_em.has_component<CameraComponent>(id)) command_em.get_component<CameraComponent>(id) = after;
        },
        description
    ));
}

void InspectorPanel::track_camera_edit(EntityManager& em, EntityID entity, const CameraComponent& before, const char* description) {
    if (g_property_interaction.activated) {
        m_pending_camera_edits.try_emplace(g_property_interaction.id, before);
    }
    if (g_property_interaction.deactivated_after_edit) {
        auto pending = m_pending_camera_edits.find(g_property_interaction.id);
        if (pending != m_pending_camera_edits.end()) {
            push_camera_change(em, entity, pending->second, description);
            m_pending_camera_edits.erase(pending);
        }
    }
}

void InspectorPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
        if (!m_scene_manager || !m_selection) {
            ImGui::TextDisabled("Inspector unavailable.");
            ImGui::PopStyleVar(3);
            ImGui::End();
            return;
        }

        Scene* active_scene = m_scene_manager->get_active_scene();
        if (!active_scene) {
            ImGui::TextDisabled("No active scene.");
            ImGui::PopStyleVar(3);
            ImGui::End();
            return;
        }

        const auto& selected_set = m_selection->get_selected();
        EntityManager& em = active_scene->get_entity_manager();

        if (selected_set.empty()) {
            ImGui::TextDisabled("No Entity Selected.");
            ImGui::Separator();
            ImGui::TextWrapped("Select an entity from the Scene Hierarchy panel or drag-select in the Viewport to inspect components.");
            ImGui::PopStyleVar(3);
            ImGui::End();
            return;
        }

        if (selected_set.size() > 1) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Multi-Selection: %zu Entities", selected_set.size());
            ImGui::Separator();
            ImGui::TextWrapped("You have %zu entities selected. Use the Viewport Gizmo to translate them together as a group.", selected_set.size());
            ImGui::Spacing();
            if (ImGui::Button(" Delete All Selected ", ImVec2(180, 26))) {
                std::vector<EntityID> ids(selected_set.begin(), selected_set.end());
                if (m_command_history) {
                    m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, std::move(ids)));
                } else {
                    for (EntityID id : ids) em.destroy_entity(id);
                    m_selection->clear();
                }
            }
            ImGui::PopStyleVar(3);
            ImGui::End();
            return;
        }

        EntityID selected_id = *selected_set.begin();
        if (!em.is_alive(selected_id)) {
            m_selection->clear();
            ImGui::PopStyleVar(3);
            ImGui::End();
            return;
        }

        draw_components(em, selected_id);
        ImGui::PopStyleVar(3);
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

    // --- 1. Object Header (Unity-style entity context) ---
    if (em.has_component<TagComponent>(entity)) {
        auto& tag_comp = em.get_component<TagComponent>(entity);
        char buffer[128];
        std::strncpy(buffer, tag_comp.tag.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.31f, 0.27f, 0.82f, 0.90f));
        ImGui::Button("E", ImVec2(28.0f, 28.0f));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##entity_name", buffer, sizeof(buffer))) {
            tag_comp.tag = buffer;
        }
        ImGui::TextDisabled("Entity #%u", entity);

        if (em.has_component<TransformComponent>(entity)) {
            ImGui::SameLine();
            auto& transform = em.get_component<TransformComponent>(entity);
            const TransformComponent active_before = transform;
            begin_property_interaction("Entity Active");
            ImGui::Checkbox("Active", &transform.visible);
            capture_property_interaction();
            track_transform_edit(em, entity, active_before, "Edit Entity Active State");
        }
    } else {
        ImGui::TextDisabled("Entity #%u", entity);
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
            const bool has_parent = em.has_component<HierarchyComponent>(entity) &&
                em.get_component<HierarchyComponent>(entity).parent != 0 &&
                em.is_alive(em.get_component<HierarchyComponent>(entity).parent);
            if (has_parent) {
                const EntityID parent_id = em.get_component<HierarchyComponent>(entity).parent;
                const char* parent_name = em.has_component<TagComponent>(parent_id)
                    ? em.get_component<TagComponent>(parent_id).tag.c_str() : "Entity";
                ImGui::TextDisabled("Local transform relative to: %s", parent_name);
            }

            // Position
            float pos[2] = {tx.position.x, tx.position.y};
            const TransformComponent position_before = tx;
            if (property_vec2("Position", pos, 1.0f, 0.0f, 0.0f, "%.3f", true)) {
                tx.position.x = pos[0];
                tx.position.y = pos[1];
                tx.world_position = tx.position;
                PhysicsSystem::set_position(em, entity, {tx.position.x, tx.position.y});
            }
            track_transform_edit(em, entity, position_before, "Edit Transform Position");
            if (property_reset_button("R##pos")) {
                const TransformComponent before = tx;
                tx.position = glm::vec3(0.0f);
                tx.world_position = tx.position;
                PhysicsSystem::set_position(em, entity, {0.0f, 0.0f});
                push_transform_change(em, entity, before, "Reset Transform Position");
            }

            const TransformComponent depth_before = tx;
            if (property_axis_float("Z Depth", "Z", &tx.position.z, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
                tx.world_position.z = tx.position.z;
            }
            track_transform_edit(em, entity, depth_before, "Edit Transform Z Depth");

            // Scale
            float scl[2] = {tx.scale.x, tx.scale.y};
            const TransformComponent scale_before = tx;
            if (property_vec2("Scale", scl, 0.05f, 0.01f, 100.0f, "%.3f", true)) {
                tx.scale = glm::vec2(scl[0], scl[1]);
                tx.world_scale = tx.scale;
            }
            track_transform_edit(em, entity, scale_before, "Edit Transform Scale");
            if (property_reset_button("R##scl")) {
                const TransformComponent before = tx;
                tx.scale = glm::vec2(1.0f);
                tx.world_scale = tx.scale;
                push_transform_change(em, entity, before, "Reset Transform Scale");
            }

            // Rotation
            const TransformComponent rotation_before = tx;
            if (property_axis_float("Rotation", "Z", &tx.rotation, 0.5f, -360.0f, 360.0f, "%.1f deg", true)) {
                tx.world_rotation = tx.rotation;
            }
            track_transform_edit(em, entity, rotation_before, "Edit Transform Rotation");
            if (property_reset_button("R##rot")) {
                const TransformComponent before = tx;
                tx.rotation = 0.0f;
                tx.world_rotation = 0.0f;
                push_transform_change(em, entity, before, "Reset Transform Rotation");
            }

            const TransformComponent pivot_before = tx;
            float pivot[2] = {tx.pivot.x, tx.pivot.y};
            if (property_vec2("Pivot", pivot, 0.01f, 0.0f, 1.0f, "%.2f")) {
                tx.pivot = glm::vec2(pivot[0], pivot[1]);
                tx.world_pivot = tx.pivot;
            }
            track_transform_edit(em, entity, pivot_before, "Edit Transform Pivot");

            // Visibility
            const TransformComponent visibility_before = tx;
            property_bool("Visible", &tx.visible);
            track_transform_edit(em, entity, visibility_before, "Edit Transform Visibility");

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

            if (sprite.texture && sprite.texture->get_sdl_texture()) {
                const float max_preview_size = 112.0f;
                const float texture_width = static_cast<float>(std::max(1, sprite.texture->get_width()));
                const float texture_height = static_cast<float>(std::max(1, sprite.texture->get_height()));
                const float scale = std::min(max_preview_size / texture_width, max_preview_size / texture_height);
                const ImVec2 preview_size(texture_width * scale, texture_height * scale);

                begin_property_row("Preview");
                ImGui::Image(sprite.texture->get_sdl_texture(), preview_size);
                ImGui::SameLine();
                ImGui::TextDisabled("%d x %d", sprite.texture->get_width(), sprite.texture->get_height());
            }

            float sz[2] = {sprite.size.x, sprite.size.y};
            if (property_vec2("Size", sz, 1.0f, 1.0f, 2048.0f)) {
                sprite.size = glm::vec2(sz[0], sz[1]);
            }

            float col[3] = {
                sprite.color.r / 255.0f,
                sprite.color.g / 255.0f,
                sprite.color.b / 255.0f
            };
            if (ImGui::ColorEdit3("Tint Color", col)) {
                sprite.color.r = static_cast<uint8_t>(col[0] * 255.0f);
                sprite.color.g = static_cast<uint8_t>(col[1] * 255.0f);
                sprite.color.b = static_cast<uint8_t>(col[2] * 255.0f);
            }

            float opacity = sprite.color.a / 255.0f * 100.0f;
            if (property_float("Opacity", &opacity, 0.5f, 0.0f, 100.0f, "%.0f%%")) {
                sprite.color.a = static_cast<uint8_t>(opacity / 100.0f * 255.0f);
            }
            int sorting_layer = sprite.sorting_layer;
            if (property_int("Sorting Layer", &sorting_layer, 1.0f, -32768, 32767)) {
                sprite.sorting_layer = static_cast<int16_t>(sorting_layer);
            }
            int order_in_layer = sprite.order_in_layer;
            if (property_int("Order in Layer", &order_in_layer, 1.0f, -32768, 32767)) {
                sprite.order_in_layer = static_cast<int16_t>(order_in_layer);
            }

            begin_property_row("Flags");
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
            begin_property_row("Body Type");
            ImGui::PushID("body_type");
            if (ImGui::Combo("##value", &current_type, body_types, 3)) {
                rb.body_type = static_cast<BodyType>(current_type);
            }
            ImGui::PopID();

            property_float("Mass", &rb.mass, 0.1f, 0.01f, 1000.0f);
            property_float("Gravity Scale", &rb.gravity_scale, 0.1f, -10.0f, 10.0f);
            property_float("Friction", &rb.friction, 0.01f, 0.0f, 1.0f);
            property_float("Restitution", &rb.restitution, 0.01f, 0.0f, 1.0f);
            begin_property_row("Fixed Rotation");
            ImGui::Checkbox("##fixed_rotation", &rb.fixed_rotation);

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
                    if (property_vec2("Size", sz, 1.0f)) {
                        shape.size = glm::vec2(sz[0], sz[1]);
                    }
                } else if (shape.type == ColliderShapeType::Circle) {
                    property_float("Radius", &shape.radius, 1.0f, 1.0f, 500.0f);
                }

                float off[2] = {shape.offset.x, shape.offset.y};
                if (property_vec2("Offset", off, 1.0f)) {
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

            property_float("Tile Size (px)", &tilemap.tile_size, 1.0f, 8.0f, 256.0f);
            property_int("Tile Pixel Size", &tilemap.tile_pixel_size, 1.0f, 8, 128);

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

                if (property_float("Friction", &tilemap.friction, 0.01f, 0.0f, 1.0f, "%.2f")) {
                    needs_rebuild = true;
                }

                if (property_float("Restitution (Bounce)", &tilemap.restitution, 0.01f, 0.0f, 1.0f, "%.2f")) {
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

    // --- 6. CameraComponent ---
    if (em.has_component<CameraComponent>(entity)) {
        if (ImGui::CollapsingHeader("🎥 Camera 2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& cam = em.get_component<CameraComponent>(entity);

            if (cam.is_primary) {
                ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Primary Scene Camera");
            } else if (ImGui::Button("Make Primary", ImVec2(-1.0f, 0.0f)) && m_command_history) {
                m_command_history->execute_command(std::make_unique<SetPrimaryCameraCommand>(&em, entity));
            }

            const CameraComponent zoom_before = cam;
            property_float("Camera Zoom", &cam.zoom, 0.02f, 0.1f, 20.0f, "%.2fx");
            track_camera_edit(em, entity, zoom_before, "Edit Camera Zoom");

            const char* projection_labels[] = {"Fixed Height", "Fixed Width", "Expand", "Pixel Perfect"};
            int projection = static_cast<int>(cam.projection);
            begin_property_row("Projection");
            ImGui::PushID("camera_projection");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##value", &projection, projection_labels, IM_ARRAYSIZE(projection_labels))) {
                const CameraComponent before = cam;
                cam.projection = static_cast<CameraProjectionMode>(projection);
                push_camera_change(em, entity, before, "Change Camera Projection");
            }
            ImGui::PopID();

            const char* target_preview = "None";
            if (cam.follow_target != 0 && em.is_alive(cam.follow_target)) {
                target_preview = em.has_component<TagComponent>(cam.follow_target)
                    ? em.get_component<TagComponent>(cam.follow_target).tag.c_str()
                    : "Entity";
            }
            begin_property_row("Follow Target");
            ImGui::PushID("camera_follow_target");
            if (ImGui::BeginCombo("##value", target_preview)) {
                if (ImGui::Selectable("None", cam.follow_target == 0)) {
                    const CameraComponent before = cam;
                    cam.follow_target = 0;
                    push_camera_change(em, entity, before, "Change Camera Follow Target");
                }
                for (EntityID candidate : em.get_living_entities()) {
                    if (candidate == entity || !em.has_component<TransformComponent>(candidate)) continue;
                    const std::string label = em.has_component<TagComponent>(candidate)
                        ? em.get_component<TagComponent>(candidate).tag
                        : "Entity #" + std::to_string(candidate);
                    const std::string item_label = label + "##" + std::to_string(candidate);
                    if (ImGui::Selectable(item_label.c_str(), cam.follow_target == candidate)) {
                        const CameraComponent before = cam;
                        cam.follow_target = candidate;
                        push_camera_change(em, entity, before, "Change Camera Follow Target");
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();

            if (cam.follow_target != 0) {
                const CameraComponent offset_before = cam;
                property_vec2("Follow Offset", &cam.follow_offset.x, 1.0f);
                track_camera_edit(em, entity, offset_before, "Edit Camera Follow Offset");
                const CameraComponent speed_before = cam;
                property_float("Follow Speed", &cam.follow_speed, 0.1f, 0.1f, 20.0f, "%.1f");
                track_camera_edit(em, entity, speed_before, "Edit Camera Follow Speed");
            }

            const CameraComponent bounds_enabled_before = cam;
            property_bool("Enable Bounds", &cam.enable_bounds);
            track_camera_edit(em, entity, bounds_enabled_before, "Toggle Camera Bounds");
            if (cam.enable_bounds) {
                const CameraComponent bounds_before = cam;
                begin_property_row("Bounds");
                begin_property_interaction("Camera Bounds");
                ImGui::PushID("camera_bounds");
                ImGui::DragFloat4("##value", &cam.bounds.x, 10.0f);
                capture_property_interaction();
                ImGui::PopID();
                track_camera_edit(em, entity, bounds_before, "Edit Camera Bounds");
            }
            ImGui::TextDisabled("Output resolution is configured in Project Settings");
        }
    }

    // --- 7. AnimationComponent (Sprite Animator) ---
    if (em.has_component<AnimationComponent>(entity)) {
        if (ImGui::CollapsingHeader("🎬 Sprite Animator 2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& anim = em.get_component<AnimationComponent>(entity);

            if (m_anim_studio) {
                if (ImGui::Button("🎬 Open in Animation Studio ↗", ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
                    m_anim_studio->open_for_entity(entity);
                }
                ImGui::Spacing();
            }

            ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "SPRITESHEET GRID");
            ImGui::DragInt("Columns", &anim.columns, 1.0f, 1, 64);
            ImGui::DragInt("Rows", &anim.rows, 1.0f, 1, 64);
            ImGui::DragFloat("Frame Width", &anim.frame_width, 1.0f, 4.0f, 1024.0f, "%.0f px");
            ImGui::DragFloat("Frame Height", &anim.frame_height, 1.0f, 4.0f, 1024.0f, "%.0f px");
            ImGui::DragFloat("Spacing", &anim.spacing_x, 1.0f, 0.0f, 64.0f, "%.0f px");
            anim.spacing_y = anim.spacing_x;

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "CLIPS & PLAYBACK");

            if (!anim.clips.empty()) {
                // Combo of available clips
                std::vector<const char*> clip_names;
                for (const auto& c : anim.clips) clip_names.push_back(c.name.c_str());

                int sel_idx = anim.active_clip_index;
                if (sel_idx < 0 || sel_idx >= (int)anim.clips.size()) sel_idx = 0;

                if (ImGui::Combo("Active Clip", &sel_idx, clip_names.data(), (int)clip_names.size())) {
                    anim.active_clip_index = sel_idx;
                    anim.current_frame = anim.clips[sel_idx].start_frame;
                    anim.timer = 0.0f;
                    anim.is_finished = false;
                }

                auto* active_clip = anim.get_active_clip();
                if (active_clip) {
                    // Live playback in edit mode when Play is active
                    if (anim.is_playing) {
                        float dt = ImGui::GetIO().DeltaTime;
                        anim.timer += dt;
                        float frame_duration = 1.0f / (active_clip->fps > 0.1f ? active_clip->fps : 10.0f);

                        while (anim.timer >= frame_duration && anim.is_playing) {
                            anim.timer -= frame_duration;
                            int total = active_clip->get_total_frames();
                            if (total > 0) {
                                int rel_frame = active_clip->custom_frames.empty() ? (anim.current_frame - active_clip->start_frame) : anim.current_frame;
                                rel_frame++;

                                if (rel_frame >= total) {
                                    if (active_clip->loop) {
                                        rel_frame = 0;
                                    } else {
                                        rel_frame = total - 1;
                                        anim.is_playing = false;
                                        anim.is_finished = true;
                                    }
                                }
                                anim.current_frame = active_clip->custom_frames.empty() ? (active_clip->start_frame + rel_frame) : rel_frame;
                            }
                        }

                        if (em.has_component<SpriteComponent>(entity)) {
                            auto& sprite = em.get_component<SpriteComponent>(entity);
                            sprite.src_rect = anim.get_frame_rect(anim.current_frame, active_clip);
                            sprite.use_src_rect = true;
                        }
                    }

                    ImGui::SliderFloat("Speed (FPS)", &active_clip->fps, 1.0f, 60.0f, "%.0f FPS");
                    ImGui::Checkbox("Loop Clip", &active_clip->loop);
                    ImGui::TextDisabled("Frame: %d | Total Frames: %d", anim.current_frame, active_clip->get_total_frames());

                    if (anim.is_playing) {
                        if (ImGui::Button("⏸ Pause", ImVec2(90, 24))) anim.is_playing = false;
                    } else {
                        if (ImGui::Button("▶ Play", ImVec2(90, 24))) anim.is_playing = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("⏹ Reset", ImVec2(90, 24))) {
                        anim.current_frame = active_clip->start_frame;
                        anim.timer = 0.0f;
                        anim.is_finished = false;
                    }
                }
            } else {
                ImGui::TextDisabled("No animation clips created yet.");
            }

            ImGui::Spacing();
            if (ImGui::TreeNode("Clip Management")) {
                int clip_to_remove = -1;
                for (size_t i = 0; i < anim.clips.size(); ++i) {
                    auto& c = anim.clips[i];
                    ImGui::PushID(static_cast<int>(i));

                    char name_buf[64];
                    strncpy(name_buf, c.name.c_str(), sizeof(name_buf));
                    name_buf[sizeof(name_buf) - 1] = '\0';
                    if (ImGui::InputText("##Name", name_buf, sizeof(name_buf))) {
                        c.name = name_buf;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(60);
                    ImGui::DragInt("Start", &c.start_frame, 1.0f, 0, anim.columns * anim.rows - 1);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(60);
                    ImGui::DragInt("Count", &c.frame_count, 1.0f, 1, anim.columns * anim.rows);
                    ImGui::SameLine();
                    if (ImGui::Button("🗑")) {
                        clip_to_remove = static_cast<int>(i);
                    }
                    ImGui::PopID();
                }

                if (clip_to_remove >= 0 && clip_to_remove < (int)anim.clips.size()) {
                    anim.clips.erase(anim.clips.begin() + clip_to_remove);
                    if (anim.active_clip_index >= (int)anim.clips.size()) {
                        anim.active_clip_index = std::max(0, (int)anim.clips.size() - 1);
                    }
                }

                if (ImGui::Button("+ Add Clip", ImVec2(100, 22))) {
                    anim.add_clip("NewClip", 0, 4, 10.0f, true);
                }
                ImGui::TreePop();
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
        if (!em.has_component<AnimationComponent>(entity) && ImGui::MenuItem("Sprite Animator 2D")) {
            AnimationComponent anim;
            anim.columns = 8;
            anim.rows = 3;
            anim.frame_width = 96.0f;
            anim.frame_height = 96.0f;
            anim.add_clip("Idle", 0, 8, 8.0f, true);
            anim.add_clip("Run", 8, 6, 11.0f, true);
            anim.add_clip("Jump", 16, 4, 8.0f, false);
            anim.add_clip("Fall", 20, 4, 8.0f, true);
            em.add_component<AnimationComponent>(entity, anim);
        }
        if (!em.has_component<CameraComponent>(entity) && ImGui::MenuItem("Camera 2D")) {
            CameraComponent camera;
            camera.is_primary = !CameraSystem::has_camera(em);
            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<ModifyEntityCommand>(
                    &em,
                    entity,
                    [](EntityManager& command_em, EntityID id) {
                        if (command_em.has_component<CameraComponent>(id)) command_em.remove_component<CameraComponent>(id);
                    },
                    [camera](EntityManager& command_em, EntityID id) {
                        if (!command_em.has_component<CameraComponent>(id)) command_em.add_component<CameraComponent>(id, camera);
                    },
                    "Add Camera 2D"
                ));
            }
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
