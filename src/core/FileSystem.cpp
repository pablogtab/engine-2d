#include "FileSystem.hpp"
#include "Log.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Engine {

std::unordered_map<std::string, std::string> FileSystem::s_mount_points;
std::string FileSystem::s_assets_dir = "assets/";
std::string FileSystem::s_save_dir = "saves/";
std::string FileSystem::s_app_dir = "./";
bool FileSystem::s_initialized = false;

void FileSystem::init(const std::string& org_name, const std::string& app_name, bool use_local_save_dir) {
    if (s_initialized) return;

    // Base Application directory
    const char* base_path = SDL_GetBasePath();
    if (base_path) {
        s_app_dir = base_path;
    } else {
        s_app_dir = "./";
    }

    // Save directory
    if (use_local_save_dir) {
        s_save_dir = "saves/";
    } else {
        char* pref_path = SDL_GetPrefPath(org_name.c_str(), app_name.c_str());
        if (pref_path) {
            s_save_dir = pref_path;
            SDL_free(pref_path);
        } else {
            s_save_dir = "saves/";
        }
    }

    // Assets directory
    s_assets_dir = "assets/";

    // Ensure save directory exists physically
    create_directories(s_save_dir);

    // Register default virtual mount points
    mount("assets://", s_assets_dir);
    mount("save://", s_save_dir);
    mount("pref://", s_save_dir);

    s_initialized = true;
    ENGINE_LOG_INFO("[FILESYSTEM] Initialized. Assets: '{}' | Saves: '{}'", s_assets_dir, s_save_dir);
}

void FileSystem::mount(const std::string& virtual_prefix, const std::string& physical_path) {
    std::string norm_prefix = virtual_prefix;
    if (norm_prefix.find("://") == std::string::npos) {
        norm_prefix += "://";
    }

    std::string norm_path = physical_path;
    if (!norm_path.empty() && norm_path.back() != '/' && norm_path.back() != '\\') {
        norm_path += "/";
    }

    s_mount_points[norm_prefix] = norm_path;
    ENGINE_LOG_INFO("[FILESYSTEM] Mounted '{}' -> '{}'", norm_prefix, norm_path);
}

void FileSystem::unmount(const std::string& virtual_prefix) {
    std::string norm_prefix = virtual_prefix;
    if (norm_prefix.find("://") == std::string::npos) {
        norm_prefix += "://";
    }
    s_mount_points.erase(norm_prefix);
}

std::string FileSystem::resolve_path(const std::string& path) {
    for (const auto& [prefix, physical] : s_mount_points) {
        if (path.rfind(prefix, 0) == 0) { // starts_with prefix
            std::string sub = path.substr(prefix.length());
            return physical + sub;
        }
    }
    return path;
}

const std::string& FileSystem::get_assets_directory() {
    return s_assets_dir;
}

const std::string& FileSystem::get_save_directory() {
    return s_save_dir;
}

const std::string& FileSystem::get_app_directory() {
    return s_app_dir;
}

std::string FileSystem::read_text(const std::string& path) {
    std::string resolved = resolve_path(path);
    std::ifstream file(resolved, std::ios::in);
    if (!file.is_open()) {
        ENGINE_LOG_WARN("[FILESYSTEM] Failed to open text file for reading: '{}'", resolved);
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool FileSystem::write_text(const std::string& path, const std::string& content) {
    std::string resolved = resolve_path(path);

    // Ensure parent directory exists
    try {
        fs::path p(resolved);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[FILESYSTEM] Exception creating directories for '{}': {}", resolved, e.what());
    }

    std::ofstream file(resolved, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        ENGINE_LOG_ERROR("[FILESYSTEM] Failed to open text file for writing: '{}'", resolved);
        return false;
    }
    file << content;
    return file.good();
}

std::vector<uint8_t> FileSystem::read_bytes(const std::string& path) {
    std::string resolved = resolve_path(path);
    std::ifstream file(resolved, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        ENGINE_LOG_WARN("[FILESYSTEM] Failed to open binary file for reading: '{}'", resolved);
        return {};
    }

    file.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (size > 0) {
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    }
    return buffer;
}

bool FileSystem::write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    return write_bytes(path, bytes.data(), bytes.size());
}

bool FileSystem::write_bytes(const std::string& path, const uint8_t* data, size_t size) {
    std::string resolved = resolve_path(path);

    try {
        fs::path p(resolved);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[FILESYSTEM] Exception creating directories for '{}': {}", resolved, e.what());
    }

    std::ofstream file(resolved, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        ENGINE_LOG_ERROR("[FILESYSTEM] Failed to open binary file for writing: '{}'", resolved);
        return false;
    }

    if (size > 0 && data) {
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    return file.good();
}

bool FileSystem::exists(const std::string& path) {
    std::string resolved = resolve_path(path);
    std::error_code ec;
    return fs::exists(resolved, ec);
}

bool FileSystem::is_directory(const std::string& path) {
    std::string resolved = resolve_path(path);
    std::error_code ec;
    return fs::is_directory(resolved, ec);
}

size_t FileSystem::file_size(const std::string& path) {
    std::string resolved = resolve_path(path);
    std::error_code ec;
    auto size = fs::file_size(resolved, ec);
    return ec ? 0 : static_cast<size_t>(size);
}

bool FileSystem::remove(const std::string& path) {
    std::string resolved = resolve_path(path);
    std::error_code ec;
    return fs::remove(resolved, ec);
}

bool FileSystem::create_directories(const std::string& path) {
    std::string resolved = resolve_path(path);
    std::error_code ec;
    return fs::create_directories(resolved, ec);
}

std::vector<std::string> FileSystem::list_files(const std::string& dir_path, const std::string& extension, bool recursive) {
    std::string resolved = resolve_path(dir_path);
    std::vector<std::string> results;

    std::error_code ec;
    if (!fs::exists(resolved, ec) || !fs::is_directory(resolved, ec)) {
        return results;
    }

    auto match_ext = [&extension](const fs::path& p) {
        if (extension.empty()) return true;
        std::string p_ext = p.extension().string();
        std::string filter_ext = (extension.front() == '.') ? extension : ("." + extension);
        return p_ext == filter_ext;
    };

    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(resolved, ec)) {
            if (entry.is_regular_file() && match_ext(entry.path())) {
                results.push_back(entry.path().string());
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(resolved, ec)) {
            if (entry.is_regular_file() && match_ext(entry.path())) {
                results.push_back(entry.path().string());
            }
        }
    }

    return results;
}

} // namespace Engine
