#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Engine {

struct RecentProject {
    std::string name;
    std::filesystem::path path;
};

class ProjectRegistry {
public:
    static constexpr std::size_t MAX_ENTRIES = 12;

    [[nodiscard]] bool load(const std::filesystem::path& filepath, std::string* error = nullptr);
    [[nodiscard]] bool save(const std::filesystem::path& filepath, std::string* error = nullptr) const;
    void record(const RecentProject& project);
    void remove(const std::filesystem::path& project_path);
    [[nodiscard]] bool remove_missing_projects();
    void clear();

    [[nodiscard]] const std::vector<RecentProject>& projects() const { return m_projects; }

private:
    [[nodiscard]] static std::filesystem::path normalize_path(
        const std::filesystem::path& path
    );

    std::vector<RecentProject> m_projects;
};

class ProjectWorkspace {
public:
    static std::optional<std::filesystem::path> create(
        const std::filesystem::path& parent_directory,
        const std::string& project_name,
        std::string* error = nullptr
    );

    [[nodiscard]] static bool is_valid_name(const std::string& name);
};

} // namespace Engine
