#include "ScriptInspectorDrawer.hpp"

#include "AssetDropTarget.hpp"
#include "ComponentInspectorTransaction.hpp"
#include "InspectorPropertyPolicies.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../scripting/ScriptAnalyzer.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/ScriptingComponents.hpp"
#include "../../scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <unordered_map>

namespace Engine {
namespace {

struct CachedScriptInfo {
    std::filesystem::file_time_type modified{};
    ScriptInspectionResult inspection;
    ScriptAnalysisResult analysis;
};

const CachedScriptInfo& script_info(AssetHandle handle) {
    static std::unordered_map<AssetHandle, CachedScriptInfo> cache;
    AssetDatabase& database = AssetDatabase::get();
    const std::string path = database.resolve(handle);
    std::error_code error;
    const auto modified = path.empty()
        ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, error);
    auto found = cache.find(handle);
    if (found == cache.end() || error || found->second.modified != modified) {
        CachedScriptInfo refreshed;
        refreshed.modified = modified;
        refreshed.inspection = ScriptRuntime::inspect_asset(handle, database);
        refreshed.analysis = ScriptAnalyzer::analyze_asset(handle, database);
        found = cache.insert_or_assign(handle, std::move(refreshed)).first;
    }
    return found->second;
}

void track_edit(
    EntityManager& entity_manager,
    EntityID entity,
    const ScriptComponent& before,
    const char* property,
    const char* description,
    const EditorInspectorContext& context,
    bool force_commit = false
) {
    track_component_edit<ScriptComponent>(
        entity_manager,
        entity,
        before,
        property,
        description,
        {context.command_history, context.transactions},
        std::equal_to<ScriptComponent>{},
        {},
        force_commit
    );
}

void synchronize_fields(ScriptBehaviourSlot& instance, const std::vector<ScriptPublicField>& defaults) {
    std::vector<ScriptPublicField> synchronized;
    synchronized.reserve(defaults.size());
    for (const ScriptPublicField& definition : defaults) {
        const auto existing = std::find_if(
            instance.public_fields.begin(), instance.public_fields.end(),
            [&definition](const ScriptPublicField& field) {
                return field.name == definition.name && field.type == definition.type &&
                    script_field_value_matches_type(field);
            }
        );
        synchronized.push_back(existing == instance.public_fields.end() ? definition : *existing);
    }
    instance.public_fields = std::move(synchronized);
}

bool draw_script_asset(ScriptBehaviourSlot& instance) {
    InspectorWidgets::begin_property_row("Script");
    AssetDatabase& database = AssetDatabase::get();
    const AssetRecord* selected = instance.script_handle.is_valid()
        ? database.find(instance.script_handle) : nullptr;
    const char* preview = selected ? selected->source_path.c_str() : "None";
    bool changed = false;
    if (ImGui::BeginCombo("##script_asset", preview)) {
        if (ImGui::Selectable("None", !instance.script_handle.is_valid())) {
            instance.script_handle = {};
            instance.public_fields.clear();
            changed = true;
        }
        for (const AssetRecord& record : database.records()) {
            if (record.type != AssetType::Script || record.status != AssetStatus::Ready) continue;
            const bool is_selected = instance.script_handle == record.handle;
            if (ImGui::Selectable(record.source_path.c_str(), is_selected)) {
                instance.script_handle = record.handle;
                const ScriptInspectionResult& inspected = script_info(record.handle).inspection;
                instance.public_fields = inspected.success
                    ? inspected.public_fields : std::vector<ScriptPublicField>{};
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (const auto dropped = accept_asset_drop_target(database, AssetType::Script)) {
        if (*dropped != instance.script_handle) {
            instance.script_handle = *dropped;
            const ScriptInspectionResult& inspected = script_info(*dropped).inspection;
            instance.public_fields = inspected.success
                ? inspected.public_fields : std::vector<ScriptPublicField>{};
            changed = true;
        }
    }
    return changed;
}

bool draw_asset_field(const char* label, AssetHandle& value) {
    InspectorWidgets::begin_property_row(label);
    AssetDatabase& database = AssetDatabase::get();
    const AssetRecord* selected = value.is_valid() ? database.find(value) : nullptr;
    bool changed = false;
    if (ImGui::BeginCombo((std::string("##script_field_") + label).c_str(),
            selected ? selected->source_path.c_str() : "None")) {
        if (ImGui::Selectable("None", !value.is_valid())) {
            value = {};
            changed = true;
        }
        for (const AssetRecord& record : database.records()) {
            if (record.status != AssetStatus::Ready || record.type == AssetType::Unknown) continue;
            const bool is_selected = value == record.handle;
            if (ImGui::Selectable(record.source_path.c_str(), is_selected)) {
                value = record.handle;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (const auto dropped = accept_asset_drop_target(database)) {
        if (*dropped != value) {
            value = *dropped;
            changed = true;
        }
    }
    return changed;
}

void draw_diagnostics(AssetHandle handle) {
    if (!handle.is_valid()) {
        ImGui::TextDisabled("Assign a .e2s asset to inspect its public fields.");
        return;
    }
    const CachedScriptInfo& info = script_info(handle);
    const ScriptInspectionResult& inspected = info.inspection;
    if (!inspected.success) {
        ImGui::TextWrapped("Metadata error: %s", inspected.error.c_str());
        return;
    }
    const ScriptAnalysisResult& analyzed = info.analysis;
    if (analyzed.success) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.50f, 1.0f), "EngineScript analysis passed");
        return;
    }
    for (const ScriptDiagnostic& diagnostic : analyzed.diagnostics) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
            "Line %d:%d",
            diagnostic.line,
            diagnostic.column
        );
        ImGui::SameLine();
        ImGui::TextWrapped("%s", diagnostic.message.c_str());
    }
}

} // namespace

void ScriptInspectorDrawer::draw(
    EntityManager& entity_manager,
    EntityID entity,
    const EditorInspectorContext& context
) {
    if (!entity_manager.has_component<ScriptComponent>(entity)) return;
    const auto header = InspectorWidgets::registered_component_header("script");
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ScriptComponent>>(
            &entity_manager,
            entity,
            entity_manager.get_component<ScriptComponent>(entity),
            std::nullopt,
            "Remove Script"
        ));
        return;
    }
    if (!header.open) return;

    ImGui::PushID("ScriptComponentDrawer");
    ScriptComponent& component = entity_manager.get_component<ScriptComponent>(entity);
    for (std::size_t index = 0; index < component.instances.size(); ++index) {
        ScriptBehaviourSlot& instance = component.instances[index];
        ImGui::PushID(static_cast<int>(instance.id));
        ImGui::Separator();
        const AssetRecord* record = instance.script_handle.is_valid()
            ? AssetDatabase::get().find(instance.script_handle) : nullptr;
        ImGui::Text("Behaviour %zu: %s%s", index + 1,
            record ? record->source_path.c_str() : "Unassigned",
            instance.runtime_only ? " (Runtime)" : "");

        if (index > 0 && ImGui::SmallButton("Up")) {
            const ScriptComponent before = component;
            std::swap(component.instances[index], component.instances[index - 1]);
            track_edit(entity_manager, entity, before, "Script.Order", "Reorder Script Behaviour", context, true);
            ImGui::PopID();
            break;
        }
        if (index + 1 < component.instances.size()) {
            if (index > 0) ImGui::SameLine();
            if (ImGui::SmallButton("Down")) {
                const ScriptComponent before = component;
                std::swap(component.instances[index], component.instances[index + 1]);
                track_edit(entity_manager, entity, before, "Script.Order", "Reorder Script Behaviour", context, true);
                ImGui::PopID();
                break;
            }
        }
        if (!instance.runtime_only) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Duplicate")) {
                const ScriptComponent before = component;
                ScriptBehaviourSlot duplicate = instance;
                duplicate.id = make_script_slot_id();
                component.instances.insert(component.instances.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(duplicate));
                track_edit(entity_manager, entity, before, "Script.Duplicate", "Duplicate Script Behaviour", context, true);
                ImGui::PopID();
                break;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            const ScriptComponent before = component;
            component.instances.erase(component.instances.begin() + static_cast<std::ptrdiff_t>(index));
            track_edit(entity_manager, entity, before, "Script.Remove", "Remove Script Behaviour", context, true);
            ImGui::PopID();
            break;
        }

        const ScriptComponent enabled_before = component;
        InspectorWidgets::property_bool("Enabled", &instance.enabled);
        track_edit(entity_manager, entity, enabled_before, "Script.Enabled", "Toggle Script Behaviour", context);

        if (!instance.runtime_only) {
            const ScriptComponent asset_before = component;
            InspectorWidgets::begin_property_interaction("Script.Asset");
            const bool asset_changed = draw_script_asset(instance);
            InspectorWidgets::capture_property_interaction();
            track_edit(entity_manager, entity, asset_before, "Script.Asset", "Assign Script Asset", context, asset_changed);
        }

        if (instance.script_handle.is_valid()) {
            const ScriptInspectionResult& inspected = script_info(instance.script_handle).inspection;
            if (inspected.success) {
                const ScriptComponent sync_before = component;
                synchronize_fields(instance, inspected.public_fields);
                if (!(sync_before == component)) {
                    track_edit(entity_manager, entity, sync_before, "Script.Fields.Schema", "Synchronize Script Fields", context, true);
                }
            }
        }

        for (ScriptPublicField& field : instance.public_fields) {
            ImGui::PushID(field.name.c_str());
            const ScriptComponent before = component;
            bool changed = false;
            bool discrete = false;
            switch (field.type) {
                case ScriptFieldType::Boolean: {
                    bool value = std::get<bool>(field.value);
                    changed = InspectorWidgets::property_bool(field.name.c_str(), &value);
                    if (changed) field.value = value;
                    break;
                }
                case ScriptFieldType::Number: {
                    float value = static_cast<float>(std::get<double>(field.value));
                    changed = InspectorWidgets::property_float(field.name.c_str(), &value, 0.1f, 0.0f, 0.0f, "%.3f");
                    if (changed) field.value = static_cast<double>(value);
                    break;
                }
                case ScriptFieldType::String: {
                    std::array<char, 512> buffer{};
                    std::snprintf(buffer.data(), buffer.size(), "%s", std::get<std::string>(field.value).c_str());
                    changed = InspectorWidgets::property_text(field.name.c_str(), buffer.data(), buffer.size());
                    if (changed) field.value = std::string(buffer.data());
                    break;
                }
                case ScriptFieldType::Vector2: {
                    const ScriptVector2 current = std::get<ScriptVector2>(field.value);
                    float values[2]{current.x, current.y};
                    changed = InspectorWidgets::property_vec2(field.name.c_str(), values, 0.1f);
                    if (changed) field.value = ScriptVector2{values[0], values[1]};
                    break;
                }
                case ScriptFieldType::Entity: {
                    EntityID value = std::get<EntityID>(field.value);
                    changed = InspectorWidgets::property_entity_picker(field.name.c_str(), &value, entity_manager);
                    if (changed) field.value = value;
                    discrete = changed;
                    break;
                }
                case ScriptFieldType::Asset: {
                    AssetHandle value = std::get<AssetHandle>(field.value);
                    InspectorWidgets::begin_property_interaction(field.name.c_str());
                    changed = draw_asset_field(field.name.c_str(), value);
                    InspectorWidgets::capture_property_interaction();
                    if (changed) field.value = value;
                    discrete = changed;
                    break;
                }
            }
            const std::string property = "Script." + std::to_string(instance.id) + ".Field." + field.name;
            const std::string description = "Edit Script Field " + field.name;
            track_edit(entity_manager, entity, before, property.c_str(), description.c_str(), context, discrete);
            ImGui::PopID();
        }

        draw_diagnostics(instance.script_handle);
        if (context.script_runtime) {
            if (const std::string* reload_error = context.script_runtime->reload_error(instance.script_handle)) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "Hot reload kept previous version");
                ImGui::TextWrapped("%s", reload_error->c_str());
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Add Behaviour")) {
        const ScriptComponent before = component;
        component.instances.emplace_back();
        track_edit(entity_manager, entity, before, "Script.Add", "Add Script Behaviour", context, true);
    }
    if (context.script_runtime) {
        if (const ScriptExecutionStatus* status = context.script_runtime->status(entity);
            status && status->state == ScriptExecutionState::Error) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Runtime error");
            if (status->has_source_location()) {
                ImGui::TextDisabled(
                    "%s:%u:%u",
                    status->source_path.c_str(),
                    status->line,
                    status->column
                );
            }
            ImGui::TextWrapped("%s", status->message.c_str());
        }
    }
    ImGui::PopID();
}

} // namespace Engine
