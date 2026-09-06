#pragma once

#include "AssetHandle.hpp"
#include <filesystem>
#include <functional>
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
    Font,
    Audio,
    Shader,
    Material,
    PostProcessProfile,
    RenderLayerEffectProfile,
    ParticleEffect,
    LevelPalette,
    Script,
    Data
};

enum class AssetStatus {
    Loading,
    Ready,
    MissingSource,
    MissingDependency,
    FailedImport,
    InvalidMetadata,
    DuplicateHandle
};

struct AssetImportSettings {
    std::string filter{"linear"};
    float pixels_per_unit{100.0f};
    float pivot_x{0.5f};
    float pivot_y{0.5f};
    std::string compression{"default"};
    std::unordered_map<std::string, std::string> platform_overrides;
};

struct AssetRecord {
    AssetHandle handle{};
    std::string source_path; // normalized, project-relative path
    AssetType type{AssetType::Unknown};
    AssetStatus status{AssetStatus::Ready};
    AssetImportSettings import_settings{};
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

    void set_project_root(const std::filesystem::path& project_root);
    void set_read_only(bool read_only) { m_read_only = read_only; }
    [[nodiscard]] bool is_read_only() const { return m_read_only; }

    // Scans an asset root, persisting identity in ProjectSettings/AssetDatabase.json.
    [[nodiscard]] AssetScanResult scan(const std::filesystem::path& assets_root = "Assets");
    [[nodiscard]] AssetScanResult rescan();
    void clear();

    [[nodiscard]] std::optional<AssetHandle> import_asset(const std::filesystem::path& source_path);
    [[nodiscard]] bool move_asset(const std::filesystem::path& source_path, const std::filesystem::path& destination_path);
    [[nodiscard]] bool remove_asset(const std::filesystem::path& source_path);
    // Removes only an unreferenced MissingSource record when an uncommitted
    // asset-creation command is abandoned and no Redo can still restore it.
    [[nodiscard]] bool discard_missing_asset_record(const std::filesystem::path& source_path);
    [[nodiscard]] bool update_import_settings(AssetHandle handle, AssetImportSettings settings);
    [[nodiscard]] const AssetRecord* find(AssetHandle handle) const;
    [[nodiscard]] const AssetRecord* find_by_path(const std::filesystem::path& source_path) const;
    [[nodiscard]] std::string resolve(AssetHandle handle) const;
    [[nodiscard]] const std::vector<AssetHandle>& dependencies(AssetHandle handle) const;
    [[nodiscard]] const std::vector<AssetHandle>& dependents(AssetHandle handle) const;
    [[nodiscard]] const std::vector<AssetRecord>& records() const { return m_records; }
    void set_on_assets_changed(std::function<void()> callback) { m_on_assets_changed = std::move(callback); }

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
    bool commit_records(AssetDatabase&& candidate, bool notify = true);
    void rebuild_indexes();
    void rebuild_dependencies();
    void notify_assets_changed();

    std::filesystem::path m_project_root;
    std::filesystem::path m_scanned_root{"Assets"};
    std::vector<AssetRecord> m_records;
    std::unordered_map<std::uint64_t, std::size_t> m_by_handle;
    std::unordered_map<std::string, std::size_t> m_by_path;
    std::unordered_map<std::uint64_t, std::vector<AssetHandle>> m_dependencies;
    std::unordered_map<std::uint64_t, std::vector<AssetHandle>> m_dependents;
    std::function<void()> m_on_assets_changed;
    bool m_read_only{false};
};

} // namespace Engine
