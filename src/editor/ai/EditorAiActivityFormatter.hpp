#pragma once

#include "EditorAiProvider.hpp"

#include <string>

namespace Engine {

struct EditorAiActivityPresentation {
    std::string summary;
    std::string detail;
};

[[nodiscard]] EditorAiActivityPresentation format_editor_ai_activity(
    const EditorAiProviderEvent& event
);

} // namespace Engine
