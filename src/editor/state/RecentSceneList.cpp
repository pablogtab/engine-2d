#include "RecentSceneList.hpp"

#include "../../core/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Engine {
namespace {

constexpr const char* RECENT_SCENES_KEY = "recent_scenes";

} // namespace

bool RecentSceneList::load(const std::string& filepath) {
    m_paths.clear();

    std::ifstream file(filepath);
    if (!file.is_open()) return true;

    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        ENGINE_LOG_WARN("[EDITOR] Ignoring invalid recent-scenes file '{}'", filepath);
        return false;
    }

    const auto entries = root.value(RECENT_SCENES_KEY, nlohmann::json::array());
    if (!entries.is_array()) {
        ENGINE_LOG_WARN("[EDITOR] Ignoring invalid recent-scenes entries in '{}'", filepath);
        return false;
    }

    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->is_string()) record(it->get<std::string>());
    }
    return true;
}

bool RecentSceneList::save(const std::string& filepath) const {
    const std::filesystem::path path(filepath);
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            ENGINE_LOG_ERROR("[EDITOR] Failed to create recent-scenes directory '{}': {}", path.parent_path().string(), error.message());
            return false;
        }
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        ENGINE_LOG_ERROR("[EDITOR] Failed to save recent scenes to '{}'", filepath);
        return false;
    }
    file << nlohmann::json{{RECENT_SCENES_KEY, m_paths}}.dump(2) << '\n';
    return static_cast<bool>(file);
}

void RecentSceneList::record(const std::string& scene_path) {
    const std::string normalized = normalize_path(scene_path);
    if (normalized.empty()) return;

    std::erase(m_paths, normalized);
    m_paths.insert(m_paths.begin(), normalized);
    if (m_paths.size() > MAX_ENTRIES) m_paths.resize(MAX_ENTRIES);
}

bool RecentSceneList::remove_missing_files() {
    const auto previous_size = m_paths.size();
    std::erase_if(m_paths, [](const std::string& path) {
        std::error_code error;
        return !std::filesystem::is_regular_file(path, error);
    });
    return m_paths.size() != previous_size;
}

void RecentSceneList::clear() {
    m_paths.clear();
}

std::string RecentSceneList::normalize_path(const std::string& path) {
    if (path.empty()) return {};
    return std::filesystem::path(path).lexically_normal().generic_string();
}

} // namespace Engine
