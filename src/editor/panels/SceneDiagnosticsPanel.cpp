#include "SceneDiagnosticsPanel.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../scene/Scene.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/SceneValidator.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/HierarchyCommands.hpp"
#include "../../ecs/components/UIComponents.hpp"

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
        if (diagnostic.asset_handle.is_valid()) {
            ImGui::SameLine();
            ImGui::TextDisabled("Handle: %s", diagnostic.asset_handle.to_string().c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("Missing asset handle: %s", diagnostic.asset_handle.to_string().c_str());
            }
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
        const bool clearable_asset_handle = diagnostic.code == SceneDiagnosticCode::MissingTextureAsset ||
            diagnostic.code == SceneDiagnosticCode::MissingMaterialAsset ||
            diagnostic.code == SceneDiagnosticCode::MissingRenderEffectAsset ||
            diagnostic.code == SceneDiagnosticCode::MissingUIImageTexture ||
            diagnostic.code == SceneDiagnosticCode::MissingUIFont;
        if (clearable_asset_handle && diagnostic.asset_handle.is_valid() && m_command_history) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear Reference")) {
                m_pending_clear = diagnostic;
                ImGui::OpenPopup("Clear Missing Asset Reference");
            }
        }
        ImGui::PopID();
    }

    if (ImGui::BeginPopupModal("Clear Missing Asset Reference", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This clears the broken asset reference from the entity.");
        ImGui::TextUnformatted("The operation can be undone.");
        ImGui::Spacing();
        if (ImGui::Button("Clear Reference", ImVec2(140.0f, 0.0f))) {
            if (m_pending_clear && m_command_history &&
                active_scene->get_entity_manager().is_alive(m_pending_clear->entity)) {
                EntityManager& entities = active_scene->get_entity_manager();
                if (m_pending_clear->code == SceneDiagnosticCode::MissingRenderEffectAsset &&
                    entities.has_component<RenderEffectComponent>(m_pending_clear->entity)) {
                    const RenderEffectComponent before =
                        entities.get_component<RenderEffectComponent>(m_pending_clear->entity);
                    if (before.profile_handle == m_pending_clear->asset_handle) {
                        auto after = before;
                        after.profile_handle = {};
                        m_command_history->execute_command(std::make_unique<SetComponentCommand<RenderEffectComponent>>(
                            &entities,
                            m_pending_clear->entity,
                            before,
                            after,
                            "Clear Missing Render Effect Reference"
                        ));
                    }
                } else if (m_pending_clear->code == SceneDiagnosticCode::MissingUIFont &&
                           entities.has_component<UITextComponent>(m_pending_clear->entity)) {
                    const UITextComponent before =
                        entities.get_component<UITextComponent>(m_pending_clear->entity);
                    if (before.font_handle == m_pending_clear->asset_handle) {
                        auto after = before;
                        after.font_handle = {};
                        m_command_history->execute_command(std::make_unique<SetComponentCommand<UITextComponent>>(
                            &entities,
                            m_pending_clear->entity,
                            before,
                            after,
                            "Clear Missing UI Font Reference"
                        ));
                    }
                } else if (m_pending_clear->code == SceneDiagnosticCode::MissingUIImageTexture &&
                           entities.has_component<UIImageComponent>(m_pending_clear->entity)) {
                    const UIImageComponent before =
                        entities.get_component<UIImageComponent>(m_pending_clear->entity);
                    if (before.texture_handle == m_pending_clear->asset_handle) {
                        auto after = before;
                        after.texture_handle = {};
                        m_command_history->execute_command(std::make_unique<SetComponentCommand<UIImageComponent>>(
                            &entities,
                            m_pending_clear->entity,
                            before,
                            after,
                            "Clear Missing UI Image Reference"
                        ));
                    }
                } else if (m_pending_clear->code == SceneDiagnosticCode::MissingMaterialAsset &&
                           entities.has_component<UITextComponent>(m_pending_clear->entity) &&
                           entities.get_component<UITextComponent>(m_pending_clear->entity).material_handle ==
                               m_pending_clear->asset_handle) {
                    const UITextComponent before =
                        entities.get_component<UITextComponent>(m_pending_clear->entity);
                    auto after = before;
                    after.material_handle = {};
                    after.material.reset();
                    m_command_history->execute_command(std::make_unique<SetComponentCommand<UITextComponent>>(
                        &entities,
                        m_pending_clear->entity,
                        before,
                        after,
                        "Clear Missing UI Text Material Reference"
                    ));
                } else if (m_pending_clear->code == SceneDiagnosticCode::MissingMaterialAsset &&
                           entities.has_component<UIImageComponent>(m_pending_clear->entity) &&
                           entities.get_component<UIImageComponent>(m_pending_clear->entity).material_handle ==
                               m_pending_clear->asset_handle) {
                    const UIImageComponent before =
                        entities.get_component<UIImageComponent>(m_pending_clear->entity);
                    auto after = before;
                    after.material_handle = {};
                    after.material.reset();
                    m_command_history->execute_command(std::make_unique<SetComponentCommand<UIImageComponent>>(
                        &entities,
                        m_pending_clear->entity,
                        before,
                        after,
                        "Clear Missing UI Image Material Reference"
                    ));
                } else if (entities.has_component<SpriteComponent>(m_pending_clear->entity)) {
                    const SpriteComponent before = entities.get_component<SpriteComponent>(m_pending_clear->entity);
                    SpriteComponent after = before;
                    bool changed = false;
                    if (m_pending_clear->code == SceneDiagnosticCode::MissingTextureAsset &&
                        after.texture_handle == m_pending_clear->asset_handle) {
                        after.texture_handle = {};
                        changed = true;
                    } else if (m_pending_clear->code == SceneDiagnosticCode::MissingMaterialAsset &&
                               after.material_handle == m_pending_clear->asset_handle) {
                        after.material_handle = {};
                        after.material.reset();
                        changed = true;
                    }
                    if (changed) {
                        m_command_history->execute_command(std::make_unique<SetComponentCommand<SpriteComponent>>(
                            &entities,
                            m_pending_clear->entity,
                            before,
                            after,
                            "Clear Missing Sprite Asset Reference"
                        ));
                    }
                }
            }
            m_pending_clear.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            m_pending_clear.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace Engine
