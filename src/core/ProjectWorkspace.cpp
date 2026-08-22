#include "ProjectWorkspace.hpp"

#include "FileSystem.hpp"
#include "scene/ComponentRegistry.hpp"
#include "scene/SceneSchema.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace Engine {
namespace {

void set_error(std::string* destination, std::string message) {
    if (destination) *destination = std::move(message);
}

bool write_json(const fs::path& path, const nlohmann::json& value, std::string* error) {
    std::error_code filesystem_error;
    fs::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error) {
        set_error(error, "Cannot create directory '" + path.parent_path().string() +
            "': " + filesystem_error.message());
        return false;
    }
    std::ofstream file(path);
    if (!file.is_open()) {
        set_error(error, "Cannot write file: " + path.string());
        return false;
    }
    file << value.dump(2) << '\n';
    if (!file) {
        set_error(error, "Cannot finish writing file: " + path.string());
        return false;
    }
    return true;
}

std::string trim_copy(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

} // namespace

bool ProjectRegistry::load(const fs::path& filepath, std::string* error) {
    m_projects.clear();
    std::ifstream file(filepath);
    if (!file.is_open()) return true;

    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("projects") ||
        !root["projects"].is_array()) {
        set_error(error, "Invalid recent-projects registry: " + filepath.string());
        return false;
    }

    const auto& entries = root["projects"];
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (!it->is_object()) continue;
        const std::string name = it->value("name", std::string{});
        const std::string path = it->value("path", std::string{});
        if (!name.empty() && !path.empty()) record({name, path});
    }
    return true;
}

bool ProjectRegistry::save(const fs::path& filepath, std::string* error) const {
    nlohmann::json entries = nlohmann::json::array();
    for (const RecentProject& project : m_projects) {
        entries.push_back({
            {"name", project.name},
            {"path", project.path.generic_string()}
        });
    }
    return FileSystem::write_text_atomically(
        filepath.string(),
        nlohmann::json{{"version", 1}, {"projects", std::move(entries)}}.dump(2)
    ) || (set_error(error, "Cannot save recent-projects registry: " + filepath.string()), false);
}

void ProjectRegistry::record(const RecentProject& project) {
    const fs::path normalized = normalize_path(project.path);
    if (normalized.empty() || project.name.empty()) return;
    std::erase_if(m_projects, [&](const RecentProject& existing) {
        return normalize_path(existing.path) == normalized;
    });
    m_projects.insert(m_projects.begin(), {project.name, normalized});
    if (m_projects.size() > MAX_ENTRIES) m_projects.resize(MAX_ENTRIES);
}

void ProjectRegistry::remove(const fs::path& project_path) {
    const fs::path normalized = normalize_path(project_path);
    std::erase_if(m_projects, [&](const RecentProject& project) {
        return normalize_path(project.path) == normalized;
    });
}

bool ProjectRegistry::remove_missing_projects() {
    const std::size_t previous_size = m_projects.size();
    std::erase_if(m_projects, [](const RecentProject& project) {
        std::error_code error;
        return !fs::is_regular_file(project.path / "project.engine2d", error);
    });
    return previous_size != m_projects.size();
}

void ProjectRegistry::clear() {
    m_projects.clear();
}

fs::path ProjectRegistry::normalize_path(const fs::path& path) {
    if (path.empty()) return {};
    std::error_code error;
    fs::path normalized = fs::weakly_canonical(path, error);
    if (!error) return normalized;
    error.clear();
    normalized = fs::absolute(path, error).lexically_normal();
    return error ? path.lexically_normal() : normalized;
}

bool ProjectWorkspace::is_valid_name(const std::string& name) {
    const std::string trimmed = trim_copy(name);
    if (trimmed.empty() || trimmed == "." || trimmed == "..") return false;
    return std::none_of(trimmed.begin(), trimmed.end(), [](unsigned char character) {
        return character < 32 || character == '/' || character == '\\' || character == ':';
    });
}

std::optional<fs::path> ProjectWorkspace::create(
    const fs::path& parent_directory,
    const std::string& project_name,
    std::string* error
) {
    const std::string name = trim_copy(project_name);
    if (!is_valid_name(name)) {
        set_error(error, "Project name cannot be empty or contain path separators");
        return std::nullopt;
    }

    std::error_code filesystem_error;
    const fs::path parent = fs::absolute(parent_directory, filesystem_error).lexically_normal();
    if (filesystem_error || !fs::is_directory(parent, filesystem_error)) {
        set_error(error, "Parent directory does not exist: " + parent_directory.string());
        return std::nullopt;
    }
    const fs::path target = parent / name;
    if (fs::exists(target, filesystem_error)) {
        set_error(error, "Project directory already exists: " + target.string());
        return std::nullopt;
    }

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path staging = parent / ("." + name + ".engine2d-create-" + std::to_string(nonce));
    fs::create_directories(staging, filesystem_error);
    if (filesystem_error) {
        set_error(error, "Cannot create project staging directory: " + filesystem_error.message());
        return std::nullopt;
    }
    const auto rollback = [&]() {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
    };

    constexpr const char* asset_directories[]{
        "Audio", "PostProcess", "Prefabs", "Scenes", "Scripts", "Shaders", "Textures"
    };
    for (const char* directory : asset_directories) {
        fs::create_directories(staging / "Assets" / directory, filesystem_error);
        if (filesystem_error) {
            set_error(error, "Cannot create project assets: " + filesystem_error.message());
            rollback();
            return std::nullopt;
        }
    }
    for (const char* directory : {"Library", "Saves"}) {
        fs::create_directories(staging / directory, filesystem_error);
        if (filesystem_error) {
            set_error(error, "Cannot create project data directories: " + filesystem_error.message());
            rollback();
            return std::nullopt;
        }
    }

    const nlohmann::json descriptor{
        {"version", 1},
        {"name", name},
        {"startup_scene", "Assets/Scenes/Main.scene"}
    };
    const nlohmann::json scene{
        {"version", SceneSchema::CURRENT_VERSION},
        {"component_versions", SceneSchema::component_versions(default_component_registry())},
        {"scene_name", "Main"},
        {"entities", nlohmann::json::array()}
    };
    const nlohmann::json settings{{"display", {
        {"reference_width", 1280}, {"reference_height", 720},
        {"default_window_width", 1280}, {"default_window_height", 720},
        {"default_window_mode", "windowed"}, {"default_refresh_rate", 0.0},
        {"default_vsync", true}, {"presentation", "fit"},
        {"orientation_policy", "landscape"}, {"render_scale_mode", "fixed"},
        {"fixed_render_scale", 1.0}, {"dynamic_min_render_scale", 0.5},
        {"dynamic_max_render_scale", 1.0}, {"dynamic_target_fps", 60.0}
    }}};

    if (!write_json(staging / "project.engine2d", descriptor, error) ||
        !write_json(staging / "Assets/Scenes/Main.scene", scene, error) ||
        !write_json(staging / "ProjectSettings/Project.json", settings, error) ||
        !write_json(staging / "ProjectSettings/AssetDatabase.json", {{"assets", nlohmann::json::array()}}, error)) {
        rollback();
        return std::nullopt;
    }

    {
        std::ofstream ignore(staging / ".gitignore");
        ignore << "Library/\nSaves/\n.DS_Store\n";
        if (!ignore) {
            set_error(error, "Cannot write project .gitignore");
            rollback();
            return std::nullopt;
        }
    }

    fs::rename(staging, target, filesystem_error);
    if (filesystem_error) {
        set_error(error, "Cannot finalize project: " + filesystem_error.message());
        rollback();
        return std::nullopt;
    }
    return target;
}

} // namespace Engine
