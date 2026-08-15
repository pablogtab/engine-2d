#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Engine {

// ---------------------------------------------------------------------------
// FileSystem
// Multiplatform storage and virtual file system abstraction.
// Allows mounting virtual prefixes (e.g. "assets://", "save://", "pref://")
// to concrete OS filesystem paths, with unified text/binary I/O operations.
// ---------------------------------------------------------------------------
class FileSystem {
public:
    // Initializes default mount points ("assets://", "save://", "pref://").
    // If use_local_save_dir is true, "save://" points to "./saves/" (ideal for dev/portable builds).
    // If false, it uses OS-appropriate paths (SDL_GetPrefPath).
    static void init(const std::string& org_name = "Engine2D",
                     const std::string& app_name = "Engine2DApp",
                     bool use_local_save_dir = true);

    // Mounts a virtual prefix (e.g. "mods://") to a physical filesystem folder.
    static void mount(const std::string& virtual_prefix, const std::string& physical_path);
    static void unmount(const std::string& virtual_prefix);

    // Resolves a virtual path (e.g. "save://save1.sav") into a physical OS path.
    // If no virtual prefix matches, returns normalized input path.
    [[nodiscard]] static std::string resolve_path(const std::string& path);

    // Base directory accessors
    [[nodiscard]] static const std::string& get_assets_directory();
    [[nodiscard]] static const std::string& get_save_directory();
    [[nodiscard]] static const std::string& get_app_directory();

    // --- File Reading & Writing ---
    [[nodiscard]] static std::string read_text(const std::string& path);
    static bool write_text(const std::string& path, const std::string& content);

    [[nodiscard]] static std::vector<uint8_t> read_bytes(const std::string& path);
    static bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes);
    static bool write_bytes(const std::string& path, const uint8_t* data, size_t size);

    // --- File & Directory Inspection ---
    [[nodiscard]] static bool exists(const std::string& path);
    [[nodiscard]] static bool is_directory(const std::string& path);
    [[nodiscard]] static size_t file_size(const std::string& path);
    static bool remove(const std::string& path);
    static bool create_directories(const std::string& path);

    // Lists all files in a directory (optionally filtered by extension e.g. ".json" or ".sav")
    [[nodiscard]] static std::vector<std::string> list_files(
        const std::string& dir_path,
        const std::string& extension = "",
        bool recursive = false
    );

private:
    static std::unordered_map<std::string, std::string> s_mount_points;
    static std::string s_assets_dir;
    static std::string s_save_dir;
    static std::string s_app_dir;
    static bool s_initialized;
};

} // namespace Engine
