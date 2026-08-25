#include "ParticleEmitterInspectorDrawer.hpp"
#include "AssetDropTarget.hpp"
#include "InspectorWidgets.hpp"
#include "ComponentInspectorTransaction.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/ParticleComponents.hpp"
#include "../../particles/ParticleEffectAsset.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include "../../scene/SceneManager.hpp"
#include <imgui.h>

namespace Engine {

void ParticleEmitterInspectorDrawer::draw(
    EntityManager& em,
    EntityID entity,
    const EditorInspectorContext& context
) {
    if (!em.has_component<ParticleEmitter2DComponent>(entity)) return;

    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, "particle_emitter_2d");
    const auto header = InspectorWidgets::registered_component_header(
        "particle_emitter_2d", true, true, has_override
    );
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, "particle_emitter_2d", nullptr
        ));
        return;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, "particle_emitter_2d", nullptr
        ));
        return;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
            &em,
            entity,
            em.get_component<ParticleEmitter2DComponent>(entity),
            std::nullopt,
            "Remove Particle Emitter"
        ));
        return;
    }
    if (!header.open) return;

    ImGui::PushID("ParticleEmitterComponentDrawer");
    const ParticleEmitter2DComponent current = em.get_component<ParticleEmitter2DComponent>(entity);
    AssetDatabase& database = AssetDatabase::get();
    const AssetRecord* selected = current.effect_handle.is_valid()
        ? database.find(current.effect_handle) : nullptr;

    InspectorWidgets::begin_property_row("Particle Effect");
    const char* preview = selected ? selected->source_path.c_str() : "None";
    if (ImGui::BeginCombo("##particle_effect_asset", preview)) {
        if (ImGui::Selectable("None", !current.effect_handle.is_valid()) && context.command_history) {
            auto next = current;
            next.effect_handle = {};
            context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
                &em, entity, current, next, "Clear Particle Effect"
            ));
        }
        for (const AssetRecord& record : database.records()) {
            if (record.type != AssetType::ParticleEffect || record.status != AssetStatus::Ready) {
                continue;
            }
            const bool is_selected = record.handle == current.effect_handle;
            if (ImGui::Selectable(record.source_path.c_str(), is_selected) && context.command_history) {
                auto next = current;
                next.effect_handle = record.handle;
                context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
                    &em, entity, current, next, "Assign Particle Effect"
                ));
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (const auto dropped = accept_asset_drop_target(
            database, AssetType::ParticleEffect
        ); dropped && *dropped != current.effect_handle && context.command_history) {
        auto next = current;
        next.effect_handle = *dropped;
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
            &em, entity, current, next, "Assign Particle Effect"
        ));
    }

    bool active = current.active;
    InspectorWidgets::begin_property_row("Active");
    if (ImGui::Checkbox("##particle_active", &active) && context.command_history) {
        auto next = current;
        next.active = active;
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
            &em, entity, current, next, "Toggle Particle Active"
        ));
    }

    bool play_on_awake = current.play_on_awake;
    InspectorWidgets::begin_property_row("Play On Awake");
    if (ImGui::Checkbox("##particle_play_on_awake", &play_on_awake) && context.command_history) {
        auto next = current;
        next.play_on_awake = play_on_awake;
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
            &em, entity, current, next, "Toggle Play On Awake"
        ));
    }

    bool local_space = current.local_space;
    InspectorWidgets::begin_property_row("Local Space");
    if (ImGui::Checkbox("##particle_local_space", &local_space) && context.command_history) {
        auto next = current;
        next.local_space = local_space;
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
            &em, entity, current, next, "Toggle Local Space"
        ));
    }

    bool override_rate = current.emit_rate_override.has_value();
    InspectorWidgets::begin_property_row("Override Rate");
    if (ImGui::Checkbox("##override_emit_rate", &override_rate) && context.command_history) {
        auto next = current;
        next.emit_rate_override = override_rate ? std::optional<float>{30.0f} : std::nullopt;
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
            &em, entity, current, next, "Toggle Particle Rate Override"
        ));
    }
    if (override_rate) {
        auto& emitter = em.get_component<ParticleEmitter2DComponent>(entity);
        const ParticleEmitter2DComponent before = emitter;
        float rate_override = emitter.emit_rate_override.value_or(30.0f);
        InspectorWidgets::begin_property_row("Rate");
        if (ImGui::DragFloat("##emit_rate_override", &rate_override, 0.5f, 0.0f, 500.0f, "%.1f /s")) {
            emitter.emit_rate_override = rate_override;
        }
        track_component_edit<ParticleEmitter2DComponent>(
            em, entity, before, "ParticleEmitter.RateOverride", "Modify Particle Rate Override",
            {context.command_history, context.transactions}, std::equal_to<ParticleEmitter2DComponent>{}
        );
    }

    const auto snapshot = em.get_component<ParticleEmitter2DComponent>(entity);
    bool override_burst = snapshot.burst_count_override.has_value();
    InspectorWidgets::begin_property_row("Override Burst");
    if (ImGui::Checkbox("##override_burst_count", &override_burst) && context.command_history) {
        auto next = snapshot;
        next.burst_count_override = override_burst ? std::optional<int>{10} : std::nullopt;
        context.command_history->execute_command(std::make_unique<SetComponentCommand<ParticleEmitter2DComponent>>(
            &em, entity, snapshot, next, "Toggle Particle Burst Override"
        ));
    }
    if (override_burst) {
        auto& emitter = em.get_component<ParticleEmitter2DComponent>(entity);
        const ParticleEmitter2DComponent before = emitter;
        int burst_override = emitter.burst_count_override.value_or(10);
        InspectorWidgets::begin_property_row("Burst Count");
        if (ImGui::DragInt("##burst_override", &burst_override, 1.0f, 0, 500)) {
            emitter.burst_count_override = burst_override;
        }
        track_component_edit<ParticleEmitter2DComponent>(
            em, entity, before, "ParticleEmitter.BurstOverride", "Modify Particle Burst Override",
            {context.command_history, context.transactions}, std::equal_to<ParticleEmitter2DComponent>{}
        );
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool can_preview = context.scene_manager && context.scene_manager->is_playing();
    if (!can_preview) ImGui::BeginDisabled();
    const bool playing = em.get_component<ParticleEmitter2DComponent>(entity).is_playing;
    if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(80, 0)) && context.scene_manager) {
        if (playing) {
            static_cast<void>(context.scene_manager->stop_particle_emitter(entity, false));
        } else {
            static_cast<void>(context.scene_manager->play_particle_emitter(entity));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Burst Now", ImVec2(90, 0)) && context.scene_manager) {
        static_cast<void>(context.scene_manager->burst_particle_emitter(entity));
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop & Clear", ImVec2(105, 0)) && context.scene_manager) {
        static_cast<void>(context.scene_manager->stop_particle_emitter(entity, true));
    }
    if (!can_preview) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("Runtime controls are available in Play Mode.");
    }

    ImGui::PopID();
}

} // namespace Engine
