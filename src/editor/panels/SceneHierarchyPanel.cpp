#include "SceneHierarchyPanel.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include "../../ecs/Components.hpp"
#include "../../renderer/Texture.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>

namespace Engine {

SceneHierarchyPanel::SceneHierarchyPanel(SceneManager* scene_manager, SDL_Renderer* renderer)
    : m_scene_manager(scene_manager), m_renderer(renderer) {}

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

        if (m_selected_entities.size() > 1) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Selected: %zu entities", m_selected_entities.size());
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

            if (m_selected_entities.contains(id)) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s", label.c_str());
            if (ImGui::IsItemClicked()) {
                select_entity(id, is_multi_key);
            }

            // Right-click context menu per entity
            if (ImGui::BeginPopupContextItem()) {
                if (!m_selected_entities.contains(id)) {
                    select_entity(id, false);
                }
                if (ImGui::MenuItem("Delete Entity")) {
                    for (EntityID s_id : m_selected_entities) {
                        em.destroy_entity(s_id);
                    }
                    m_selected_entities.clear();
                }
                ImGui::EndPopup();
            }
        }

        ImGui::EndChild();

        // --- Bottom Actions Bar ---
        if (ImGui::Button(" + Add Entity ", ImVec2(100, 24))) {
            EntityID new_id = em.create_entity();
            em.add_component<TagComponent>(new_id, TagComponent{"Entity_" + std::to_string(new_id)});
            em.add_component<TransformComponent>(new_id, TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
            
            Texture* tex = Texture::get_white(m_renderer);
            em.add_component<SpriteComponent>(new_id, SpriteComponent{tex, glm::vec2(64.0f, 64.0f), SDL_Color{255, 255, 255, 255}});

            set_selected_entity(new_id);
        }
        ImGui::SameLine();
        if (!m_selected_entities.empty()) {
            std::string del_label = m_selected_entities.size() > 1 ? " Delete (" + std::to_string(m_selected_entities.size()) + ") " : " Delete ";
            if (ImGui::Button(del_label.c_str(), ImVec2(90, 24))) {
                for (EntityID s_id : m_selected_entities) {
                    em.destroy_entity(s_id);
                }
                m_selected_entities.clear();
            }
        }
    }
    ImGui::End();
}

} // namespace Engine
