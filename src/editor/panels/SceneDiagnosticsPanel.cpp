#include "SceneDiagnosticsPanel.hpp"
#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../scene/Scene.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/SceneValidator.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/HierarchyCommands.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/UIComponents.hpp"
#include "../../ecs/components/ScriptingComponents.hpp"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace Engine {

namespace {

enum class DiagnosticGroup : std::size_t {
    Scene,
    Hierarchy,
    Assets,
    Components,
    Count
};

constexpr std::array<DiagnosticGroup, 4> DIAGNOSTIC_GROUPS{
    DiagnosticGroup::Scene,
    DiagnosticGroup::Hierarchy,
    DiagnosticGroup::Assets,
    DiagnosticGroup::Components
};

DiagnosticGroup diagnostic_group(SceneDiagnosticCode code) {
    switch (code) {
        case SceneDiagnosticCode::MissingPrimaryCamera:
        case SceneDiagnosticCode::DuplicatePrimaryCamera:
            return DiagnosticGroup::Scene;
        case SceneDiagnosticCode::MissingHierarchyParent:
        case SceneDiagnosticCode::MissingHierarchyChild:
        case SceneDiagnosticCode::HierarchyLinkMismatch:
        case SceneDiagnosticCode::HierarchyCycle:
            return DiagnosticGroup::Hierarchy;
        case SceneDiagnosticCode::MissingTextureAsset:
        case SceneDiagnosticCode::MissingMaterialAsset:
        case SceneDiagnosticCode::MissingRenderEffectAsset:
        case SceneDiagnosticCode::MissingScriptAsset:
        case SceneDiagnosticCode::MissingPrefabAsset:
        case SceneDiagnosticCode::MissingTilemapTileset:
        case SceneDiagnosticCode::MissingUIImageTexture:
        case SceneDiagnosticCode::MissingUIFont:
            return DiagnosticGroup::Assets;
        case SceneDiagnosticCode::InvalidColliderShape:
        case SceneDiagnosticCode::InvalidTilemapDimensions:
        case SceneDiagnosticCode::InvalidLight2D:
        case SceneDiagnosticCode::InvalidShadowCaster2D:
        case SceneDiagnosticCode::MissingShadowCasterCollider:
        case SceneDiagnosticCode::InvalidCanvas:
        case SceneDiagnosticCode::InvalidUILayout:
        case SceneDiagnosticCode::InvalidUIText:
        case SceneDiagnosticCode::MissingUILayout:
        case SceneDiagnosticCode::MissingCanvasAncestor:
            return DiagnosticGroup::Components;
    }
    return DiagnosticGroup::Components;
}

const char* group_name(DiagnosticGroup group) {
    switch (group) {
        case DiagnosticGroup::Scene: return "Scene";
        case DiagnosticGroup::Hierarchy: return "Hierarchy";
        case DiagnosticGroup::Assets: return "Assets";
        case DiagnosticGroup::Components: return "Components";
        case DiagnosticGroup::Count: break;
    }
    return "Diagnostics";
}

EditorIcon group_icon(DiagnosticGroup group) {
    switch (group) {
        case DiagnosticGroup::Scene: return EditorIcon::Camera;
        case DiagnosticGroup::Hierarchy: return EditorIcon::Component;
        case DiagnosticGroup::Assets: return EditorIcon::Folder;
        case DiagnosticGroup::Components: return EditorIcon::Settings;
        case DiagnosticGroup::Count: break;
    }
    return EditorIcon::Shield;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string entity_context(const SceneDiagnostic& diagnostic, const EntityManager& entities) {
    if (diagnostic.entity == 0) return "Scene-wide";
    if (entities.is_alive(diagnostic.entity) &&
        entities.has_component<TagComponent>(diagnostic.entity)) {
        return entities.get_component<TagComponent>(diagnostic.entity).tag +
            " (#" + std::to_string(diagnostic.entity) + ")";
    }
    return "Entity #" + std::to_string(diagnostic.entity);
}

std::string diagnostic_context(
    const SceneDiagnostic& diagnostic,
    const EntityManager& entities
) {
    std::string context = entity_context(diagnostic, entities);
    if (diagnostic.related_entity != 0) {
        context += " | Related entity #" + std::to_string(diagnostic.related_entity);
    }
    if (!diagnostic.asset_path.empty()) {
        context += " | Asset: " + diagnostic.asset_path;
    } else if (diagnostic.asset_handle.is_valid()) {
        context += " | Handle: " + diagnostic.asset_handle.to_string();
    }
    return context;
}

bool matches_filter(
    const SceneDiagnostic& diagnostic,
    const EntityManager& entities,
    const std::string& filter
) {
    if (filter.empty()) return true;
    std::string searchable = diagnostic.message + " " +
        diagnostic_context(diagnostic, entities) + " " +
        group_name(diagnostic_group(diagnostic.code));
    return lowercase(std::move(searchable)).find(filter) != std::string::npos;
}

bool can_clear_asset_reference(const SceneDiagnostic& diagnostic) {
    return diagnostic.code == SceneDiagnosticCode::MissingTextureAsset ||
        diagnostic.code == SceneDiagnosticCode::MissingMaterialAsset ||
        diagnostic.code == SceneDiagnosticCode::MissingRenderEffectAsset ||
        diagnostic.code == SceneDiagnosticCode::MissingScriptAsset ||
        diagnostic.code == SceneDiagnosticCode::MissingUIImageTexture ||
        diagnostic.code == SceneDiagnosticCode::MissingUIFont;
}

} // namespace

SceneDiagnosticsPanel::SceneDiagnosticsPanel(
    SceneManager* scene_manager,
    AssetDatabase* asset_database,
    std::shared_ptr<EditorSelection> selection
) : m_scene_manager(scene_manager),
    m_asset_database(asset_database),
    m_selection(std::move(selection)) {}

void SceneDiagnosticsPanel::draw_diagnostic_row(
    Scene& scene,
    const SceneDiagnostic& diagnostic,
    std::size_t index
) {
    EntityManager& entities = scene.get_entity_manager();
    const auto& metrics = EditorTheme::metrics();
    const bool is_error = diagnostic.severity == SceneDiagnosticSeverity::Error;
    const bool has_entity = diagnostic.entity != 0 && entities.is_alive(diagnostic.entity);
    const bool selected = has_entity && m_selection && m_selection->is_selected(diagnostic.entity);

    ImGui::PushID(static_cast<int>(index));
    ImGui::TableNextRow(ImGuiTableRowFlags_None, 44.0f * metrics.scale);
    ImGui::TableSetColumnIndex(0);

    EditorUi::badge(
        is_error ? "ERROR" : "WARNING",
        is_error ? EditorUi::BadgeTone::Error : EditorUi::BadgeTone::Warning
    );
    ImGui::SameLine();
    const float message_start = ImGui::GetCursorPosX();
    if (has_entity) {
        if (ImGui::Selectable(
                diagnostic.message.c_str(), selected, ImGuiSelectableFlags_None,
                ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing()))) {
            if (m_selection) m_selection->select(diagnostic.entity, false);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Select %s", entity_context(diagnostic, entities).c_str());
        }
    } else {
        ImGui::TextWrapped("%s", diagnostic.message.c_str());
    }
    ImGui::SetCursorPosX(message_start);
    ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::palette().text_muted);
    ImGui::TextWrapped("%s", diagnostic_context(diagnostic, entities).c_str());
    ImGui::PopStyleColor();

    ImGui::TableSetColumnIndex(1);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * metrics.scale);
    bool has_previous_action = false;
    const auto place_action = [&has_previous_action]() {
        if (has_previous_action) ImGui::SameLine();
        has_previous_action = true;
    };

    if (has_entity && m_selection) {
        place_action();
        if (EditorUi::action_button(
                "select_entity", EditorIcon::ExternalLink, "Select",
                EditorUi::ButtonTone::Neutral, true, ImVec2(78.0f * metrics.scale, 0.0f))) {
            m_selection->select(diagnostic.entity, false);
        }
    }
    if (diagnostic.code == SceneDiagnosticCode::MissingHierarchyChild &&
        diagnostic.related_entity != 0 && m_command_history) {
        place_action();
        if (EditorUi::action_button(
                "fix_hierarchy", EditorIcon::Zap, "Fix",
                EditorUi::ButtonTone::Primary, true, ImVec2(62.0f * metrics.scale, 0.0f))) {
            m_command_history->execute_command(
                std::make_unique<RemoveMissingHierarchyChildReferenceCommand>(
                    &entities, diagnostic.entity, diagnostic.related_entity
                )
            );
        }
    }
    if (!diagnostic.asset_path.empty() && m_on_reveal_asset) {
        place_action();
        if (EditorUi::action_button(
                "reveal_asset", EditorIcon::Folder, "Reveal",
                EditorUi::ButtonTone::Neutral, true, ImVec2(82.0f * metrics.scale, 0.0f))) {
            m_on_reveal_asset(diagnostic.asset_path);
        }
    }
    if (can_clear_asset_reference(diagnostic) &&
        diagnostic.asset_handle.is_valid() && m_command_history) {
        place_action();
        if (EditorUi::action_button(
                "clear_reference", EditorIcon::Clear, "Clear",
                EditorUi::ButtonTone::Danger, true, ImVec2(72.0f * metrics.scale, 0.0f))) {
            m_pending_clear = diagnostic;
        }
    }

    ImGui::PopID();
}

void SceneDiagnosticsPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(420.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    if (!active_scene) {
        EditorUi::empty_state(
            EditorIcon::Shield,
            "No active scene",
            "Open a scene to inspect validation issues"
        );
        ImGui::End();
        return;
    }

    const std::vector<SceneDiagnostic> diagnostics = SceneValidator::validate(
        active_scene->get_entity_manager(), m_asset_database
    );
    std::size_t warnings = 0;
    std::size_t errors = 0;
    for (const SceneDiagnostic& diagnostic : diagnostics) {
        diagnostic.severity == SceneDiagnosticSeverity::Error ? ++errors : ++warnings;
    }

    const auto& metrics = EditorTheme::metrics();
    const std::string error_label = std::to_string(errors) +
        (errors == 1 ? " ERROR" : " ERRORS");
    const std::string warning_label = std::to_string(warnings) +
        (warnings == 1 ? " WARNING" : " WARNINGS");
    if (ImGui::BeginTable(
            "##diagnostics_summary", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Status", ImGuiTableColumnFlags_WidthFixed, 190.0f * metrics.scale
        );
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Scene");
        ImGui::SameLine();
        ImGui::TextUnformatted(active_scene->get_name().c_str());
        ImGui::SameLine();
        EditorUi::badge("LIVE", EditorUi::BadgeTone::Accent);

        ImGui::TableSetColumnIndex(1);
        EditorUi::badge(
            error_label.c_str(),
            errors == 0 ? EditorUi::BadgeTone::Success : EditorUi::BadgeTone::Error
        );
        ImGui::SameLine();
        EditorUi::badge(
            warning_label.c_str(),
            warnings == 0 ? EditorUi::BadgeTone::Neutral : EditorUi::BadgeTone::Warning
        );
        ImGui::EndTable();
    }
    ImGui::Separator();

    if (diagnostics.empty()) {
        EditorUi::empty_state(
            EditorIcon::Shield,
            "Scene validation passed",
            "No errors or warnings were found"
        );
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable(
            "##diagnostics_filters", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Severity", ImGuiTableColumnFlags_WidthFixed, 178.0f * metrics.scale
        );
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        EditorUi::search_field(
            "##diagnostics_search", "Search diagnostics...",
            m_filter_buffer, sizeof(m_filter_buffer), ImGui::GetContentRegionAvail().x
        );
        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox("Errors", &m_show_errors);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &m_show_warnings);
        ImGui::EndTable();
    }
    ImGui::Separator();

    using IndexedDiagnostic = std::pair<std::size_t, const SceneDiagnostic*>;
    std::array<std::vector<IndexedDiagnostic>, static_cast<std::size_t>(DiagnosticGroup::Count)>
        grouped_diagnostics;
    const std::string filter = lowercase(m_filter_buffer);
    std::size_t visible_count = 0;
    EntityManager& entities = active_scene->get_entity_manager();
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        const SceneDiagnostic& diagnostic = diagnostics[index];
        if (diagnostic.severity == SceneDiagnosticSeverity::Error && !m_show_errors) continue;
        if (diagnostic.severity == SceneDiagnosticSeverity::Warning && !m_show_warnings) continue;
        if (!matches_filter(diagnostic, entities, filter)) continue;
        grouped_diagnostics[static_cast<std::size_t>(diagnostic_group(diagnostic.code))]
            .push_back({index, &diagnostic});
        ++visible_count;
    }

    if (visible_count == 0) {
        EditorUi::empty_state(
            EditorIcon::Shield,
            "No matching diagnostics",
            "Adjust the search or severity filters"
        );
    } else {
        ImGui::BeginChild("DiagnosticsList", ImVec2(0.0f, 0.0f), false);
        for (const DiagnosticGroup group : DIAGNOSTIC_GROUPS) {
            const auto& group_diagnostics =
                grouped_diagnostics[static_cast<std::size_t>(group)];
            if (group_diagnostics.empty()) continue;

            const std::string header = editor_icon_label(group_icon(group), group_name(group)) +
                "  " + std::to_string(group_diagnostics.size()) +
                "##diagnostic_group_" + std::to_string(static_cast<std::size_t>(group));
            ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
            if (!ImGui::CollapsingHeader(header.c_str())) continue;

            if (ImGui::BeginTable(
                    ("##diagnostic_rows_" + std::to_string(static_cast<std::size_t>(group))).c_str(),
                    2,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableSetupColumn("Issue", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(
                    "Actions", ImGuiTableColumnFlags_WidthFixed, 170.0f * metrics.scale
                );
                for (const auto& [index, diagnostic] : group_diagnostics) {
                    draw_diagnostic_row(*active_scene, *diagnostic, index);
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
        }
        ImGui::EndChild();
    }

    if (m_pending_clear && !ImGui::IsPopupOpen("Clear Missing Asset Reference")) {
        ImGui::OpenPopup("Clear Missing Asset Reference");
    }
    if (ImGui::BeginPopupModal("Clear Missing Asset Reference", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This clears the broken asset reference from the entity.");
        ImGui::TextUnformatted("The operation can be undone.");
        ImGui::Spacing();
        if (ImGui::Button("Clear Reference", ImVec2(140.0f, 0.0f))) {
            if (m_pending_clear && m_command_history &&
                active_scene->get_entity_manager().is_alive(m_pending_clear->entity)) {
                EntityManager& entities = active_scene->get_entity_manager();
                if (m_pending_clear->code == SceneDiagnosticCode::MissingScriptAsset &&
                    entities.has_component<ScriptComponent>(m_pending_clear->entity)) {
                    const ScriptComponent before =
                        entities.get_component<ScriptComponent>(m_pending_clear->entity);
                    auto after = before;
                    const auto instance = std::find_if(
                        after.instances.begin(), after.instances.end(),
                        [this](const ScriptBehaviourSlot& candidate) {
                            return candidate.script_handle == m_pending_clear->asset_handle;
                        }
                    );
                    if (instance != after.instances.end()) {
                        instance->script_handle = {};
                        instance->public_fields.clear();
                        m_command_history->execute_command(std::make_unique<SetComponentCommand<ScriptComponent>>(
                            &entities,
                            m_pending_clear->entity,
                            before,
                            after,
                            "Clear Missing Script Reference"
                        ));
                    }
                } else if (m_pending_clear->code == SceneDiagnosticCode::MissingRenderEffectAsset &&
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
