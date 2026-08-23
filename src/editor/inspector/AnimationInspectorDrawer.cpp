#include "AnimationInspectorDrawer.hpp"

#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../panels/SpriteAnimationStudioPanel.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../../scene/PrefabOverrides.hpp"

#include <imgui.h>

#include <memory>
#include <string>

namespace Engine {

namespace {

void reset_runtime(EntityManager& em, EntityID entity) {
    SpriteAnimationSystem::reset_playback(em, entity);
}

} // namespace

void AnimationInspectorDrawer::draw(EntityManager& em, EntityID entity, const Context& context) {
    if (!em.has_component<AnimationComponent>(entity)) return;

    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, "animation");
    const auto header = InspectorWidgets::registered_component_header(
        "animation", true, true, has_override
    );
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, "animation", nullptr
        ));
        return;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, "animation", nullptr
        ));
        return;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(
            std::make_unique<SetComponentCommand<AnimationComponent>>(
                &em,
                entity,
                em.get_component<AnimationComponent>(entity),
                std::nullopt,
                "Remove Sprite Animator 2D",
                reset_runtime
            )
        );
        return;
    }
    if (!header.open) return;

    const AnimationComponent& animation = em.get_component<AnimationComponent>(entity);
    const AnimationClip* default_clip = animation.get_clip(animation.default_clip_index);
    ImGui::TextDisabled(
        "%zu clip%s | Grid %d x %d",
        animation.clips.size(),
        animation.clips.size() == 1 ? "" : "s",
        animation.columns,
        animation.rows
    );
    ImGui::TextDisabled(
        "Default: %.20s | Startup: %s",
        default_clip ? default_clip->name.c_str() : "None",
        animation.play_on_start ? "On" : "Off"
    );

    const std::string studio_label = editor_icon_label(
        EditorIcon::ExternalLink, "Open Animation Studio"
    );
    ImGui::BeginDisabled(context.studio == nullptr);
    if (ImGui::Button(studio_label.c_str(), ImVec2(-1.0f, 0.0f)) && context.studio) {
        context.studio->open_for_entity(entity);
    }
    ImGui::EndDisabled();
    if (!context.studio) ImGui::TextDisabled("Animation Studio is unavailable.");
}

} // namespace Engine
