#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Engine {

struct EditorAiRequestPolicyInput {
  bool image_generation_requested{false};
  bool authoring_available{false};
  bool confirm_before_apply{true};
  bool viewport_available{false};
  bool has_images{false};
  bool iterative_continuation{false};
  bool final_verification_only{false};
  bool planning{false};
  bool has_existing_plan{false};
  std::size_t applied_batches{0};
  std::uint64_t work_time_ms{0};
  std::uint64_t work_budget_ms{0};
  std::string previous_receipt;
  nlohmann::json attached_contexts;
  nlohmann::json project_overview;
  std::string existing_plan;
};

[[nodiscard]] std::string
build_editor_ai_request_instructions(const EditorAiRequestPolicyInput &input);

} // namespace Engine
