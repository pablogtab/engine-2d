#include "FileSystem.hpp"
#include "Log.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <chrono>
#include <limits>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Engine {

std::unordered_map<std::string, std::string> FileSystem::s_mount_points;
std::string FileSystem::s_assets_dir = "Assets/";
std::string FileSystem::s_save_dir = "saves/";
std::string FileSystem::s_app_dir = "./";
std::string FileSystem::s_project_dir = "./";
bool FileSystem::s_initialized = false;

void FileSystem::init(const std::string& org_name, const std::string& app_name, bool use_local_save_dir,
                      const fs::path& project_root) {
    s_mount_points.clear();
    s_project_dir = fs::absolute(project_root).lexically_normal().string();

    // Base Application directory
    const char* base_path = SDL_GetBasePath();
    if (base_path) {
        s_app_dir = base_path;
    } else {
        s_app_dir = "./";
    }

    // Save directory
    if (use_local_save_dir) {
        s_save_dir = (fs::absolute(project_root) / "Saves").lexically_normal().string();
    } else {
        char* pref_path = SDL_GetPrefPath(org_name.c_str(), app_name.c_str());
        if (pref_path) {
            s_save_dir = pref_path;
            SDL_free(pref_path);
        } else {
            s_save_dir = (fs::absolute(project_root) / "Saves").lexically_normal().string();
        }
    }

    // Assets directory
    s_assets_dir = (fs::absolute(project_root) / "Assets").lexically_normal().string();

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
    const fs::path filesystem_path(path);
    if (filesystem_path.is_absolute()) return filesystem_path.lexically_normal().string();
    return (fs::path(s_project_dir) / filesystem_path).lexically_normal().string();
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

const std::string& FileSystem::get_project_directory() {
    return s_project_dir;
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
    file.flush();
    file.close();
    return !file.fail();
}

bool FileSystem::write_text_atomically(const std::string& path, const std::string& content) {
    return write_bytes_atomically(path, reinterpret_cast<const uint8_t*>(content.data()), content.size());
}

bool FileSystem::write_bytes_atomically(const std::string& path, const uint8_t* data, size_t size) {
    if ((size != 0 && !data) || size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) return false;
    const fs::path destination = resolve_path(path);
    std::error_code error;
    if (destination.has_parent_path()) fs::create_directories(destination.parent_path(), error);
    if (error) return false;

    // Reserving a unique sibling directory prevents concurrent writers sharing a temporary file.
    static std::atomic<uint64_t> sequence{0};
    fs::path staging;
    bool reserved = false;
    for (int attempt = 0; attempt < 32 && !reserved; ++attempt) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        staging = destination.string() + ".write-" + std::to_string(nonce) + "-" + std::to_string(sequence++);
        reserved = fs::create_directory(staging, error);
        if (error && error != std::errc::file_exists) return false;
        error.clear();
    }
    if (!reserved) return false;
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{staging};
    const fs::path temporary = staging / "content";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    if (size) file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    file.flush();
    file.close();
    if (file.fail()) {
        ENGINE_LOG_ERROR("[FILESYSTEM] Could not finish atomic write to '{}'", destination.string());
        return false;
    }
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ENGINE_LOG_ERROR("[FILESYSTEM] Atomic replacement failed for '{}'", destination.string());
        return false;
    }
#else
    fs::rename(temporary, destination, error);
    if (error) {
        ENGINE_LOG_ERROR("[FILESYSTEM] Atomic replacement failed for '{}': {}", destination.string(), error.message());
        return false;
    }
#endif
    return true;
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
    if ((size > 0 && !data) || size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) return false;
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

    if (size > 0) {
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    file.flush();
    file.close();
    return !file.fail();
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
