#include "ecs/components/CoreComponents.hpp"
#include "editor/EditorSelection.hpp"
#include "editor/ai/EditorAiAttachmentStore.hpp"
#include "scene/Scene.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {

void test_entity_context_lifecycle() {
  const auto root =
      std::filesystem::temp_directory_path() / "engine2d_ai_attachment_tests";
  Engine::EditorAiAttachmentStore attachments(nullptr, root);
  auto scene = std::make_shared<Engine::Scene>("Attached scene");
  Engine::EditorSelection selection;
  const Engine::EntityID entity = scene->get_entity_manager().create_entity();
  scene->get_entity_manager().add_component<Engine::TagComponent>(
      entity, Engine::TagComponent{"Player"});
  selection.select(entity);

  const Engine::EditorAiAttachmentResult first =
      attachments.attach_selection(scene, &selection);
  assert(first.success);
  assert(attachments.entities().size() == 1);
  assert(attachments.entities().front().name == "Player");
  assert(!attachments.attach_selection(scene, &selection).success);

  const nlohmann::json request_context = {
      {"entity_index",
       nlohmann::json::array({{{"entity_id", entity}, {"name", "Player"}}})},
      {"asset_index", nlohmann::json::array()}};
  const nlohmann::json built =
      attachments.build_context(request_context, nullptr);
  assert(built.size() == 1);
  assert(built[0]["kind"] == "entity");
  assert(built[0]["data"]["entity_id"] == entity);

  attachments.prune_entities(std::make_shared<Engine::Scene>("Other scene"));
  assert(!attachments.has_contexts());
}

void test_rejects_invalid_screenshot_without_leaking_state() {
  Engine::EditorAiAttachmentStore attachments(
      nullptr,
      std::filesystem::temp_directory_path() / "engine2d_ai_attachment_tests");
  constexpr std::uint8_t invalid[] = {1, 2, 3, 4};
  const Engine::EditorAiAttachmentResult result =
      attachments.attach_screenshot_bytes(invalid, sizeof(invalid), "image/png",
                                          "invalid");
  assert(!result.success);
  assert(!attachments.has_screenshots());
}

} // namespace

int main() {
  test_entity_context_lifecycle();
  test_rejects_invalid_screenshot_without_leaking_state();
  std::cout << "Editor AI attachment store tests passed\n";
  return 0;
}
