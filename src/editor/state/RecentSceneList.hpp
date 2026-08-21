#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Engine {

class RecentSceneList {
public:
    static constexpr std::size_t MAX_ENTRIES = 10;

    [[nodiscard]] bool load(const std::string& filepath);
    [[nodiscard]] bool save(const std::string& filepath) const;

    void record(const std::string& scene_path);
    [[nodiscard]] bool remove_missing_files();
    void clear();

    [[nodiscard]] const std::vector<std::string>& paths() const { return m_paths; }
    [[nodiscard]] bool empty() const { return m_paths.empty(); }

private:
    [[nodiscard]] static std::string normalize_path(const std::string& path);

    std::vector<std::string> m_paths;
};

} // namespace Engine
