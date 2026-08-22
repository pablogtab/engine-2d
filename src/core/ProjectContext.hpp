#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Engine {

struct ProjectDescriptor {
    int version{1};
    std::string name;
    std::filesystem::path startup_scene;
};

class ProjectContext {
public:
    static std::optional<ProjectContext> open(
        const std::filesystem::path& project_path,
        std::string* error = nullptr
    );

    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }
    [[nodiscard]] const ProjectDescriptor& descriptor() const { return m_descriptor; }
    [[nodiscard]] std::filesystem::path descriptor_path() const { return m_root / "project.engine2d"; }
    [[nodiscard]] std::filesystem::path assets_directory() const { return m_root / "Assets"; }
    [[nodiscard]] std::filesystem::path settings_directory() const { return m_root / "ProjectSettings"; }
    [[nodiscard]] std::filesystem::path library_directory() const { return m_root / "Library"; }
    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& project_relative_path) const;

private:
    ProjectContext(std::filesystem::path root, ProjectDescriptor descriptor)
        : m_root(std::move(root)), m_descriptor(std::move(descriptor)) {}

    std::filesystem::path m_root;
    ProjectDescriptor m_descriptor;
};

} // namespace Engine
