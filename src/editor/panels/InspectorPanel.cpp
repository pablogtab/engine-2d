#include "InspectorPanel.hpp"
#include "SceneHierarchyPanel.hpp"
#include "SpriteAnimationStudioPanel.hpp"
#include "../stage/PrefabStageController.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../commands/EditorEntityStateCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../inspector/InspectorWidgets.hpp"
#include "../inspector/MultiComponentInspectorTransaction.hpp"
#include "../inspector/SpriteInspectorDrawer.hpp"
#include "../inspector/RigidBodyInspectorDrawer.hpp"
#include "../inspector/ColliderInspectorDrawer.hpp"
#include "../inspector/TilemapInspectorDrawer.hpp"
#include "../inspector/AnimationInspectorDrawer.hpp"
#include "../inspector/TransformInspectorDrawer.hpp"
#include "../inspector/CameraInspectorDrawer.hpp"
#include "../inspector/EditorComponentRegistry.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../core/AssetManager.hpp"
#include "../../scene/ComponentRegistry.hpp"
#include "../../scene/PrefabSerializer.hpp"
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

    const bool any_editor_locked = m_entity_state_store && std::any_of(entities.begin(), entities.end(), [&](EntityID e) {
        return m_entity_state_store->is_locked(e);
    });
    if (any_editor_locked) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "[Locked]");
    }

    if (any_editor_locked) {
        ImGui::BeginDisabled();
    }

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

    const std::uint32_t current_selection_owner = selection_transaction_owner(entities);
    if (m_multi_tag_selection_owner != current_selection_owner) {
        m_multi_tag_selection_owner = current_selection_owner;
        m_multi_tag_buffer[0] = '\0';
    }

    const bool all_have_tag = std::all_of(entities.begin(), entities.end(), [&](EntityID entity) {
        return em.has_component<TagComponent>(entity);
    });
    if (all_have_tag) {
        const auto before_tags = capture_selected_components<TagComponent>(em, entities);
        const bool tag_mixed = selected_values_mixed<TagComponent, std::string>(
            before_tags, [](const TagComponent& t) { return t.tag; }
        );
        const auto tag_key = EditorPropertyTransaction::make_key(current_selection_owner, "MultiSelection.Tag.Name");
        if (!m_property_transactions.contains(tag_key)) {
            if (!tag_mixed && !before_tags.empty()) {
                std::strncpy(m_multi_tag_buffer, before_tags.front().second.tag.c_str(), sizeof(m_multi_tag_buffer) - 1);
            } else {
                m_multi_tag_buffer[0] = '\0';
            }
        }
        if (InspectorWidgets::property_text("Name", m_multi_tag_buffer, sizeof(m_multi_tag_buffer), tag_mixed)) {
            for (const EntityID entity : entities) {
                if (em.is_alive(entity) && em.has_component<TagComponent>(entity)) {
                    em.get_component<TagComponent>(entity).tag = m_multi_tag_buffer;
                }
            }
        }
        track_selected_component_edit<TagComponent>(
            em, entities, before_tags, "Tag.Name", "Bulk Rename Entities",
            transaction_context, [](const TagComponent& a, const TagComponent& b) { return a.tag == b.tag; }
        );
        ImGui::Separator();
    }

    std::vector<EntityID> prefab_instances;
    for (EntityID entity : entities) {
        if (em.has_component<PrefabLinkComponent>(entity)) {
            prefab_instances.push_back(entity);
        }
    }
    if (!prefab_instances.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.16f, 0.22f, 0.90f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        if (ImGui::BeginChild("##multi_prefab_card", ImVec2(0.0f, 62.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.75f, 1.0f, 1.0f));
            ImGui::Text("%s", editor_icon_label(EditorIcon::Component, "Prefab Instances").c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu of %zu selected)", prefab_instances.size(), entities.size());

            ImGui::Spacing();
            if (ImGui::Button("Unpack Selected", ImVec2(130.0f, 22.0f))) {
                for (EntityID p_ent : prefab_instances) {
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<UnpackPrefabCommand>(
                            &em, m_selection, p_ent, m_entity_state_store.get()
                        ));
                    } else {
                        UnpackPrefabCommand cmd(&em, m_selection, p_ent, m_entity_state_store.get());
                        cmd.execute();
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Unpack all selected prefab instances into standard scene entities");
            }

            ImGui::SameLine();
            if (ImGui::Button("Revert Selected", ImVec2(130.0f, 22.0f))) {
                for (EntityID p_ent : prefab_instances) {
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<RevertPrefabCommand>(
                            &em, m_selection, p_ent, m_renderer, m_entity_state_store.get()
                        ));
                    } else {
                        RevertPrefabCommand cmd(&em, m_selection, p_ent, m_renderer, m_entity_state_store.get());
                        cmd.execute();
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Revert all selected prefab instances to their master asset definitions");
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
    }


    if (all_have_transform) {
        const bool locked = any_editor_locked || std::any_of(entities.begin(), entities.end(), [&](EntityID entity) {
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
    }

    const auto rb_count = static_cast<std::size_t>(std::count_if(
        entities.begin(), entities.end(), [&](EntityID entity) {
            return em.has_component<RigidBodyComponent>(entity);
        }
    ));
    if (rb_count == entities.size()) {
        RigidBodyInspectorDrawer::draw_multiple(em, entities, transaction_context);
    } else if (rb_count > 0) {
        ImGui::TextDisabled("RigidBody 2D is shared by %zu of %zu entities.", rb_count, entities.size());
    }

    const auto col_count = static_cast<std::size_t>(std::count_if(
        entities.begin(), entities.end(), [&](EntityID entity) {
            return em.has_component<ColliderComponent>(entity);
        }
    ));
    if (col_count == entities.size()) {
        ColliderInspectorDrawer::draw_multiple(em, entities, transaction_context);
    } else if (col_count > 0) {
        ImGui::TextDisabled("Collider 2D is shared by %zu of %zu entities.", col_count, entities.size());
    }

    const auto cam_count = static_cast<std::size_t>(std::count_if(
        entities.begin(), entities.end(), [&](EntityID entity) {
            return em.has_component<CameraComponent>(entity);
        }
    ));
    if (cam_count == entities.size()) {
        CameraInspectorDrawer::draw_multiple(em, entities, transaction_context);
    } else if (cam_count > 0) {
        ImGui::TextDisabled("Camera 2D is shared by %zu of %zu entities.", cam_count, entities.size());
    }

    ImGui::Spacing();
    ImGui::Separator();

    std::vector<EntityID> to_delete;
    for (const EntityID entity : entities) {
        if (m_prefab_stage && m_prefab_stage->is_root_entity(entity)) continue;
        to_delete.push_back(entity);
    }
    const bool can_delete_any = !to_delete.empty() && !any_editor_locked;
    if (!can_delete_any) {
        ImGui::BeginDisabled(true);
    }
    const std::string delete_label = editor_icon_label(EditorIcon::Trash, "Delete Selected");
    if (ImGui::Button(delete_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
        if (can_delete_any) {
            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(
                    &em, m_selection, to_delete, m_entity_state_store.get()
                ));
            } else {
                for (const EntityID entity : to_delete) em.destroy_entity(entity);
                m_selection->clear();
            }
        }
    }
    if (!can_delete_any) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Prefab root entity cannot be deleted");
        }
    }
}

void InspectorPanel::draw_components(EntityManager& em, EntityID entity) {
    const bool is_locked_tilemap_collider = em.has_component<TilemapGeneratedColliderComponent>(entity);
    const bool is_editor_locked = m_entity_state_store && m_entity_state_store->is_locked(entity);
    const bool is_editor_visible = !m_entity_state_store || m_entity_state_store->is_visible(entity);
    const bool is_locked = is_locked_tilemap_collider || is_editor_locked;

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
        ImGuiInputTextFlags name_flags = is_editor_locked ? ImGuiInputTextFlags_ReadOnly : 0;
        if (ImGui::InputText("##entity_name", buffer, sizeof(buffer), name_flags)) {
            if (!is_editor_locked) {
                tag_comp.tag = buffer;
            }
        }
        capture_property_interaction();
        if (!is_editor_locked) {
            track_tag_edit(em, entity, tag_before, "Entity.Name", "Rename Entity");
        }
        ImGui::TextDisabled("Entity #%u", entity);

        if (is_editor_locked) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "[Locked]");
        }
        if (!is_editor_visible) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.60f, 0.80f), "[Hidden in Scene View]");
        }

        if (em.has_component<TransformComponent>(entity)) {
            ImGui::SameLine();
            TransformInspectorDrawer::draw_entity_active(em, entity, {
                m_command_history.get(), &m_property_transactions
            });
        }
    } else {
        ImGui::TextDisabled("Entity #%u", entity);
        if (is_editor_locked) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "[Locked]");
        }
        if (is_editor_visible) {
            ImGui::SameLine();
        }
    }

    // Prefab Instance Card or Save As Prefab button
    if (em.has_component<PrefabLinkComponent>(entity)) {
        const auto& link = em.get_component<PrefabLinkComponent>(entity);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.16f, 0.22f, 0.90f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        if (ImGui::BeginChild("##prefab_link_card", ImVec2(0.0f, 62.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.75f, 1.0f, 1.0f));
            ImGui::Text("%s", editor_icon_label(EditorIcon::Component, "Prefab Instance").c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", link.prefab_asset_path.c_str());

            ImGui::Spacing();
            if (ImGui::Button("Open Prefab", ImVec2(90.0f, 22.0f))) {
                if (m_on_open_prefab) {
                    m_on_open_prefab(link.prefab_asset_path);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open this prefab in isolated Prefab Stage");
            }

            ImGui::SameLine();
            if (ImGui::Button("Unpack", ImVec2(70.0f, 22.0f))) {
                if (m_command_history) {
                    m_command_history->execute_command(std::make_unique<UnpackPrefabCommand>(
                        &em, m_selection, entity, m_entity_state_store.get()
                    ));
                } else {
                    UnpackPrefabCommand cmd(&em, m_selection, entity, m_entity_state_store.get());
                    cmd.execute();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Unpack this instance into standard scene entities");
            }

            ImGui::SameLine();
            if (ImGui::Button("Revert Contents", ImVec2(115.0f, 22.0f))) {
                if (m_command_history) {
                    m_command_history->execute_command(std::make_unique<RevertPrefabCommand>(
                        &em, m_selection, entity, m_renderer, m_entity_state_store.get()
                    ));
                } else {
                    RevertPrefabCommand cmd(&em, m_selection, entity, m_renderer, m_entity_state_store.get());
                    cmd.execute();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Restores asset contents from disk while keeping current scene placement and parent");
            }

            ImGui::SameLine();
            if (ImGui::Button("Apply to Asset", ImVec2(105.0f, 22.0f))) {
                if (m_command_history) {
                    m_command_history->execute_command(std::make_unique<ApplyPrefabCommand>(
                        &em, entity, m_renderer, m_entity_state_store.get()
                    ));
                } else {
                    ApplyPrefabCommand cmd(&em, entity, m_renderer, m_entity_state_store.get());
                    cmd.execute();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Writes this instance's current modifications back to the prefab asset file on disk");
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    } else {
        const std::string prefab_btn_label = editor_icon_label(EditorIcon::Component, "Save As Prefab...");
        if (ImGui::Button(prefab_btn_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 22.0f))) {
            std::string tag_name = em.has_component<TagComponent>(entity) ? em.get_component<TagComponent>(entity).tag : "Entity";
            std::string clean_name = tag_name;
            for (char& c : clean_name) {
                if (c == ' ' || c == '/' || c == '\\' || c == ':') c = '_';
            }
            if (clean_name.empty()) clean_name = "Prefab";
            std::string filepath = "assets/prefabs/" + clean_name + ".prefab";
            static_cast<void>(PrefabSerializer::save_to_file(em, entity, filepath, m_renderer));
        }
    }

    ImGui::Separator();

    const EditorInspectorContext inspector_context{
        m_renderer,
        m_asset_manager,
        m_command_history.get(),
        &m_property_transactions,
        &m_animation_preview,
        m_anim_studio,
        is_locked
    };
    ComponentRegistry& runtime_registry = default_component_registry();
    EditorComponentRegistry& editor_registry = default_editor_component_registry();
    for (const EditorComponentDescriptor& descriptor : editor_registry.descriptors()) {
        if (!descriptor.show_in_inspector || !descriptor.draw) continue;
        const ComponentDescriptor* runtime_descriptor = runtime_registry.find_by_key(descriptor.component_key);
        if (runtime_descriptor && runtime_descriptor->has(em, entity)) {
            descriptor.draw(em, entity, inspector_context);
        }
    }

    // --- Bottom Add Component Popup ---
    ImGui::Spacing();
    if (is_editor_locked) {
        ImGui::BeginDisabled();
    }
    const std::string add_component_label = editor_icon_label(EditorIcon::Plus, "Add Component...");
    if (ImGui::Button(add_component_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
        if (!is_editor_locked) {
            ImGui::OpenPopup("AddComponentPopup");
        }
    }
    if (is_editor_locked) {
        ImGui::EndDisabled();
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        constexpr EditorComponentCategory categories[] = {
            EditorComponentCategory::Core,
            EditorComponentCategory::Rendering,
            EditorComponentCategory::Physics,
            EditorComponentCategory::Animation,
            EditorComponentCategory::World
        };
        bool has_previous_category = false;
        for (const EditorComponentCategory category : categories) {
            bool category_started = false;
            for (const EditorComponentDescriptor& descriptor : editor_registry.descriptors()) {
                if (!descriptor.addable || descriptor.category != category) continue;
                const ComponentDescriptor* runtime_descriptor = runtime_registry.find_by_key(descriptor.component_key);
                if (!runtime_descriptor || runtime_descriptor->has(em, entity)) continue;
                if (!category_started) {
                    if (has_previous_category) ImGui::Separator();
                    ImGui::TextDisabled("%s", editor_component_category_label(category));
                    category_started = true;
                    has_previous_category = true;
                }

                const EditorComponentAvailability availability = editor_registry.availability(
                    descriptor, runtime_registry, em, entity
                );
                const bool enabled = availability.available && m_command_history;
                const std::string label = editor_icon_label(descriptor.icon, descriptor.display_name.c_str());
                if (ImGui::MenuItem(label.c_str(), nullptr, false, enabled)) {
                    ComponentSnapshotSet before;
                    ComponentSnapshotSet after;
                    runtime_descriptor->capture(em, entity, before);
                    descriptor.create_default(em, entity, inspector_context, after);
                    m_command_history->execute_command(std::make_unique<SetRegisteredComponentCommand>(
                        &runtime_registry,
                        &em,
                        entity,
                        descriptor.component_key,
                        std::move(before),
                        std::move(after),
                        ComponentSerializationContext{m_renderer, m_asset_manager},
                        "Add " + descriptor.display_name
                    ));
                }
                if (!enabled && !availability.reason.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", availability.reason.c_str());
                }
            }
        }
        ImGui::EndPopup();
    }

}

} // namespace Engine
