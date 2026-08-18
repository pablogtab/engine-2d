#include "InspectorPanel.hpp"
#include "SceneHierarchyPanel.hpp"
#include "SpriteAnimationStudioPanel.hpp"
#include "../commands/EntityCommands.hpp"
#include "../inspector/InspectorWidgets.hpp"
#include "../inspector/SpriteInspectorDrawer.hpp"
#include "../inspector/RigidBodyInspectorDrawer.hpp"
#include "../inspector/ColliderInspectorDrawer.hpp"
#include "../inspector/TilemapInspectorDrawer.hpp"
#include "../inspector/AnimationInspectorDrawer.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
#include "../../ecs/CameraSystem.hpp"
#include "../../core/AssetManager.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../renderer/Texture.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"
#include <imgui.h>
#include <cfloat>
#include <cstring>
#include <utility>

namespace Engine {

using namespace InspectorWidgets;

InspectorPanel::InspectorPanel(
    SceneManager* scene_manager,
    std::shared_ptr<EditorSelection> selection,
    SDL_Renderer* renderer,
    AssetManager* asset_manager
) : m_scene_manager(scene_manager),
    m_selection(std::move(selection)),
    m_renderer(renderer),
    m_asset_manager(asset_manager ? asset_manager : &AssetManager::get()) {}

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
    const auto& interaction = last_property_interaction();
    if (interaction.activated) {
        m_pending_transform_edits.try_emplace(interaction.id, before);
    }

    if (interaction.cancelled) {
        auto pending = m_pending_transform_edits.find(interaction.id);
        if (pending != m_pending_transform_edits.end()) {
            em.get_component<TransformComponent>(entity) = pending->second;
            PhysicsSystem::set_position(
                em,
                entity,
                glm::vec2(pending->second.position.x, pending->second.position.y)
            );
            m_pending_transform_edits.erase(pending);
        }
        return;
    }

    if (interaction.deactivated_after_edit) {
        auto pending = m_pending_transform_edits.find(interaction.id);
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
    const auto& interaction = last_property_interaction();
    if (interaction.activated) {
        m_pending_camera_edits.try_emplace(interaction.id, before);
    }
    if (interaction.cancelled) {
        auto pending = m_pending_camera_edits.find(interaction.id);
        if (pending != m_pending_camera_edits.end()) {
            em.get_component<CameraComponent>(entity) = pending->second;
            m_pending_camera_edits.erase(pending);
        }
        return;
    }
    if (interaction.deactivated_after_edit) {
        auto pending = m_pending_camera_edits.find(interaction.id);
        if (pending != m_pending_camera_edits.end()) {
            push_camera_change(em, entity, pending->second, description);
            m_pending_camera_edits.erase(pending);
        }
    }
}

void InspectorPanel::track_tag_edit(
    EntityManager& em,
    EntityID entity,
    const std::string& before,
    const char* property,
    const char* description
) {
    if (!m_command_history) return;
    const auto& interaction = last_property_interaction();
    const auto key = EditorPropertyTransaction::make_key(entity, property);
    if (interaction.activated) {
        m_property_transactions.begin<std::string>(
            key,
            before,
            [&em, entity]() -> std::optional<std::string> {
                if (!em.is_alive(entity) || !em.has_component<TagComponent>(entity)) return std::nullopt;
                return em.get_component<TagComponent>(entity).tag;
            },
            [&em, entity](const std::string& value) {
                if (em.is_alive(entity) && em.has_component<TagComponent>(entity)) {
                    em.get_component<TagComponent>(entity).tag = value;
                }
            },
            description
        );
    }
    if (interaction.cancelled) {
        m_property_transactions.cancel(key);
    } else if (interaction.deactivated_after_edit) {
        m_property_transactions.commit(key, *m_command_history);
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
    const bool is_locked_tilemap_collider = em.has_component<TilemapGeneratedColliderComponent>(entity);

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
        const std::string tag_before = tag_comp.tag;
        begin_property_interaction("Entity.Name");
        if (ImGui::InputText("##entity_name", buffer, sizeof(buffer))) {
            tag_comp.tag = buffer;
        }
        capture_property_interaction();
        track_tag_edit(em, entity, tag_before, "Entity.Name", "Rename Entity");
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
        if (component_header("Transform", "TransformHeader", false, true, EditorIcon::Move).open) {
            if (is_locked_tilemap_collider) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.75f, 1.0f, 1.0f));
                const std::string locked_label = editor_icon_label(
                    EditorIcon::Lock, "Auto-generated Tilemap Collider (Managed by Tilemap)"
                );
                ImGui::TextWrapped("%s", locked_label.c_str());
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

    SpriteInspectorDrawer::draw(em, entity, {
        m_renderer,
        m_asset_manager,
        m_command_history.get(),
        &m_property_transactions
    });

    const ComponentTransactionContext physics_context{
        m_command_history.get(),
        &m_property_transactions
    };
    RigidBodyInspectorDrawer::draw(em, entity, physics_context);
    ColliderInspectorDrawer::draw(em, entity, physics_context);

    TilemapInspectorDrawer::draw(em, entity, {
        m_renderer,
        m_asset_manager,
        m_command_history.get(),
        &m_property_transactions
    });

    // --- 6. CameraComponent ---
    if (em.has_component<CameraComponent>(entity)) {
        const auto header = component_header("Camera 2D", "CameraHeader", true, true, EditorIcon::Camera);
        if (header.remove_requested && m_command_history) {
            m_command_history->execute_command(std::make_unique<SetComponentCommand<CameraComponent>>(
                &em,
                entity,
                em.get_component<CameraComponent>(entity),
                std::nullopt,
                "Remove Camera 2D"
            ));
        } else if (header.open) {
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

    AnimationInspectorDrawer::draw(em, entity, {
        m_command_history.get(),
        &m_property_transactions,
        &m_animation_preview,
        m_anim_studio
    });

    // --- Bottom Add Component Popup ---
    ImGui::Spacing();
    const auto rebuild_physics = [](EntityManager& command_em, EntityID id) {
        PhysicsSystem::rebuild_body(command_em, id);
    };
    const auto rebuild_tilemap = [](EntityManager& command_em, EntityID id) {
        if (command_em.has_component<TilemapComponent>(id)) {
            TilemapColliderSystem::build_composite_colliders(command_em, id);
        } else {
            TilemapColliderSystem::clear_composite_colliders(command_em, id);
        }
    };

    const std::string add_component_label = editor_icon_label(EditorIcon::Plus, "Add Component...");
    if (ImGui::Button(add_component_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!em.has_component<TransformComponent>(entity) && ImGui::MenuItem("Transform") && m_command_history) {
            m_command_history->execute_command(std::make_unique<SetComponentCommand<TransformComponent>>(
                &em, entity, std::nullopt, TransformComponent{glm::vec3(0.0f)}, "Add Transform", rebuild_physics
            ));
        }
        if (!em.has_component<SpriteComponent>(entity) && ImGui::MenuItem("Sprite Renderer") && m_command_history) {
            Texture* tex = m_renderer && m_asset_manager
                ? m_asset_manager->get_white_texture(m_renderer)
                : nullptr;
            m_command_history->execute_command(std::make_unique<SetComponentCommand<SpriteComponent>>(
                &em, entity, std::nullopt, SpriteComponent{tex, glm::vec2(64.0f, 64.0f)}, "Add Sprite Renderer"
            ));
        }
        if (!em.has_component<AnimationComponent>(entity) && ImGui::MenuItem("Sprite Animator 2D") && m_command_history) {
            AnimationComponent anim;
            anim.columns = 8;
            anim.rows = 3;
            anim.frame_width = 96.0f;
            anim.frame_height = 96.0f;
            anim.add_clip("Idle", 0, 8, 8.0f, true);
            anim.add_clip("Run", 8, 6, 11.0f, true);
            anim.add_clip("Jump", 16, 4, 8.0f, false);
            anim.add_clip("Fall", 20, 4, 8.0f, true);
            m_command_history->execute_command(std::make_unique<SetComponentCommand<AnimationComponent>>(
                &em,
                entity,
                std::nullopt,
                std::move(anim),
                "Add Sprite Animator 2D",
                [](EntityManager& command_em, EntityID id) {
                    SpriteAnimationSystem::reset_playback(command_em, id);
                }
            ));
        }
        if (!em.has_component<CameraComponent>(entity) && ImGui::MenuItem("Camera 2D") && m_command_history) {
            CameraComponent camera;
            camera.is_primary = !CameraSystem::has_camera(em);
            m_command_history->execute_command(std::make_unique<SetComponentCommand<CameraComponent>>(
                &em, entity, std::nullopt, camera, "Add Camera 2D"
            ));
        }
        if (!em.has_component<TilemapComponent>(entity) && ImGui::MenuItem("Tilemap Grid") && m_command_history) {
            m_command_history->execute_command(std::make_unique<SetComponentCommand<TilemapComponent>>(
                &em, entity, std::nullopt, TilemapComponent{}, "Add Tilemap Grid", rebuild_tilemap
            ));
        }
        if (!em.has_component<RigidBodyComponent>(entity) && ImGui::MenuItem("RigidBody 2D") && m_command_history) {
            m_command_history->execute_command(std::make_unique<SetComponentCommand<RigidBodyComponent>>(
                &em, entity, std::nullopt, RigidBodyComponent::Dynamic(), "Add RigidBody 2D", rebuild_physics
            ));
        }
        if (!em.has_component<ColliderComponent>(entity) && ImGui::MenuItem("Collider 2D") && m_command_history) {
            m_command_history->execute_command(std::make_unique<SetComponentCommand<ColliderComponent>>(
                &em, entity, std::nullopt, ColliderComponent::Box(glm::vec2(64.0f, 64.0f)), "Add Collider 2D", rebuild_physics
            ));
        }
        ImGui::EndPopup();
    }

}

} // namespace Engine
