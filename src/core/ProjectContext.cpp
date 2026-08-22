#include "ProjectContext.hpp"

#include "FileSystem.hpp"
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace Engine {
namespace {

void set_error(std::string* destination, std::string message) {
    if (destination) *destination = std::move(message);
}

bool is_project_relative(const fs::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    const fs::path normalized = path.lexically_normal();
    const std::string generic = normalized.generic_string();
    return generic != ".." && !generic.starts_with("../");
}

} // namespace

std::optional<ProjectContext> ProjectContext::open(const fs::path& project_path, std::string* error) {
    std::error_code filesystem_error;
    fs::path root = fs::absolute(project_path, filesystem_error).lexically_normal();
    if (filesystem_error || !fs::is_directory(root, filesystem_error)) {
        set_error(error, "Project directory does not exist: " + project_path.string());
        return std::nullopt;
    }

    const fs::path descriptor_path = root / "project.engine2d";
    const std::string text = FileSystem::read_text(descriptor_path.string());
    if (text.empty()) {
        set_error(error, "Missing project descriptor: " + descriptor_path.string());
        return std::nullopt;
    }

    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        set_error(error, "Invalid project descriptor JSON: " + descriptor_path.string());
        return std::nullopt;
    }

    ProjectDescriptor descriptor;
    descriptor.version = json.value("version", 0);
    descriptor.name = json.value("name", std::string{});
    descriptor.startup_scene = json.value("startup_scene", std::string{});
    if (descriptor.version != 1) {
        set_error(error, "Unsupported project descriptor version: " + std::to_string(descriptor.version));
        return std::nullopt;
    }
    if (descriptor.name.empty()) {
        set_error(error, "Project descriptor requires a non-empty name");
        return std::nullopt;
    }
    if (!is_project_relative(descriptor.startup_scene) ||
        descriptor.startup_scene.lexically_normal().generic_string().rfind("Assets/", 0) != 0) {
        set_error(error, "startup_scene must be a project-relative path below Assets/");
        return std::nullopt;
    }
    if (!fs::is_regular_file(root / descriptor.startup_scene, filesystem_error)) {
        set_error(error, "Startup scene does not exist: " + (root / descriptor.startup_scene).string());
        return std::nullopt;
    }
    if (!fs::is_directory(root / "Assets", filesystem_error)) {
        set_error(error, "Project is missing its Assets directory: " + (root / "Assets").string());
        return std::nullopt;
    }
    fs::create_directories(root / "ProjectSettings", filesystem_error);
    if (filesystem_error) {
        set_error(error, "Cannot create ProjectSettings: " + filesystem_error.message());
        return std::nullopt;
    }
    fs::create_directories(root / "Library", filesystem_error);
    if (filesystem_error) {
        set_error(error, "Cannot create Library: " + filesystem_error.message());
        return std::nullopt;
    }
    return ProjectContext(std::move(root), std::move(descriptor));
}

fs::path ProjectContext::resolve(const fs::path& project_relative_path) const {
    if (project_relative_path.is_absolute()) return project_relative_path.lexically_normal();
    return (m_root / project_relative_path).lexically_normal();
}

} // namespace Engine
