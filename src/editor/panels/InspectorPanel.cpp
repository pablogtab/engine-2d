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
#include "../inspector/TransformInspectorDrawer.hpp"
#include "../inspector/CameraInspectorDrawer.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
#include "../../ecs/CameraSystem.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../core/AssetManager.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../renderer/Texture.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

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

        std::vector<EntityID> selected_ids;
        selected_ids.reserve(selected_set.size());
        for (const EntityID entity : selected_set) {
            if (em.is_alive(entity)) selected_ids.push_back(entity);
        }
        std::sort(selected_ids.begin(), selected_ids.end());

        if (selected_ids.size() != selected_set.size()) {
            m_selection->set_selection(std::unordered_set<EntityID>(selected_ids.begin(), selected_ids.end()));
        }
        if (selected_ids.empty()) {
            ImGui::PopStyleVar(3);
            ImGui::End();
            return;
        }

        if (selected_ids.size() > 1) {
            draw_multi_components(em, selected_ids);
            ImGui::PopStyleVar(3);
            ImGui::End();
            return;
        }

        const EntityID selected_id = selected_ids.front();

        draw_components(em, selected_id);
        ImGui::PopStyleVar(3);
    }
    ImGui::End();
}

void InspectorPanel::draw_multi_components(EntityManager& em, const std::vector<EntityID>& entities) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.31f, 0.27f, 0.82f, 0.90f));
    ImGui::Button("E", ImVec2(28.0f, 28.0f));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%zu Entities", entities.size());
    ImGui::TextDisabled("Multiple selection");

    const ComponentTransactionContext transaction_context{
        m_command_history.get(),
        &m_property_transactions
    };
    const bool all_have_transform = std::all_of(entities.begin(), entities.end(), [&](EntityID entity) {
        return em.has_component<TransformComponent>(entity);
    });
    if (all_have_transform) {
        ImGui::SameLine();
        TransformInspectorDrawer::draw_entities_active(em, entities, transaction_context);
    }
    ImGui::Separator();

    if (all_have_transform) {
        const bool locked = std::any_of(entities.begin(), entities.end(), [&](EntityID entity) {
            return em.has_component<TilemapGeneratedColliderComponent>(entity);
        });
        TransformInspectorDrawer::draw_multiple(em, entities, locked, transaction_context);
    } else {
        ImGui::TextDisabled("Transform is not shared by every selected entity.");
    }

    const auto sprite_count = static_cast<std::size_t>(std::count_if(
        entities.begin(), entities.end(), [&](EntityID entity) {
            return em.has_component<SpriteComponent>(entity);
        }
    ));
    if (sprite_count == entities.size()) {
        SpriteInspectorDrawer::draw_multiple(em, entities, {
            m_renderer,
            m_asset_manager,
            m_command_history.get(),
            &m_property_transactions
        });
    } else if (sprite_count > 0) {
        ImGui::TextDisabled("Sprite Renderer is shared by %zu of %zu entities.", sprite_count, entities.size());
        ImGui::TextDisabled("Select entities with a common component to edit it in bulk.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    const std::string delete_label = editor_icon_label(EditorIcon::Trash, "Delete Selected");
    if (ImGui::Button(delete_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
        if (m_command_history) {
            m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(
                &em, m_selection, entities
            ));
        } else {
            for (const EntityID entity : entities) em.destroy_entity(entity);
            m_selection->clear();
        }
    }
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
            TransformInspectorDrawer::draw_entity_active(em, entity, {
                m_command_history.get(), &m_property_transactions
            });
        }
    } else {
        ImGui::TextDisabled("Entity #%u", entity);
    }

    ImGui::Separator();

    TransformInspectorDrawer::draw(em, entity, is_locked_tilemap_collider, {
        m_command_history.get(), &m_property_transactions
    });

    SpriteInspectorDrawer::draw(em, entity, {
        m_renderer,
        m_asset_manager,
        m_command_history.get(),
        &m_property_transactions
    });

    const ComponentTransactionContext component_context{
        m_command_history.get(),
        &m_property_transactions
    };
    RigidBodyInspectorDrawer::draw(em, entity, component_context);
    ColliderInspectorDrawer::draw(em, entity, component_context);

    TilemapInspectorDrawer::draw(em, entity, {
        m_renderer,
        m_asset_manager,
        m_command_history.get(),
        &m_property_transactions
    });

    CameraInspectorDrawer::draw(em, entity, component_context);

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
    const auto rebuild_transform = [](EntityManager& command_em, EntityID id) {
        TransformSystem::update(command_em);
        PhysicsSystem::rebuild_body_hierarchy(command_em, id);
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
                &em,
                entity,
                std::nullopt,
                TransformComponent{glm::vec3(0.0f)},
                "Add Transform",
                rebuild_transform
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
