#include "SceneSaveAsTarget.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Engine {

SceneSaveAsTarget resolve_scene_save_as_target(
    const std::filesystem::path& scenes_directory,
    const std::string& requested_name
) {
    SceneSaveAsTarget target;
    const bool blank = requested_name.empty() || std::all_of(
        requested_name.begin(), requested_name.end(),
        [](unsigned char character) { return std::isspace(character) != 0; }
    );
    if (blank) {
        target.issue = SceneSaveAsIssue::EmptyName;
        return target;
    }
    const bool contains_invalid_character = std::any_of(
        requested_name.begin(), requested_name.end(), [](unsigned char character) {
            constexpr const char* INVALID_FILENAME_CHARACTERS = "<>:\"/\\|?*";
            return character < 32 ||
                std::string_view(INVALID_FILENAME_CHARACTERS).find(character) !=
                    std::string_view::npos;
        }
    );
    if (requested_name == "." || requested_name == ".." || contains_invalid_character) {
        target.issue = SceneSaveAsIssue::InvalidName;
        return target;
    }

    target.filename = requested_name;
    if (std::filesystem::path(target.filename).extension() != ".scene") {
        target.filename += ".scene";
    }
    target.path = scenes_directory / target.filename;
    target.issue = std::filesystem::exists(target.path)
        ? SceneSaveAsIssue::DestinationExists
        : SceneSaveAsIssue::None;
    return target;
}

} // namespace Engine
