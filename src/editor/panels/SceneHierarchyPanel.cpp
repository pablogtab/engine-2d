#include "SceneHierarchyPanel.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
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

        // --- Entity List ---
        ImGui::BeginChild("EntityTreeScroll", ImVec2(0, -35), true);

        std::string filter_str = m_search_filter;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        ImGuiIO& io = ImGui::GetIO();
        bool is_multi_key = io.KeyCtrl || io.KeyShift;

        for (EntityID id : living_entities) {
            if (!em.is_alive(id)) continue;

            std::string label = "Entity #" + std::to_string(id);
            if (em.has_component<TagComponent>(id)) {
                label = em.get_component<TagComponent>(id).tag + " (ID: " + std::to_string(id) + ")";
            }

            // Apply search filter
            if (!filter_str.empty()) {
                std::string lower_label = label;
                std::transform(lower_label.begin(), lower_label.end(), lower_label.begin(), ::tolower);
                if (lower_label.find(filter_str) == std::string::npos) {
                    continue;
                }
            }

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | 
                                      ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                      ImGuiTreeNodeFlags_SpanAvailWidth;

            if (m_selection && m_selection->is_selected(id)) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s", label.c_str());
            if (ImGui::IsItemClicked()) {
                if (m_selection) {
                    m_selection->select(id, is_multi_key);
                }
            }

            // Right-click context menu per entity
            if (ImGui::BeginPopupContextItem()) {
                if (m_selection && !m_selection->is_selected(id)) {
                    m_selection->select(id, false);
                }
                if (ImGui::MenuItem("Delete Entity")) {
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
        }

        ImGui::EndChild();

        // --- Bottom Actions Bar ---
        if (ImGui::Button(" + Add Entity ", ImVec2(100, 24))) {
            EntitySnapshot snap;
            snap.tag = TagComponent{"New_Entity"};
            snap.transform = TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)};
            snap.has_sprite = true;
            snap.sprite = SpriteComponent{AssetManager::get().get_white_texture(m_renderer), glm::vec2(64.0f, 64.0f), SDL_Color{255, 255, 255, 255}};

            if (m_command_history) {
                m_command_history->execute_command(std::make_unique<CreateEntityCommand>(&em, m_selection, snap));
            } else {
                EntityID new_id = em.create_entity();
                em.add_component<TagComponent>(new_id, snap.tag);
                em.add_component<TransformComponent>(new_id, snap.transform);
                em.add_component<SpriteComponent>(new_id, snap.sprite);
                if (m_selection) m_selection->select(new_id, false);
            }
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
    }
    ImGui::End();
}

} // namespace Engine
