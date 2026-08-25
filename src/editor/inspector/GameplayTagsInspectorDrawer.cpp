#include "GameplayTagsInspectorDrawer.hpp"

#include "../../ecs/components/CoreComponents.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "ComponentInspectorTransaction.hpp"
#include "InspectorWidgets.hpp"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <unordered_map>

namespace Engine {

void GameplayTagsInspectorDrawer::draw(EntityManager &entity_manager,
                                       EntityID entity,
                                       const EditorInspectorContext &context) {
  if (!entity_manager.has_component<GameplayTagsComponent>(entity))
    return;

  constexpr const char *COMPONENT_KEY = "gameplay_tags";
  const bool has_override = PrefabOverrides::is_component_overridden(
      entity_manager, entity, COMPONENT_KEY);
  const auto header = InspectorWidgets::registered_component_header(
      COMPONENT_KEY, true, true, has_override);
  if (header.revert_override_requested && context.command_history) {
    context.command_history->execute_command(
        std::make_unique<RevertComponentPrefabCommand>(
            &entity_manager, entity, COMPONENT_KEY, context.renderer));
    return;
  }
  if (header.apply_override_requested && context.command_history) {
    context.command_history->execute_command(
        std::make_unique<ApplyComponentPrefabCommand>(
            &entity_manager, entity, COMPONENT_KEY, context.renderer));
    return;
  }
  if (header.remove_requested && context.command_history) {
    context.command_history->execute_command(
        std::make_unique<SetComponentCommand<GameplayTagsComponent>>(
            &entity_manager, entity,
            entity_manager.get_component<GameplayTagsComponent>(entity),
            std::nullopt, "Remove Gameplay Tags"));
    return;
  }
  if (!header.open)
    return;

  auto &component = entity_manager.get_component<GameplayTagsComponent>(entity);
  std::optional<std::size_t> remove_index;
  for (std::size_t index = 0; index < component.tags.size(); ++index) {
    ImGui::PushID(static_cast<int>(index));
    ImGui::TextUnformatted(component.tags[index].c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("x"))
      remove_index = index;
    ImGui::PopID();
  }
  if (remove_index) {
    const GameplayTagsComponent before = component;
    component.tags.erase(component.tags.begin() +
                         static_cast<std::ptrdiff_t>(*remove_index));
    track_component_edit<GameplayTagsComponent>(
        entity_manager, entity, before, "GameplayTags.Remove",
        "Remove Gameplay Tag", {context.command_history, context.transactions},
        std::equal_to<GameplayTagsComponent>{}, {}, true);
  }

  static std::unordered_map<EntityID, std::array<char, 65>> inputs;
  auto &input = inputs[entity];
  InspectorWidgets::begin_property_row("Add Tag");
  ImGui::PushID("GameplayTagInput");
  constexpr float ADD_BUTTON_WIDTH = 42.0f;
  ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x -
                                              ADD_BUTTON_WIDTH -
                                              ImGui::GetStyle().ItemSpacing.x));
  const bool submitted = ImGui::InputText("##value", input.data(), input.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::PopID();
  const std::string candidate(input.data());
  const bool duplicate = component.contains(candidate);
  const bool can_add = component.tags.size() < 32 &&
                       is_valid_gameplay_tag(candidate) && !duplicate;
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_add);
  const bool add_clicked = ImGui::Button("Add", ImVec2(ADD_BUTTON_WIDTH, 0.0f));
  ImGui::EndDisabled();
  if ((submitted || add_clicked) && can_add) {
    const GameplayTagsComponent before = component;
    component.tags.push_back(candidate);
    std::sort(component.tags.begin(), component.tags.end());
    input.fill('\0');
    track_component_edit<GameplayTagsComponent>(
        entity_manager, entity, before, "GameplayTags.Add", "Add Gameplay Tag",
        {context.command_history, context.transactions},
        std::equal_to<GameplayTagsComponent>{}, {}, true);
  }
  if (!candidate.empty() && !can_add) {
    ImGui::TextDisabled(
        "%s", duplicate ? "Tag already assigned"
                        : "Use lowercase identifiers such as collectible.coin");
  }
}

} // namespace Engine
