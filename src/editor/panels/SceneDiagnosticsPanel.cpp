#include "SceneDiagnosticsPanel.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../scene/Scene.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/SceneValidator.hpp"
#include "../commands/HierarchyCommands.hpp"

#include <imgui.h>

namespace Engine {

SceneDiagnosticsPanel::SceneDiagnosticsPanel(
    SceneManager* scene_manager,
    AssetDatabase* asset_database,
    std::shared_ptr<EditorSelection> selection
) : m_scene_manager(scene_manager),
    m_asset_database(asset_database),
    m_selection(std::move(selection)) {}

void SceneDiagnosticsPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(420.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    if (!active_scene) {
        ImGui::TextDisabled("No active scene loaded.");
        ImGui::End();
        return;
    }

    const std::vector<SceneDiagnostic> diagnostics = SceneValidator::validate(
        active_scene->get_entity_manager(), m_asset_database
    );
    std::size_t warnings = 0;
    std::size_t errors = 0;
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        const SceneDiagnostic& diagnostic = diagnostics[index];
        diagnostic.severity == SceneDiagnosticSeverity::Error ? ++errors : ++warnings;
    }

    ImGui::Text("%s", active_scene->get_name().c_str());
    ImGui::SameLine();
    if (errors > 0) {
        ImGui::TextColored(ImVec4(0.95f, 0.36f, 0.32f, 1.0f), "%zu Errors", errors);
    } else {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "No errors");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu Warnings", warnings);
    ImGui::Separator();

    if (diagnostics.empty()) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Scene validation passed.");
        ImGui::End();
        return;
    }

    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        const SceneDiagnostic& diagnostic = diagnostics[index];
        const bool is_error = diagnostic.severity == SceneDiagnosticSeverity::Error;
        const ImVec4 color = is_error
            ? ImVec4(0.95f, 0.36f, 0.32f, 1.0f)
            : ImVec4(0.95f, 0.70f, 0.25f, 1.0f);
        const char* severity = is_error ? "Error" : "Warning";
        const std::string label = std::string(severity) + ": " + diagnostic.message;

        ImGui::PushID(static_cast<int>(index));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const bool selected = m_selection && diagnostic.entity != 0 && m_selection->is_selected(diagnostic.entity);
        if (ImGui::Selectable(label.c_str(), selected, diagnostic.entity == 0 ? ImGuiSelectableFlags_Disabled : 0)) {
            if (m_selection && diagnostic.entity != 0) {
                m_selection->select(diagnostic.entity, false);
            }
        }
        ImGui::PopStyleColor();

        if (diagnostic.entity != 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Select entity #%u", diagnostic.entity);
        }
        if (diagnostic.code == SceneDiagnosticCode::MissingHierarchyChild &&
            diagnostic.related_entity != 0 && m_command_history) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Fix")) {
                m_command_history->execute_command(std::make_unique<RemoveMissingHierarchyChildReferenceCommand>(
                    &active_scene->get_entity_manager(), diagnostic.entity, diagnostic.related_entity
                ));
            }
        }
        if (!diagnostic.asset_path.empty() && m_on_reveal_asset) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Show Asset")) {
                m_on_reveal_asset(diagnostic.asset_path);
            }
        }
        ImGui::PopID();
    }

    ImGui::End();
}

} // namespace Engine
