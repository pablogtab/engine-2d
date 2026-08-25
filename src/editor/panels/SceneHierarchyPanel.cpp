#include "SceneHierarchyPanel.hpp"
#include "../stage/PrefabStageController.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/HierarchyCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../commands/EditorEntityStateCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include "../tools/EditorIcons.hpp"
#include "../tools/EditorTransformUtils.hpp"
#include "../inspector/EditorComponentRegistry.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../ecs/components/AnimationComponents.hpp"
#include "../../ecs/components/UIComponents.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/Log.hpp"
#include "../../core/FileSystem.hpp"
#include "../tools/EditorHierarchyFilter.hpp"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace Engine {

namespace {

bool has_canvas_ancestor(const EntityManager& em, EntityID entity) {
    std::unordered_set<EntityID> visited;
    EntityID current = entity;
    while (current != 0 && em.is_alive(current) && visited.insert(current).second) {
        if (em.has_component<CanvasComponent>(current)) return true;
        if (!em.has_component<HierarchyComponent>(current)) break;
        current = em.get_component<HierarchyComponent>(current).parent;
    }
    return false;
}

} // namespace

void SceneHierarchyPanel::begin_rename(EntityManager& em, EntityID id) {
    if (id == 0 || !em.is_alive(id) || !em.has_component<TagComponent>(id)) return;
    if (m_entity_state_store && m_entity_state_store->is_locked(id)) return;
    m_renaming_entity = id;
    std::snprintf(
        m_rename_buffer,
        sizeof(m_rename_buffer),
        "%s",
        em.get_component<TagComponent>(id).tag.c_str()
    );
    m_focus_rename = true;
}

void SceneHierarchyPanel::finish_rename(EntityManager& em, bool commit) {
    const EntityID entity = m_renaming_entity;
    m_renaming_entity = 0;
    m_focus_rename = false;
    if (!commit || entity == 0 || !em.is_alive(entity) ||
        !em.has_component<TagComponent>(entity)) {
        return;
    }

    std::string name = m_rename_buffer;
    const auto first = name.find_first_not_of(" \t\r\n");
    const auto last = name.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return;
    name = name.substr(first, last - first + 1);

    const TagComponent before = em.get_component<TagComponent>(entity);
    if (before.tag == name) return;
    TagComponent after = before;
    after.tag = std::move(name);
    if (m_command_history) {
        m_command_history->execute_command(std::make_unique<SetComponentCommand<TagComponent>>(
            &em, entity, before, after, "Rename Entity"
        ));
    } else {
        em.get_component<TagComponent>(entity) = std::move(after);
    }
}

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
        const bool is_dirty = m_command_history && m_command_history->is_dirty();
        std::string dirty_indicator = is_dirty ? " *" : "";
        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "ACTIVE SCENE: %s%s", active_scene->get_name().c_str(), dirty_indicator.c_str());
        if (is_dirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "(Unsaved Changes)");
        }
        ImGui::Separator();

        // --- Search Filter with Clear Button ---
        float avail_w = ImGui::GetContentRegionAvail().x;
        bool has_filter = m_search_filter[0] != '\0';
        if (has_filter) {
            ImGui::SetNextItemWidth(avail_w - 28.0f);
        } else {
            ImGui::SetNextItemWidth(avail_w);
        }
        ImGui::InputTextWithHint("##search", "Search name or :type (e.g. :sprite, :col, :locked)...", m_search_filter, sizeof(m_search_filter));
        if (has_filter) {
            ImGui::SameLine();
            if (ImGui::Button("X##clear_search", ImVec2(22.0f, 0.0f))) {
                m_search_filter[0] = '\0';
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Clear search filter");
            }
        }
        ImGui::Spacing();

        EntityManager& em = active_scene->get_entity_manager();
        const bool runtime_mode = !m_scene_manager->is_editing();
        if (runtime_mode && m_renaming_entity != 0) finish_rename(em, false);
        const auto& living_entities = em.get_living_entities();

        if (runtime_mode) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                "RUNTIME - values are temporary; structure is locked");
        }

        if (m_selection && m_selection->count() > 1) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Selected: %zu entities", m_selection->count());
        } else {
            ImGui::TextDisabled("Entities: %zu alive", living_entities.size());
        }
        ImGui::Separator();

        // --- Entity Tree List ---
        ImGui::BeginChild("EntityTreeScroll", ImVec2(0, 0), true);

        std::string filter_str = m_search_filter;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        ImGuiIO& io = ImGui::GetIO();
        bool is_multi_key = io.KeyCtrl || io.KeyShift;

        const ImVec2 hierarchy_spacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(hierarchy_spacing.x, 0.0f));

        // Iterate through root-level entities in authored visual order
        for (EntityID id : em.get_root_entities()) {
            if (!em.is_alive(id)) continue;
            draw_entity_node(em, id, filter_str, is_multi_key);
        }
        ImGui::PopStyleVar();

        // Empty space at bottom of tree to allow dropping to unparent (make root entity)
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > 10.0f) {
            ImGui::Dummy(avail);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                if (runtime_mode) {
                    m_scene_manager->clear_runtime_selection();
                } else if (m_selection) {
                    m_selection->clear();
                }
            }
            if (!runtime_mode && ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* multi_payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY_MULTI_DRAG");
                const ImGuiPayload* single_payload = multi_payload ? nullptr : ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY_DRAG");
                std::vector<EntityID> dropped_entities;
                if (multi_payload) {
                    const size_t count = multi_payload->DataSize / sizeof(EntityID);
                    const EntityID* data = (const EntityID*)multi_payload->Data;
                    dropped_entities.assign(data, data + count);
                } else if (single_payload) {
                    dropped_entities.push_back(*(const EntityID*)single_payload->Data);
                }

                if (const ImGuiPayload* asset_payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET")) {
                    const char* path_str = static_cast<const char*>(asset_payload->Data);
                    if (path_str) {
                        create_asset_helper(em, 0, path_str);
                    }
                }

                if (!dropped_entities.empty()) {
                    if (m_command_history) {
                        m_command_history->execute_command(
                            std::make_unique<ReparentEntitiesCommand>(&em, dropped_entities, 0, m_selection, "", m_entity_state_store.get())
                        );
                    } else {
                        ReparentEntitiesCommand cmd(&em, dropped_entities, 0, m_selection, "", m_entity_state_store.get());
                        cmd.execute();
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        // Right-click on empty area in tree window
        if (!runtime_mode && ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            ImGui::TextDisabled("Create");
            ImGui::Separator();
            render_create_menu(em, 0);
            ImGui::EndPopup();
        }

        // --- Keyboard Shortcuts (Alt+Up / Alt+Down to move entity) ---
        if (!runtime_mode && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && m_selection && m_selection->count() == 1) {
            EntityID selected_entity = m_selection->get_primary();
            if (em.is_alive(selected_entity)) {
                bool is_locked = m_entity_state_store && m_entity_state_store->is_locked(selected_entity);
                if (!is_locked) {
                    if (m_renaming_entity == 0 && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                        begin_rename(em, selected_entity);
                    }
                    size_t sib_idx = em.get_sibling_index(selected_entity);
                    size_t sib_count = em.get_sibling_count(selected_entity);
                    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow) && sib_idx > 0) {
                        if (m_command_history) {
                            m_command_history->execute_command(
                                std::make_unique<ReorderEntityCommand>(&em, selected_entity, sib_idx - 1, m_selection, m_entity_state_store.get())
                            );
                        } else {
                            static_cast<void>(em.reorder_sibling(selected_entity, sib_idx - 1));
                        }
                    } else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_DownArrow) && sib_idx + 1 < sib_count) {
                        if (m_command_history) {
                            m_command_history->execute_command(
                                std::make_unique<ReorderEntityCommand>(&em, selected_entity, sib_idx + 1, m_selection, m_entity_state_store.get())
                            );
                        } else {
                            static_cast<void>(em.reorder_sibling(selected_entity, sib_idx + 1));
                        }
                    }
                }
            }
        }

        ImGui::EndChild();

        // Keyboard Shortcut via ShortcutManager: editor.delete
        if (!runtime_mode && !io.WantTextInput && ShortcutManager::get().is_triggered("editor.delete") &&
            (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))) {
            if (m_selection && !m_selection->empty()) {
                std::vector<EntityID> to_delete;
                for (EntityID s_id : m_selection->get_selected()) {
                    if (m_prefab_stage && m_prefab_stage->is_root_entity(s_id)) continue;
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
    return EditorHierarchyFilter::matches_recursive(em, id, filter_str, m_entity_state_store.get());
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

    const bool runtime_mode = m_scene_manager && !m_scene_manager->is_editing();
    bool is_selected = runtime_mode ? m_scene_manager->runtime_selected_entity() == id
        : m_selection && m_selection->is_selected(id);
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

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
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

    const bool is_locked = m_entity_state_store && m_entity_state_store->is_locked(id);
    const bool is_visible = !m_entity_state_store || m_entity_state_store->is_visible(id);
    bool is_ancestor_hidden = false;
    if (m_entity_state_store && em.has_component<HierarchyComponent>(id)) {
        EntityID curr = em.get_component<HierarchyComponent>(id).parent;
        while (curr != 0 && em.is_alive(curr)) {
            if (!m_entity_state_store->is_visible(curr)) {
                is_ancestor_hidden = true;
                break;
            }
            if (!em.has_component<HierarchyComponent>(curr)) break;
            curr = em.get_component<HierarchyComponent>(curr).parent;
        }
    }
    const bool is_effectively_visible = is_visible && !is_ancestor_hidden;
    const bool directly_matches = !filter_str.empty() && EditorHierarchyFilter::matches_single(em, id, filter_str, m_entity_state_store.get());
    const bool is_prefab_instance = em.has_component<PrefabLinkComponent>(id);
    bool is_variant = false;
    if (is_prefab_instance) {
        is_variant = em.get_component<PrefabLinkComponent>(id).is_variant();
    }
    const std::string display_name = is_prefab_instance
        ? (is_variant ? "[V] " + name : "[P] " + name)
        : name;

    int style_color_count = 0;
    if (!is_effectively_visible) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 0.55f));
        style_color_count++;
    } else if (directly_matches) {
        // Highlight matching node in vibrant cyan
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.85f, 1.0f, 1.0f));
        style_color_count++;
    } else if (is_prefab_instance) {
        if (is_variant) {
            // Highlight variant in soft electric lavender/purple
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.55f, 1.0f, 1.0f));
        } else {
            // Highlight prefab instance in soft electric cyan/blue
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.75f, 1.0f, 1.0f));
        }
        style_color_count++;
    } else if (!filter_str.empty()) {
        // Ancestor container of matching child (subtle dimming)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.72f, 0.78f, 0.75f));
        style_color_count++;
    }

    const bool is_renaming = !runtime_mode && m_renaming_entity == id;
    bool node_open = ImGui::TreeNodeEx(
        (void*)(uintptr_t)id,
        flags,
        "%s",
        is_renaming ? "" : display_name.c_str()
    );
    const ImVec2 node_min = ImGui::GetItemRectMin();
    const ImVec2 node_max = ImGui::GetItemRectMax();
    if (!runtime_mode && !is_locked && ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        begin_rename(em, id);
    }
    if (style_color_count > 0) {
        ImGui::PopStyleColor(style_color_count);
    }

    if (is_prefab_instance && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        const auto& link = em.get_component<PrefabLinkComponent>(id);
        if (link.is_variant()) {
            ImGui::SetTooltip("Prefab Variant: %s\nBase: %s", link.prefab_asset_path.c_str(), link.base_prefab_path.c_str());
        } else {
            ImGui::SetTooltip("Prefab Instance: %s", link.prefab_asset_path.c_str());
        }
    }



    // Single click / multi-click selection on the tree node item
    if (runtime_mode) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
            m_scene_manager->set_runtime_selected_entity(id);
        }
    } else if (m_selection) {
        if (is_multi_key) {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                m_selection->select(id, true);
            }
        } else if (!is_selected) {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                m_selection->select(id, false);
            }
        } else {
            // Already selected item clicked without modifiers:
            // Defer narrowing selection until mouse release without drag to allow dragging the multi-selection
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                !ImGui::IsItemToggledOpen() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                m_selection->select(id, false);
            }
        }
    }

    // --- Drag and Drop: Source ---
    if (!runtime_mode && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        std::vector<EntityID> drag_entities;
        if (m_selection && m_selection->is_selected(id) && m_selection->count() > 1) {
            const std::unordered_set<EntityID> sel_set = m_selection->get_selected();
            for (EntityID s_id : m_selection->get_selected()) {
                if (em.is_alive(s_id) && !has_selected_ancestor(em, s_id, sel_set)) {
                    drag_entities.push_back(s_id);
                }
            }
        }
        if (drag_entities.empty()) {
            drag_entities.push_back(id);
        }

        ImGui::SetDragDropPayload("ENTITY_HIERARCHY_MULTI_DRAG", drag_entities.data(), drag_entities.size() * sizeof(EntityID));

        if (drag_entities.size() > 1) {
            ImGui::Text("Reparent (%zu) Entities", drag_entities.size());
        } else {
            ImGui::Text("Reparent '%s'", name.c_str());
        }
        ImGui::EndDragDropSource();
    }

    // --- Drag and Drop: Target (Reparent & Reorder) ---
    if (!runtime_mode && ImGui::BeginDragDropTarget()) {
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        const float item_h = std::max(item_max.y - item_min.y, 1.0f);
        const float mouse_y = ImGui::GetMousePos().y;
        const float rel_y = mouse_y - item_min.y;

        enum class DropZone { Before, Inside, After };
        DropZone zone = DropZone::Inside;
        if (rel_y < item_h * 0.28f) {
            zone = DropZone::Before;
        } else if (rel_y > item_h * 0.72f) {
            zone = DropZone::After;
        }

        // Draw professional visual insertion line / box indicators
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        if (zone == DropZone::Before) {
            draw_list->AddLine(ImVec2(item_min.x, item_min.y), ImVec2(item_max.x, item_min.y), IM_COL32(95, 145, 255, 255), 2.0f);
            draw_list->AddCircleFilled(ImVec2(item_min.x + 3.0f, item_min.y), 3.0f, IM_COL32(95, 145, 255, 255));
        } else if (zone == DropZone::After) {
            draw_list->AddLine(ImVec2(item_min.x, item_max.y), ImVec2(item_max.x, item_max.y), IM_COL32(95, 145, 255, 255), 2.0f);
            draw_list->AddCircleFilled(ImVec2(item_min.x + 3.0f, item_max.y), 3.0f, IM_COL32(95, 145, 255, 255));
        } else {
            draw_list->AddRect(item_min, item_max, IM_COL32(95, 145, 255, 180), 2.0f);
        }

        const ImGuiPayload* multi_payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY_MULTI_DRAG");
        const ImGuiPayload* single_payload = multi_payload ? nullptr : ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY_DRAG");
        std::vector<EntityID> dropped_entities;
        if (multi_payload) {
            const size_t count = multi_payload->DataSize / sizeof(EntityID);
            const EntityID* data = (const EntityID*)multi_payload->Data;
            dropped_entities.assign(data, data + count);
        } else if (single_payload) {
            dropped_entities.push_back(*(const EntityID*)single_payload->Data);
        }

        if (const ImGuiPayload* asset_payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET")) {
            const char* path_str = static_cast<const char*>(asset_payload->Data);
            if (path_str) {
                if (zone == DropZone::Inside) {
                    create_asset_helper(em, id, path_str);
                } else {
                    EntityID target_parent = 0;
                    if (em.has_component<HierarchyComponent>(id)) {
                        target_parent = em.get_component<HierarchyComponent>(id).parent;
                    }
                    create_asset_helper(em, target_parent, path_str);
                }
            }
        }

        if (!dropped_entities.empty()) {
            bool allows_reparent = true;
            if (m_prefab_stage && m_prefab_stage->is_active()) {
                for (EntityID d_id : dropped_entities) {
                    if (m_prefab_stage->is_root_entity(d_id)) {
                        allows_reparent = false;
                        break;
                    }
                }
            }

            if (allows_reparent && zone == DropZone::Inside) {
                if (m_command_history) {
                    m_command_history->execute_command(
                        std::make_unique<ReparentEntitiesCommand>(&em, dropped_entities, id, m_selection, "", m_entity_state_store.get())
                    );
                } else {
                    ReparentEntitiesCommand cmd(&em, dropped_entities, id, m_selection, "", m_entity_state_store.get());
                    cmd.execute();
                }
            } else if (allows_reparent) {
                EntityID target_parent = 0;
                if (em.has_component<HierarchyComponent>(id)) {
                    target_parent = em.get_component<HierarchyComponent>(id).parent;
                }

                // In Prefab Stage, non-root entities cannot become root-level (target_parent == 0)
                if (m_prefab_stage && m_prefab_stage->is_active() && target_parent == 0) {
                    allows_reparent = false;
                }

                if (allows_reparent) {
                    size_t target_idx = em.get_sibling_index(id);
                    if (zone == DropZone::After) {
                        target_idx++;
                    }

                    if (dropped_entities.size() == 1 && em.has_component<HierarchyComponent>(dropped_entities[0]) &&
                        em.get_component<HierarchyComponent>(dropped_entities[0]).parent == target_parent) {
                        // Single sibling reorder
                        EntityID single_id = dropped_entities[0];
                        size_t cur_idx = em.get_sibling_index(single_id);
                        if (cur_idx < target_idx && target_idx > 0) target_idx--;
                        if (m_command_history) {
                            m_command_history->execute_command(
                                std::make_unique<ReorderEntityCommand>(&em, single_id, target_idx, m_selection, m_entity_state_store.get())
                            );
                        } else {
                            static_cast<void>(em.reorder_sibling(single_id, target_idx));
                        }
                    } else {
                        // Multi-entity reparenting/reordering
                        if (m_command_history) {
                            m_command_history->execute_command(
                                std::make_unique<ReparentEntitiesCommand>(&em, dropped_entities, target_parent, m_selection, "", m_entity_state_store.get(), target_idx)
                            );
                        } else {
                            ReparentEntitiesCommand cmd(&em, dropped_entities, target_parent, m_selection, "", m_entity_state_store.get(), target_idx);
                            cmd.execute();
                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click context menu per entity
    if (!runtime_mode && ImGui::BeginPopupContextItem()) {
        if (m_selection && !m_selection->is_selected(id)) {
            m_selection->select(id, false);
        }

        if (ImGui::MenuItem("Rename", "F2", false, !is_locked)) {
            begin_rename(em, id);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Create Child", !is_locked)) {
            render_create_menu(em, id);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save As Prefab...", nullptr, false, !is_locked)) {
            std::string tag_name = em.has_component<TagComponent>(id) ? em.get_component<TagComponent>(id).tag : "Entity";
            std::string clean_name = tag_name;
            for (char& c : clean_name) {
                if (c == ' ' || c == '/' || c == '\\' || c == ':') c = '_';
            }
            if (clean_name.empty()) clean_name = "Prefab";
            const std::string filepath = (std::filesystem::path(FileSystem::get_project_directory()) /
                "Assets/Prefabs" / (clean_name + ".prefab")).string();
            if (m_command_history) {
                m_command_history->execute_command(
                    std::make_unique<SaveAsPrefabCommand>(&em, id, filepath, m_renderer, m_entity_state_store.get())
                );
            } else {
                SaveAsPrefabCommand command(&em, id, filepath, m_renderer, m_entity_state_store.get());
                static_cast<void>(command.execute());
            }
        }

        if (em.has_component<PrefabLinkComponent>(id)) {
            if (ImGui::MenuItem("Create Prefab Variant...", nullptr, false, !is_locked)) {
                std::string tag_name = em.has_component<TagComponent>(id) ? em.get_component<TagComponent>(id).tag : "Variant";
                std::string clean_name = tag_name;
                for (char& c : clean_name) {
                    if (c == ' ' || c == '/' || c == '\\' || c == ':') c = '_';
                }
                if (clean_name.empty()) clean_name = "Variant";
                const std::string variant_path = (std::filesystem::path(FileSystem::get_project_directory()) /
                    "Assets/Prefabs" / (clean_name + "_Variant.prefab")).string();
                if (m_command_history) {
                    m_command_history->execute_command(
                        std::make_unique<CreatePrefabVariantCommand>(&em, id, variant_path, m_renderer, m_entity_state_store.get())
                    );
                } else {
                    CreatePrefabVariantCommand cmd(&em, id, variant_path, m_renderer, m_entity_state_store.get());
                    cmd.execute();
                }
            }
        }



        bool has_parent = em.has_component<HierarchyComponent>(id) && (em.get_component<HierarchyComponent>(id).parent != 0);
        const bool in_prefab_stage = m_prefab_stage && m_prefab_stage->is_active();
        if (has_parent && ImGui::MenuItem("Unparent (Move to Root)", nullptr, false, !is_locked && !in_prefab_stage)) {
            if (m_command_history) {
                m_command_history->execute_command(
                    std::make_unique<ReparentEntityCommand>(&em, id, 0, m_selection, "", m_entity_state_store.get())
                );
            } else {
                ReparentEntityCommand cmd(&em, id, 0, m_selection, "", m_entity_state_store.get());
                cmd.execute();
            }
        }

        ImGui::Separator();

        // --- Sibling Reordering ---
        size_t sib_idx = em.get_sibling_index(id);
        size_t sib_count = em.get_sibling_count(id);
        bool can_move_up = !is_locked && (sib_idx > 0);
        bool can_move_down = !is_locked && (sib_idx + 1 < sib_count);

        if (ImGui::MenuItem("Move Up", "Alt+Up", false, can_move_up)) {
            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<ReorderEntityCommand>(&em, id, sib_idx - 1, m_selection, m_entity_state_store.get()));
            } else {
                static_cast<void>(em.reorder_sibling(id, sib_idx - 1));
            }
        }
        if (ImGui::MenuItem("Move Down", "Alt+Down", false, can_move_down)) {
            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<ReorderEntityCommand>(&em, id, sib_idx + 1, m_selection, m_entity_state_store.get()));
            } else {
                static_cast<void>(em.reorder_sibling(id, sib_idx + 1));
            }
        }
        if (ImGui::MenuItem("Move to Top", nullptr, false, can_move_up)) {
            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<ReorderEntityCommand>(&em, id, 0, m_selection, m_entity_state_store.get()));
            } else {
                static_cast<void>(em.reorder_sibling(id, 0));
            }
        }
        if (ImGui::MenuItem("Move to Bottom", nullptr, false, can_move_down)) {
            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<ReorderEntityCommand>(&em, id, sib_count > 0 ? sib_count - 1 : 0, m_selection, m_entity_state_store.get()));
            } else {
                static_cast<void>(em.reorder_sibling(id, sib_count > 0 ? sib_count - 1 : 0));
            }
        }

        if (m_selection && !m_selection->empty()) {
            std::string group_label = m_selection->count() > 1
                ? "Group Selection (" + std::to_string(m_selection->count()) + ")"
                : "Group Selection";
            bool selection_has_prefab_root = false;
            if (m_prefab_stage && m_prefab_stage->is_active()) {
                for (EntityID sel_id : m_selection->get_selected()) {
                    if (m_prefab_stage->is_root_entity(sel_id)) {
                        selection_has_prefab_root = true;
                        break;
                    }
                }
            }
            if (ImGui::MenuItem(group_label.c_str(), "Cmd+G", false, !is_locked && !selection_has_prefab_root)) {
                std::vector<EntityID> selected_vec(m_selection->get_selected().begin(), m_selection->get_selected().end());
                if (m_command_history) {
                    m_command_history->execute_command(std::make_unique<GroupSelectedEntitiesCommand>(&em, m_selection, selected_vec, "Group", m_entity_state_store.get()));
                }
            }

            bool has_group = false;
            for (EntityID sel_id : m_selection->get_selected()) {
                if (em.is_alive(sel_id) && em.has_component<GroupComponent>(sel_id)) {
                    has_group = true;
                    break;
                }
            }
            if (has_group && ImGui::MenuItem("Ungroup", "Cmd+Shift+G", false, !is_locked)) {
                std::vector<EntityID> selected_vec(m_selection->get_selected().begin(), m_selection->get_selected().end());
                if (m_command_history) {
                    m_command_history->execute_command(std::make_unique<UngroupEntitiesCommand>(&em, m_selection, selected_vec, m_entity_state_store.get()));
                }
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem(is_locked ? "Unlock Selected" : "Lock Selected")) {
            std::vector<EntityID> entities = (m_selection && m_selection->is_selected(id))
                ? std::vector<EntityID>(m_selection->get_selected().begin(), m_selection->get_selected().end())
                : std::vector<EntityID>{id};
            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<SetEditorEntityStateCommand>(
                    &em, m_entity_state_store, EditorEntityStateField::Locked, entities, !is_locked
                ));
            } else if (m_entity_state_store) {
                for (EntityID e : entities) m_entity_state_store->set_locked(e, !is_locked);
            }
        }

        if (ImGui::MenuItem(is_visible ? "Hide Selected in Scene View" : "Show Selected in Scene View")) {
            std::vector<EntityID> entities = (m_selection && m_selection->is_selected(id))
                ? std::vector<EntityID>(m_selection->get_selected().begin(), m_selection->get_selected().end())
                : std::vector<EntityID>{id};
            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<SetEditorEntityStateCommand>(
                    &em, m_entity_state_store, EditorEntityStateField::VisibleInEditor, entities, !is_visible
                ));
            } else if (m_entity_state_store) {
                for (EntityID e : entities) m_entity_state_store->set_visible(e, !is_visible);
            }
        }

        ImGui::Separator();

        const bool is_prefab_root = m_prefab_stage && m_prefab_stage->is_root_entity(id);
        if (ImGui::MenuItem("Delete Entity", nullptr, false, !is_locked && !is_prefab_root)) {
            std::vector<EntityID> to_delete;
            if (m_selection) {
                for (EntityID s_id : m_selection->get_selected()) {
                    if (m_prefab_stage && m_prefab_stage->is_root_entity(s_id)) continue;
                    to_delete.push_back(s_id);
                }
            } else if (!is_prefab_root) {
                to_delete.push_back(id);
            }

            if (!to_delete.empty()) {
                if (m_command_history) {
                    m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, to_delete, m_entity_state_store.get()));
                } else {
                    for (EntityID s_id : to_delete) em.destroy_entity(s_id);
                    if (m_selection) m_selection->clear();
                }
            }
        }
        ImGui::EndPopup();
    }

    if (!runtime_mode && m_renaming_entity == id) {
        const ImVec2 cursor_after_row = ImGui::GetCursorScreenPos();
        const float label_x = node_min.x + ImGui::GetTreeNodeToLabelSpacing();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
        const float editor_height = ImGui::GetFrameHeight();
        const float editor_y = node_min.y + std::max(
            0.0f,
            ((node_max.y - node_min.y) - editor_height) * 0.5f
        );
        ImGui::SetCursorScreenPos(ImVec2(label_x, editor_y));
        ImGui::SetNextItemWidth(std::max(60.0f, node_max.x - label_x - 44.0f));
        if (m_focus_rename) {
            ImGui::SetKeyboardFocusHere();
            m_focus_rename = false;
        }
        const bool submitted = ImGui::InputText(
            ("##rename_entity_" + std::to_string(id)).c_str(),
            m_rename_buffer,
            sizeof(m_rename_buffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
        );
        const bool cancelled = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool lost_focus = ImGui::IsItemDeactivated() && !submitted && !cancelled;
        if (submitted || lost_focus) {
            finish_rename(em, true);
        } else if (cancelled) {
            finish_rename(em, false);
        }
        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos(cursor_after_row);
    }

    // Editor-only authoring state controls. Runtime hierarchy is structural read-only.
    if (!runtime_mode) {
    float right_margin = ImGui::GetWindowContentRegionMax().x - 42.0f;
    ImGui::SameLine(std::max(right_margin, 20.0f));

    // Visibility toggle: Visible (eye) vs Hidden (eye-off)
    const EditorIcon eye_icon = is_effectively_visible ? EditorIcon::Visible : EditorIcon::Hidden;
    const char* eye_tooltip = !is_visible
        ? "Hidden in Scene View (Click to Show)"
        : (is_ancestor_hidden ? "Hidden by Ancestor in Scene View (Click to Hide Self)" : "Visible in Scene View (Click to Hide)");
    const std::string eye_id = "##eye_" + std::to_string(id);
    if (editor_icon_button(eye_id.c_str(), eye_icon, eye_tooltip, !is_effectively_visible, ImVec2(16.0f, 16.0f))) {
        if (m_command_history) {
            m_command_history->execute_command(std::make_unique<SetEditorEntityStateCommand>(
                &em, m_entity_state_store, EditorEntityStateField::VisibleInEditor, id, !is_visible
            ));
        } else if (m_entity_state_store) {
            m_entity_state_store->set_visible(id, !is_visible);
        }
    }

    ImGui::SameLine(0.0f, 2.0f);

    // Lock toggle: Lock (locked) vs Unlock (unlocked)
    const EditorIcon lock_icon = is_locked ? EditorIcon::Lock : EditorIcon::Unlock;
    const char* lock_tooltip = is_locked ? "Entity Locked (Click to Unlock)" : "Entity Unlocked (Click to Lock)";
    const std::string lock_id = "##lock_" + std::to_string(id);
    if (editor_icon_button(lock_id.c_str(), lock_icon, lock_tooltip, is_locked, ImVec2(16.0f, 16.0f))) {
        if (m_command_history) {
            m_command_history->execute_command(std::make_unique<SetEditorEntityStateCommand>(
                &em, m_entity_state_store, EditorEntityStateField::Locked, id, !is_locked
            ));
        } else if (m_entity_state_store) {
            m_entity_state_store->set_locked(id, !is_locked);
        }
    }
    }

    // Render Children recursively if tree node is expanded
    if (has_children && node_open) {
        if (em.is_alive(id) && em.has_component<HierarchyComponent>(id)) {
            auto children_copy = em.get_component<HierarchyComponent>(id).children;
            for (EntityID child_id : children_copy) {
                if (em.is_alive(child_id)) {
                    draw_entity_node(em, child_id, filter_str, is_multi_key);
                }
            }
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::create_entity_helper(EntityManager& em, EntityID parent_id) {
    create_component_entity_helper(em, parent_id, "transform", "Empty Entity");
}

void SceneHierarchyPanel::create_tilemap_helper(EntityManager& em, EntityID parent_id) {
    create_component_entity_helper(em, parent_id, "tilemap", "Tilemap");
}

void SceneHierarchyPanel::render_create_menu(EntityManager& em, EntityID parent_id) {
    if (ImGui::MenuItem("Empty Entity")) {
        create_entity_helper(em, parent_id);
    }
    ImGui::Separator();
    ImGui::TextDisabled("2D");
    if (ImGui::MenuItem("Sprite")) {
        create_component_entity_helper(em, parent_id, "sprite", "Sprite");
    }
    if (ImGui::MenuItem("Tilemap")) {
        create_tilemap_helper(em, parent_id);
    }
    if (ImGui::MenuItem("Camera 2D")) {
        create_component_entity_helper(em, parent_id, "camera", "Camera 2D");
    }
    if (ImGui::MenuItem("Light 2D")) {
        create_component_entity_helper(em, parent_id, "light_2d", "Light 2D");
    }

    ImGui::Separator();
    ImGui::TextDisabled("UI");
    if (ImGui::MenuItem("Canvas")) {
        create_component_entity_helper(em, parent_id, "canvas", "Canvas");
    }
    const bool can_create_ui_child = has_canvas_ancestor(em, parent_id);
    const auto ui_item = [&](const char* label, const char* component_key, const char* name) {
        if (ImGui::MenuItem(label, nullptr, false, can_create_ui_child)) {
            create_component_entity_helper(em, parent_id, component_key, name);
        }
        if (!can_create_ui_child && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Select a Canvas or one of its UI children first");
        }
    };
    ui_item("Text", "ui_text", "UI Text");
    ui_item("Image", "ui_image", "UI Image");
    ui_item("Button", "ui_button", "UI Button");
}

void SceneHierarchyPanel::create_component_entity_helper(
    EntityManager& em,
    EntityID parent_id,
    const std::string& component_key,
    const std::string& name
) {
    if (m_prefab_stage && m_prefab_stage->is_active()) {
        if (parent_id == 0) {
            parent_id = m_prefab_stage->get_root_entity();
        }
        if (parent_id == 0 || !em.is_alive(parent_id)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot create entity: Prefab Stage root entity is zero or invalid.");
            return;
        }
    }

    if (parent_id != 0 && m_entity_state_store && m_entity_state_store->is_locked(parent_id)) {
        ENGINE_LOG_WARN("[HIERARCHY] Cannot create child entity under locked parent #{}", parent_id);
        return;
    }

    EntitySnapshot snap;
    snap.registered_components.set(TagComponent{name});
    std::string error;
    const EditorInspectorContext context{
        m_renderer,
        &AssetManager::get(),
        m_command_history.get()
    };
    if (!default_editor_component_registry().build_default_snapshot(
            component_key, em, 0, context, snap.registered_components, error
        )) {
        ENGINE_LOG_ERROR(
            "[HIERARCHY] Cannot create '{}': {}", name, error
        );
        return;
    }
    if (parent_id != 0) {
        snap.has_hierarchy = true;
        snap.hierarchy.parent = parent_id;
    }

    if (m_command_history) {
        m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, m_selection, snap, m_entity_state_store.get()));
    } else {
        CreateEntityCommand command(
            &em, m_selection, std::move(snap), m_entity_state_store.get()
        );
        static_cast<void>(command.execute());
    }
}

void SceneHierarchyPanel::create_asset_helper(EntityManager& em, EntityID parent_id, const std::string& path_str) {
    std::filesystem::path asset_path(path_str);
    std::string ext = asset_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext != ".prefab" && ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp") {
        return;
    }

    // In Prefab Stage: dropping to empty hierarchy redirects under stage root
    if (m_prefab_stage && m_prefab_stage->is_active()) {
        if (parent_id == 0) {
            parent_id = m_prefab_stage->get_root_entity();
        }
        if (parent_id == 0 || !em.is_alive(parent_id)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot instantiate asset: Prefab Stage root entity is zero or invalid.");
            return;
        }
    }

    if (parent_id != 0) {
        if (!em.is_alive(parent_id)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot instantiate asset: parent entity #{} is dead or invalid.", parent_id);
            return;
        }
        if (m_entity_state_store && m_entity_state_store->is_locked(parent_id)) {
            ENGINE_LOG_WARN("[HIERARCHY] Cannot instantiate asset: parent entity #{} is locked.", parent_id);
            return;
        }
    }

    if (ext == ".prefab") {
        if (m_command_history) {
            m_command_history->execute_command(std::make_unique<InstantiatePrefabCommand>(
                &em, m_selection, path_str, false, std::nullopt, parent_id, m_renderer, m_entity_state_store.get()
            ));
        } else {
            InstantiatePrefabCommand cmd(&em, m_selection, path_str, false, std::nullopt, parent_id, m_renderer, m_entity_state_store.get());
            cmd.execute();
        }
        return;
    }

    std::string tag_name = asset_path.stem().string();
    if (tag_name.empty()) tag_name = "New_Sprite";

    constexpr glm::vec2 DEFAULT_SPRITE_SIZE{64.0f, 64.0f};
    Texture* tex = AssetManager::get().get_texture(m_renderer, path_str);
    glm::vec2 sprite_size = tex ? glm::vec2(static_cast<float>(tex->get_width()), static_cast<float>(tex->get_height())) : DEFAULT_SPRITE_SIZE;

    EntitySnapshot snap;
    snap.registered_components.set(TagComponent{tag_name});
    snap.registered_components.set(TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
    snap.registered_components.set(SpriteComponent{
        tex ? tex : AssetManager::get().get_white_texture(m_renderer),
        sprite_size,
        SDL_Color{255, 255, 255, 255}
    });
    if (parent_id != 0) {
        snap.has_hierarchy = true;
        snap.hierarchy.parent = parent_id;
    }

    if (m_command_history) {
        m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, m_selection, std::move(snap), m_entity_state_store.get()));
    } else {
        CreateEntityCommand cmd(&em, m_selection, std::move(snap), m_entity_state_store.get());
        cmd.execute();
    }
}

} // namespace Engine
