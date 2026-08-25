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
  input.project_overview = {
      {"name", "Crystal Cavern"},
      {"gameplay_roles",
       {{"player", nlohmann::json::array({{{"entity_id", 42}}})}}}};

  const std::string instructions =
      Engine::build_editor_ai_request_instructions(input);
  assert(contains(instructions, "confirm the proposal"));
  assert(contains(instructions, "Previous editor result: 1 entity added"));
  assert(contains(instructions, "\"entity_id\":42"));
  assert(contains(instructions, "Crystal Cavern"));
  assert(contains(instructions, "generated automatically"));
  assert(contains(instructions, "attached screenshot images"));
  assert(contains(instructions, "capture_viewport"));
  assert(contains(instructions, "Never invoke any Engine2D dynamic editor tool"));
  assert(contains(instructions, "directly and one at a time"));
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

void test_iterative_policy_requires_applied_state_inspection() {
  Engine::EditorAiRequestPolicyInput input;
  input.authoring_available = true;
  input.viewport_available = true;
  input.iterative_continuation = true;
  input.applied_batches = 2;
  input.work_time_ms = 120000;
  input.work_budget_ms = 600000;

  const std::string instructions =
      Engine::build_editor_ai_request_instructions(input);
  assert(contains(instructions, "iterative verification pass"));
  assert(contains(instructions, "at least one read-only editor tool"));
  assert(contains(instructions, "Applied batches: 2"));
  assert(contains(instructions, "Active AI work time: 120/600 seconds"));
}

void test_final_verification_policy_is_read_only() {
  Engine::EditorAiRequestPolicyInput input;
  input.iterative_continuation = true;
  input.final_verification_only = true;
  input.work_time_ms = 601000;
  input.work_budget_ms = 600000;

  const std::string instructions =
      Engine::build_editor_ai_request_instructions(input);
  assert(contains(instructions, "final read-only inspection pass"));
  assert(contains(instructions, "do not request additional authoring changes"));
}

void test_plan_mode_is_read_only_and_signals_explicit_build_authorization() {
  Engine::EditorAiRequestPolicyInput input;
  input.planning = true;
  input.has_existing_plan = true;
  input.existing_plan = "Inspect, implement, verify";

  const std::string instructions =
      Engine::build_editor_ai_request_instructions(input);
  assert(contains(instructions, "Work only in Plan mode"));
  assert(contains(instructions, "do not modify files"));
  assert(contains(instructions, "begin_plan_implementation"));
  assert(contains(instructions, "clearly authorizes"));
  assert(contains(instructions, "Inspect, implement, verify"));
  assert(contains(instructions, "plan-transition tools"));
}

} // namespace

int main() {
  test_read_only_policy();
  test_confirmed_authoring_policy_includes_context();
  test_image_generation_uses_bounded_policy_and_suppresses_viewport_capture();
  test_iterative_policy_requires_applied_state_inspection();
  test_final_verification_policy_is_read_only();
  test_plan_mode_is_read_only_and_signals_explicit_build_authorization();
  std::cout << "Editor AI request policy tests passed\n";
  return 0;
}
