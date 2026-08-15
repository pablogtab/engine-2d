#include "SceneHierarchyPanel.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>

namespace Engine {

SceneHierarchyPanel::SceneHierarchyPanel(SceneManager* scene_manager)
    : m_scene_manager(scene_manager) {}

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

        ImGui::TextDisabled("Entities: %zu alive", living_entities.size());
        ImGui::Separator();

        // --- Entity List ---
        ImGui::BeginChild("EntityTreeScroll", ImVec2(0, -35), true);

        std::string filter_str = m_search_filter;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

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

            if (m_selected_entity == id) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s", label.c_str());
            if (ImGui::IsItemClicked()) {
                m_selected_entity = id;
            }

            // Right-click context menu per entity
            if (ImGui::BeginPopupContextItem()) {
                m_selected_entity = id;
                if (ImGui::MenuItem("Delete Entity")) {
                    em.destroy_entity(id);
                    if (m_selected_entity == id) m_selected_entity = 0;
                }
                ImGui::EndPopup();
            }
        }

        ImGui::EndChild();

        // --- Bottom Actions Bar ---
        if (ImGui::Button(" + Add Entity ", ImVec2(100, 24))) {
            EntityID new_id = em.create_entity();
            em.add_component<TagComponent>(new_id, TagComponent{"GameObject"});
            em.add_component<TransformComponent>(new_id, TransformComponent{glm::vec3(0.0f)});
            m_selected_entity = new_id;
        }
        ImGui::SameLine();
        if (m_selected_entity != 0 && em.is_alive(m_selected_entity)) {
            if (ImGui::Button(" Delete ", ImVec2(70, 24))) {
                em.destroy_entity(m_selected_entity);
                m_selected_entity = 0;
            }
        }
    }
    ImGui::End();
}

} // namespace Engine
