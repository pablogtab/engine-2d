#pragma once

#include "AssetHandle.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

enum class AssetType {
    Unknown,
    Scene,
    Prefab,
    Texture,
    Audio,
    Shader,
    Data
};

enum class AssetStatus {
    Ready,
    MissingSource,
    InvalidMetadata,
    DuplicateHandle
};

struct AssetRecord {
    AssetHandle handle{};
    std::string source_path; // normalized, project-relative path
    AssetType type{AssetType::Unknown};
    AssetStatus status{AssetStatus::Ready};
};

struct AssetScanResult {
    std::size_t imported{0};
    std::size_t ready{0};
    std::size_t invalid{0};
};

class AssetDatabase {
public:
    explicit AssetDatabase(std::filesystem::path project_root = ".");

    static AssetDatabase& get();

    // Scans an asset root, persisting identity in Library/AssetDatabase.json.
    [[nodiscard]] AssetScanResult scan(const std::filesystem::path& assets_root = "assets");
    [[nodiscard]] AssetScanResult rescan();
    void clear();

    [[nodiscard]] std::optional<AssetHandle> import_asset(const std::filesystem::path& source_path);
    [[nodiscard]] bool move_asset(const std::filesystem::path& source_path, const std::filesystem::path& destination_path);
    [[nodiscard]] bool remove_asset(const std::filesystem::path& source_path);
    [[nodiscard]] const AssetRecord* find(AssetHandle handle) const;
    [[nodiscard]] const AssetRecord* find_by_path(const std::filesystem::path& source_path) const;
    [[nodiscard]] std::string resolve(AssetHandle handle) const;
    [[nodiscard]] const std::vector<AssetRecord>& records() const { return m_records; }

    [[nodiscard]] const std::filesystem::path& project_root() const { return m_project_root; }
    [[nodiscard]] std::filesystem::path database_path() const;
    [[nodiscard]] static AssetType type_for_path(const std::filesystem::path& path);
    [[nodiscard]] static const char* type_name(AssetType type);

private:
    [[nodiscard]] std::filesystem::path resolve_source_path(const std::filesystem::path& source_path) const;
    [[nodiscard]] std::optional<std::string> to_project_relative(const std::filesystem::path& source_path) const;
    [[nodiscard]] bool register_record(AssetRecord record);
    [[nodiscard]] bool load_database();
    [[nodiscard]] bool save_database() const;
    [[nodiscard]] AssetHandle generate_handle() const;
    void rebuild_indexes();

    std::filesystem::path m_project_root;
    std::filesystem::path m_scanned_root{"assets"};
    std::vector<AssetRecord> m_records;
    std::unordered_map<std::uint64_t, std::size_t> m_by_handle;
    std::unordered_map<std::string, std::size_t> m_by_path;
};

} // namespace Engine
