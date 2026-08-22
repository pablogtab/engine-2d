#include "RenderEffectInspectorDrawer.hpp"
#include "AssetDropTarget.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../renderer/RenderLayerEffectProfile.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include <imgui.h>

namespace Engine {

void RenderEffectInspectorDrawer::draw(
    EntityManager& em,
    EntityID entity,
    EditorCommandHistory* command_history
) {
    if (!em.has_component<RenderEffectComponent>(entity)) return;

    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, "render_effect");
    const auto header = InspectorWidgets::registered_component_header(
        "render_effect", true, true, has_override
    );
    if (header.revert_override_requested && command_history) {
        command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, "render_effect", nullptr
        ));
        return;
    }
    if (header.apply_override_requested && command_history) {
        command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, "render_effect", nullptr
        ));
        return;
    }
    if (header.remove_requested && command_history) {
        command_history->execute_command(std::make_unique<SetComponentCommand<RenderEffectComponent>>(
            &em,
            entity,
            em.get_component<RenderEffectComponent>(entity),
            std::nullopt,
            "Remove Render Effect"
        ));
        return;
    }
    if (!header.open) return;

    ImGui::PushID("RenderEffectComponentDrawer");
    const RenderEffectComponent current = em.get_component<RenderEffectComponent>(entity);
    AssetDatabase& database = AssetDatabase::get();
    const AssetRecord* selected = current.profile_handle.is_valid()
        ? database.find(current.profile_handle) : nullptr;

    InspectorWidgets::begin_property_row("Profile");
    const char* preview = selected ? selected->source_path.c_str() : "None";
    if (ImGui::BeginCombo("##render_effect_profile", preview)) {
        if (ImGui::Selectable("None", !current.profile_handle.is_valid()) && command_history) {
            auto next = current;
            next.profile_handle = {};
            command_history->execute_command(std::make_unique<SetComponentCommand<RenderEffectComponent>>(
                &em, entity, current, next, "Clear Render Effect Profile"
            ));
        }
        for (const AssetRecord& record : database.records()) {
            if (record.type != AssetType::RenderLayerEffectProfile || record.status != AssetStatus::Ready ||
                !RenderLayerEffectProfile::load_asset(record.handle, database)) {
                continue;
            }
            const bool is_selected = record.handle == current.profile_handle;
            if (ImGui::Selectable(record.source_path.c_str(), is_selected) && command_history) {
                auto next = current;
                next.profile_handle = record.handle;
                command_history->execute_command(std::make_unique<SetComponentCommand<RenderEffectComponent>>(
                    &em, entity, current, next, "Assign Render Effect Profile"
                ));
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (const auto dropped = accept_asset_drop_target(
            database, AssetType::RenderLayerEffectProfile
        ); dropped && *dropped != current.profile_handle && command_history &&
        RenderLayerEffectProfile::load_asset(*dropped, database)) {
        auto next = current;
        next.profile_handle = *dropped;
        command_history->execute_command(std::make_unique<SetComponentCommand<RenderEffectComponent>>(
            &em, entity, current, next, "Assign Render Effect Profile"
        ));
    }

    bool enabled = current.enabled;
    InspectorWidgets::begin_property_row("Enabled");
    if (ImGui::Checkbox("##render_effect_enabled", &enabled) && command_history) {
        auto next = current;
        next.enabled = enabled;
        command_history->execute_command(std::make_unique<SetComponentCommand<RenderEffectComponent>>(
            &em, entity, current, next, "Toggle Render Effect"
        ));
    }

    if (current.profile_handle.is_valid() &&
        (!selected || selected->status != AssetStatus::Ready ||
         selected->type != AssetType::RenderLayerEffectProfile)) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
            "The assigned .layereffect asset is missing or unavailable."
        );
    }
    ImGui::TextWrapped(
        "The entity stores only this assignment. Double-click the .layereffect asset in Asset Browser to edit its recipe."
    );
    ImGui::PopID();
}

} // namespace Engine
