#include "editor/ai/EditorAiRequestPolicy.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

bool contains(const std::string &text, const std::string &fragment) {
  return text.find(fragment) != std::string::npos;
}

void test_read_only_policy() {
  const std::string instructions =
      Engine::build_editor_ai_request_instructions({});
  assert(contains(instructions, "authoring tools are unavailable"));
  assert(!contains(instructions, "confirm the proposal"));
}

void test_confirmed_authoring_policy_includes_context() {
  Engine::EditorAiRequestPolicyInput input;
  input.authoring_available = true;
  input.confirm_before_apply = true;
  input.viewport_available = true;
  input.has_images = true;
  input.previous_receipt = "1 entity added";
  input.attached_contexts = nlohmann::json::array(
      {{{"kind", "entity"}, {"data", {{"entity_id", 42}}}}});

  const std::string instructions =
      Engine::build_editor_ai_request_instructions(input);
  assert(contains(instructions, "confirm the proposal"));
  assert(contains(instructions, "Previous editor result: 1 entity added"));
  assert(contains(instructions, "\"entity_id\":42"));
  assert(contains(instructions, "attached screenshot images"));
  assert(contains(instructions, "capture_viewport"));
}

void test_image_generation_uses_bounded_policy_and_suppresses_viewport_capture() {
  Engine::EditorAiRequestPolicyInput input;
  input.image_generation_requested = true;
  input.authoring_available = true;
  input.confirm_before_apply = false;
  input.viewport_available = true;

  const std::string instructions =
      Engine::build_editor_ai_request_instructions(input);
  assert(contains(instructions, "one bounded texture workflow"));
  assert(contains(instructions, "propose_import_generated_texture"));
  assert(contains(instructions, "Autonomous safe mode"));
  assert(!contains(instructions, "capture_viewport"));
}

} // namespace

int main() {
  test_read_only_policy();
  test_confirmed_authoring_policy_includes_context();
  test_image_generation_uses_bounded_policy_and_suppresses_viewport_capture();
  std::cout << "Editor AI request policy tests passed\n";
  return 0;
}
