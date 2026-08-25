#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace Engine {

struct EditorAiRequestPolicyInput {
  bool image_generation_requested{false};
  bool authoring_available{false};
  bool confirm_before_apply{true};
  bool viewport_available{false};
  bool has_images{false};
  std::string previous_receipt;
  nlohmann::json attached_contexts;
};

[[nodiscard]] std::string
build_editor_ai_request_instructions(const EditorAiRequestPolicyInput &input);

} // namespace Engine
