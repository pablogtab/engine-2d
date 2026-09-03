#pragma once

#include <filesystem>
#include <string>

namespace Engine {

enum class SceneSaveAsIssue {
    None,
    EmptyName,
    InvalidName,
    DestinationExists
};

struct SceneSaveAsTarget {
    SceneSaveAsIssue issue{SceneSaveAsIssue::EmptyName};
    std::string filename;
    std::filesystem::path path;

    [[nodiscard]] bool can_save() const { return issue == SceneSaveAsIssue::None; }
};

[[nodiscard]] SceneSaveAsTarget resolve_scene_save_as_target(
    const std::filesystem::path& scenes_directory,
    const std::string& requested_name
);

} // namespace Engine
