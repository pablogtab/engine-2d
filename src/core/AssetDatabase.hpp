#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

// Stable project asset identity. It is never a pointer and remains valid while
// its .meta sidecar moves together with its source asset.
struct AssetHandle {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool is_valid() const { return value != 0; }
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] static std::optional<AssetHandle> from_string(const std::string& text);

    constexpr bool operator==(const AssetHandle&) const = default;
};

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

    // Scans an asset root. Assets without a sidecar receive one atomically.
    // Bad or duplicate metadata is reported as invalid and never silently remapped.
    [[nodiscard]] AssetScanResult scan(const std::filesystem::path& assets_root = "assets");
    void clear();

    [[nodiscard]] std::optional<AssetHandle> import_asset(const std::filesystem::path& source_path);
    [[nodiscard]] const AssetRecord* find(AssetHandle handle) const;
    [[nodiscard]] const AssetRecord* find_by_path(const std::filesystem::path& source_path) const;
    [[nodiscard]] std::string resolve(AssetHandle handle) const;
    [[nodiscard]] const std::vector<AssetRecord>& records() const { return m_records; }

    [[nodiscard]] const std::filesystem::path& project_root() const { return m_project_root; }
    [[nodiscard]] static AssetType type_for_path(const std::filesystem::path& path);
    [[nodiscard]] static const char* type_name(AssetType type);

private:
    [[nodiscard]] std::filesystem::path resolve_source_path(const std::filesystem::path& source_path) const;
    [[nodiscard]] std::optional<std::string> to_project_relative(const std::filesystem::path& source_path) const;
    [[nodiscard]] bool register_record(AssetRecord record);
    [[nodiscard]] std::optional<AssetRecord> read_metadata(const std::filesystem::path& source_path) const;
    [[nodiscard]] bool write_metadata(const AssetRecord& record) const;
    [[nodiscard]] AssetHandle generate_handle() const;
    void rebuild_indexes();

    std::filesystem::path m_project_root;
    std::vector<AssetRecord> m_records;
    std::unordered_map<std::uint64_t, std::size_t> m_by_handle;
    std::unordered_map<std::string, std::size_t> m_by_path;
};

} // namespace Engine

template<>
struct std::hash<Engine::AssetHandle> {
    std::size_t operator()(Engine::AssetHandle handle) const noexcept {
        return std::hash<std::uint64_t>{}(handle.value);
    }
};
