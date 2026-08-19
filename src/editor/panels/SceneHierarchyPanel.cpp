#include "SceneHierarchyPanel.hpp"
#include "../commands/EntityCommands.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../core/AssetManager.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>

namespace Engine {

SceneHierarchyPanel::SceneHierarchyPanel(
    SceneManager* scene_manager, 
    SDL_Renderer* renderer, 
    std::shared_ptr<EditorSelection> selection,
    std::shared_ptr<EditorCommandHistory> command_history
) : m_scene_manager(scene_manager), 
    m_renderer(renderer), 
    m_selection(selection),
    m_command_history(command_history) {
    if (!m_selection) {
        m_selection = std::make_shared<EditorSelection>();
    }
}

void SceneHierarchyPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(280, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        if (!m_scene_manager) {
            ImGui::TextDisabled("No SceneManager available.");
            ImGui::End();
            return;
        }

        Scene* active_scene = m_scene_manager->get_active_scene();
        if (!active_scene) {
            ImGui::TextDisabled("No active scene loaded.");
            ImGui::End();
            return;
        }

        // --- Active Scene Header ---
        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "ACTIVE SCENE: %s", active_scene->get_name().c_str());
        ImGui::Separator();

        // --- Search Filter ---
        ImGui::InputTextWithHint("##search", "Search entity...", m_search_filter, sizeof(m_search_filter));
        ImGui::Spacing();

        EntityManager& em = active_scene->get_entity_manager();
        const auto& living_entities = em.get_living_entities();

        if (m_selection && m_selection->count() > 1) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Selected: %zu entities", m_selection->count());
        } else {
            ImGui::TextDisabled("Entities: %zu alive", living_entities.size());
        }
        ImGui::Separator();

        // --- Entity Tree List ---
        ImGui::BeginChild("EntityTreeScroll", ImVec2(0, -35), true);

        std::string filter_str = m_search_filter;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        ImGuiIO& io = ImGui::GetIO();
        bool is_multi_key = io.KeyCtrl || io.KeyShift;

        // Iterate through root-level entities (entities with no parent or parent not alive)
        for (EntityID id : living_entities) {
            if (!em.is_alive(id)) continue;

            bool is_root = true;
            if (em.has_component<HierarchyComponent>(id)) {
                EntityID parent_id = em.get_component<HierarchyComponent>(id).parent;
                if (parent_id != 0 && em.is_alive(parent_id)) {
                    is_root = false;
                }
            }

            if (is_root) {
                draw_entity_node(em, id, filter_str, is_multi_key);
            }
        }

        // Empty space at bottom of tree to allow dropping to unparent (make root entity)
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > 10.0f) {
            ImGui::Dummy(avail);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY_DRAG")) {
                    EntityID dropped_id = *(const EntityID*)payload->Data;
                    if (em.is_alive(dropped_id)) {
                        em.remove_parent(dropped_id);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        // Right-click on empty area in tree window
        if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("➕ Add Entity (Root)")) {
                create_entity_helper(em, 0);
            }
            ImGui::EndPopup();
        }

        ImGui::EndChild();

        // --- Bottom Actions Bar ---
        if (ImGui::Button(" + Add Entity ", ImVec2(100, 24))) {
            EntityID selected_parent = (m_selection && !m_selection->empty()) ? m_selection->get_primary() : 0;
            create_entity_helper(em, selected_parent);
        }
        ImGui::SameLine();
        if (m_selection && !m_selection->empty()) {
            std::string del_label = m_selection->count() > 1 ? " Delete (" + std::to_string(m_selection->count()) + ") " : " Delete ";
            if (ImGui::Button(del_label.c_str(), ImVec2(90, 24))) {
                std::vector<EntityID> to_delete;
                for (EntityID s_id : m_selection->get_selected()) to_delete.push_back(s_id);

                if (m_command_history) {
                    m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, to_delete));
                } else {
                    for (EntityID s_id : to_delete) em.destroy_entity(s_id);
                    m_selection->clear();
                }
            }
        }

        // Keyboard Shortcut via ShortcutManager: editor.delete
        if (!io.WantTextInput && ShortcutManager::get().is_triggered("editor.delete") && 
            (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))) {
            if (m_selection && !m_selection->empty()) {
                std::vector<EntityID> to_delete;
                for (EntityID s_id : m_selection->get_selected()) {
                    if (em.is_alive(s_id)) to_delete.push_back(s_id);
                }

                if (!to_delete.empty()) {
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, to_delete));
                    } else {
                        for (EntityID s_id : to_delete) em.destroy_entity(s_id);
                        m_selection->clear();
                    }
                }
            }
        }
    }
    ImGui::End();
}

bool SceneHierarchyPanel::matches_filter_recursive(EntityManager& em, EntityID id, const std::string& filter_str) const {
    if (filter_str.empty()) return true;

    std::string label = "Entity";
    if (em.has_component<TagComponent>(id)) {
        label = em.get_component<TagComponent>(id).tag;
    }
    std::string lower_label = label;
    std::transform(lower_label.begin(), lower_label.end(), lower_label.begin(), ::tolower);
    if (lower_label.find(filter_str) != std::string::npos) {
        return true;
    }

    if (em.has_component<HierarchyComponent>(id)) {
        for (EntityID child_id : em.get_component<HierarchyComponent>(id).children) {
            if (em.is_alive(child_id) && matches_filter_recursive(em, child_id, filter_str)) {
                return true;
            }
        }
    }
    return false;
}

bool SceneHierarchyPanel::is_descendant(EntityManager& em, EntityID potential_descendant, EntityID ancestor) const {
    if (potential_descendant == 0 || ancestor == 0) return false;
    if (potential_descendant == ancestor) return true;
    if (!em.has_component<HierarchyComponent>(potential_descendant)) return false;

    EntityID curr = em.get_component<HierarchyComponent>(potential_descendant).parent;
    while (curr != 0 && em.is_alive(curr)) {
        if (curr == ancestor) return true;
        if (!em.has_component<HierarchyComponent>(curr)) break;
        curr = em.get_component<HierarchyComponent>(curr).parent;
    }
    return false;
}

void SceneHierarchyPanel::draw_entity_node(EntityManager& em, EntityID id, const std::string& filter_str, bool is_multi_key) {
    if (!em.is_alive(id)) return;
    if (!matches_filter_recursive(em, id, filter_str)) return;

    std::string name = "Entity";
    if (em.has_component<TagComponent>(id)) {
        name = em.get_component<TagComponent>(id).tag;
    }

    bool is_selected = m_selection && m_selection->is_selected(id);
    bool has_children = false;
    if (em.has_component<HierarchyComponent>(id)) {
        const auto& children = em.get_component<HierarchyComponent>(id).children;
        for (EntityID c : children) {
            if (em.is_alive(c)) {
                has_children = true;
                break;
            }
        }
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (has_children) {
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;
        if (!filter_str.empty()) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
    } else {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (is_selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool node_open = ImGui::TreeNodeEx((void*)(uintptr_t)id, flags, "%s", name.c_str());

    // Single click / multi-click selection
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        if (m_selection) {
            m_selection->select(id, is_multi_key);
        }
    }

    // --- Drag and Drop: Source ---
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("ENTITY_HIERARCHY_DRAG", &id, sizeof(EntityID));
        ImGui::Text("Reparent '%s'", name.c_str());
        ImGui::EndDragDropSource();
    }

    // --- Drag and Drop: Target (Reparent) ---
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY_DRAG")) {
            EntityID dropped_id = *(const EntityID*)payload->Data;
            if (em.is_alive(dropped_id) && dropped_id != id && !is_descendant(em, id, dropped_id)) {
                em.set_parent(dropped_id, id);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click context menu per entity
    if (ImGui::BeginPopupContextItem()) {
        if (m_selection && !m_selection->is_selected(id)) {
            m_selection->select(id, false);
        }

        if (ImGui::MenuItem("➕ Add Child Entity (Sprite)")) {
            create_entity_helper(em, id);
        }
        if (ImGui::MenuItem("🗺️ Add Child Tilemap")) {
            create_tilemap_helper(em, id);
        }

        bool has_parent = em.has_component<HierarchyComponent>(id) && (em.get_component<HierarchyComponent>(id).parent != 0);
        if (has_parent && ImGui::MenuItem("⤴️ Unparent (Move to Root)")) {
            em.remove_parent(id);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("🗑️ Delete Entity")) {
            std::vector<EntityID> to_delete;
            if (m_selection) {
                for (EntityID s_id : m_selection->get_selected()) to_delete.push_back(s_id);
            } else {
                to_delete.push_back(id);
            }

            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, to_delete));
            } else {
                for (EntityID s_id : to_delete) em.destroy_entity(s_id);
                if (m_selection) m_selection->clear();
            }
        }
        ImGui::EndPopup();
    }

    // Render Children recursively if tree node is expanded
    if (has_children && node_open) {
        auto children_copy = em.get_component<HierarchyComponent>(id).children;
        for (EntityID child_id : children_copy) {
            if (em.is_alive(child_id)) {
                draw_entity_node(em, child_id, filter_str, is_multi_key);
            }
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::create_entity_helper(EntityManager& em, EntityID parent_id) {
    EntitySnapshot snap;
    snap.registered_components.set(TagComponent{parent_id == 0 ? "New_Entity" : "Child_Entity"});
    snap.registered_components.set(TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
    snap.registered_components.set(SpriteComponent{
        AssetManager::get().get_white_texture(m_renderer),
        glm::vec2(64.0f, 64.0f),
        SDL_Color{255, 255, 255, 255}
    });
    if (parent_id != 0) {
        snap.has_hierarchy = true;
        snap.hierarchy.parent = parent_id;
    }

    if (m_command_history) {
        m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, m_selection, snap));
    } else {
        EntityID new_id = em.create_entity();
        default_component_registry().restore_entity(em, new_id, snap.registered_components);
        if (parent_id != 0) {
            em.set_parent(new_id, parent_id);
        }
        std::string finalize_error;
        if (!default_component_registry().finalize_entity(
            em,
            new_id,
            ComponentFinalizeContext{
                .serialization = {m_renderer, &AssetManager::get()},
                .reason = ComponentFinalizeReason::DefaultCreation
            },
            finalize_error
        )) {
            em.destroy_entity(new_id);
            return;
        }
        if (m_selection) m_selection->select(new_id, false);
    }
}

void SceneHierarchyPanel::create_tilemap_helper(EntityManager& em, EntityID parent_id) {
    EntitySnapshot snap;
    snap.registered_components.set(TagComponent{parent_id == 0 ? "Tilemap" : "Child_Tilemap"});
    snap.registered_components.set(TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
    snap.registered_components.set(TilemapComponent{});
    if (parent_id != 0) {
        snap.has_hierarchy = true;
        snap.hierarchy.parent = parent_id;
    }

    if (m_command_history) {
        m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, m_selection, std::move(snap)));
    } else {
        CreateEntityCommand command(&em, m_selection, std::move(snap));
        command.execute();
    }
}

} // namespace Engine
