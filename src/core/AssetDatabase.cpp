#include "AssetDatabase.hpp"

#include "FileSystem.hpp"
#include "Log.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace Engine {

namespace {

constexpr int k_metadata_version = 1;

std::string normalize_path(const fs::path& path) {
    return path.lexically_normal().generic_string();
}

bool is_meta_file(const fs::path& path) {
    return path.extension() == ".meta";
}

AssetType type_from_name(const std::string& name) {
    if (name == "scene") return AssetType::Scene;
    if (name == "prefab") return AssetType::Prefab;
    if (name == "texture") return AssetType::Texture;
    if (name == "audio") return AssetType::Audio;
    if (name == "shader") return AssetType::Shader;
    if (name == "data") return AssetType::Data;
    return AssetType::Unknown;
}

} // namespace

std::string AssetHandle::to_string() const {
    std::ostringstream stream;
    stream << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

std::optional<AssetHandle> AssetHandle::from_string(const std::string& text) {
    if (text.empty() || text.size() > 16) return std::nullopt;
    std::uint64_t parsed = 0;
    std::istringstream stream(text);
    stream >> std::hex >> parsed;
    if (stream.fail() || stream.peek() != std::char_traits<char>::eof() || parsed == 0) return std::nullopt;
    return AssetHandle{parsed};
}

AssetDatabase::AssetDatabase(fs::path project_root)
    : m_project_root(fs::absolute(std::move(project_root)).lexically_normal()) {}

AssetDatabase& AssetDatabase::get() {
    static AssetDatabase database;
    return database;
}

AssetScanResult AssetDatabase::scan(const fs::path& assets_root) {
    clear();
    AssetScanResult result;
    const fs::path resolved_root = resolve_source_path(assets_root);
    std::error_code error;
    if (!fs::exists(resolved_root, error) || !fs::is_directory(resolved_root, error)) {
        ENGINE_LOG_WARN("[ASSET DB] Asset root does not exist: '{}'", resolved_root.string());
        return result;
    }

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(resolved_root, error), end; it != end; it.increment(error)) {
        if (error) {
            ENGINE_LOG_WARN("[ASSET DB] Scan error in '{}': {}", resolved_root.string(), error.message());
            error.clear();
            continue;
        }
        if (it->is_regular_file() && !is_meta_file(it->path())) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    for (const fs::path& file : files) {
        const fs::path meta_path = file.string() + ".meta";
        if (!fs::exists(meta_path, error)) {
            if (import_asset(file).has_value()) ++result.imported;
            else ++result.invalid;
            continue;
        }
        const auto record = read_metadata(file);
        if (!record || !register_record(*record)) {
            ++result.invalid;
            continue;
        }
        ++result.ready;
    }
    result.ready = m_records.size();
    ENGINE_LOG_INFO("[ASSET DB] Scan complete: {} ready, {} imported, {} invalid", result.ready, result.imported, result.invalid);
    return result;
}

void AssetDatabase::clear() {
    m_records.clear();
    m_by_handle.clear();
    m_by_path.clear();
}

std::optional<AssetHandle> AssetDatabase::import_asset(const fs::path& source_path) {
    std::error_code error;
    const fs::path resolved = resolve_source_path(source_path);
    if (!fs::is_regular_file(resolved, error) || is_meta_file(resolved)) return std::nullopt;
    if (const AssetRecord* existing = find_by_path(resolved)) return existing->handle;

    const auto relative = to_project_relative(resolved);
    if (!relative) {
        ENGINE_LOG_ERROR("[ASSET DB] Refusing asset outside project: '{}'", resolved.string());
        return std::nullopt;
    }

    AssetRecord record{generate_handle(), *relative, type_for_path(resolved), AssetStatus::Ready};
    if (!write_metadata(record) || !register_record(record)) return std::nullopt;
    return record.handle;
}

const AssetRecord* AssetDatabase::find(AssetHandle handle) const {
    const auto it = m_by_handle.find(handle.value);
    return it == m_by_handle.end() ? nullptr : &m_records[it->second];
}

const AssetRecord* AssetDatabase::find_by_path(const fs::path& source_path) const {
    const auto relative = to_project_relative(source_path);
    if (!relative) return nullptr;
    const auto it = m_by_path.find(*relative);
    return it == m_by_path.end() ? nullptr : &m_records[it->second];
}

std::string AssetDatabase::resolve(AssetHandle handle) const {
    const AssetRecord* record = find(handle);
    if (!record || record->status != AssetStatus::Ready) return {};
    return (m_project_root / fs::path(record->source_path)).lexically_normal().string();
}

AssetType AssetDatabase::type_for_path(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".scene") return AssetType::Scene;
    if (extension == ".prefab") return AssetType::Prefab;
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp") return AssetType::Texture;
    if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac") return AssetType::Audio;
    if (extension == ".hlsl" || extension == ".msl" || extension == ".glsl" || extension == ".frag" || extension == ".vert") return AssetType::Shader;
    if (extension == ".json") return AssetType::Data;
    return AssetType::Unknown;
}

const char* AssetDatabase::type_name(AssetType type) {
    switch (type) {
        case AssetType::Scene: return "scene";
        case AssetType::Prefab: return "prefab";
        case AssetType::Texture: return "texture";
        case AssetType::Audio: return "audio";
        case AssetType::Shader: return "shader";
        case AssetType::Data: return "data";
        case AssetType::Unknown: return "unknown";
    }
    return "unknown";
}

std::optional<std::string> AssetDatabase::to_project_relative(const fs::path& source_path) const {
    const fs::path resolved = resolve_source_path(source_path);
    const fs::path relative = resolved.lexically_relative(m_project_root);
    if (relative.empty() || relative == "." || (!relative.empty() && relative.begin()->string() == "..")) return std::nullopt;
    return normalize_path(relative);
}

fs::path AssetDatabase::resolve_source_path(const fs::path& source_path) const {
    fs::path resolved = FileSystem::resolve_path(source_path.string());
    if (resolved.is_relative()) resolved = m_project_root / resolved;
    return fs::absolute(resolved).lexically_normal();
}

bool AssetDatabase::register_record(AssetRecord record) {
    if (!record.handle.is_valid() || record.source_path.empty()) return false;
    const auto handle_it = m_by_handle.find(record.handle.value);
    const auto path_it = m_by_path.find(record.source_path);
    if (handle_it != m_by_handle.end() || path_it != m_by_path.end()) {
        ENGINE_LOG_ERROR("[ASSET DB] Duplicate handle or path for '{}'", record.source_path);
        return false;
    }
    const std::size_t index = m_records.size();
    m_records.push_back(std::move(record));
    m_by_handle.emplace(m_records.back().handle.value, index);
    m_by_path.emplace(m_records.back().source_path, index);
    return true;
}

std::optional<AssetRecord> AssetDatabase::read_metadata(const fs::path& source_path) const {
    const auto relative = to_project_relative(source_path);
    if (!relative) return std::nullopt;
    const std::string text = FileSystem::read_text(source_path.string() + ".meta");
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("version", 0) != k_metadata_version) {
        ENGINE_LOG_ERROR("[ASSET DB] Invalid metadata: '{}.meta'", source_path.string());
        return std::nullopt;
    }
    const auto handle = AssetHandle::from_string(json.value("handle", std::string{}));
    if (!handle) {
        ENGINE_LOG_ERROR("[ASSET DB] Invalid handle in '{}.meta'", source_path.string());
        return std::nullopt;
    }
    const AssetType metadata_type = type_from_name(json.value("type", std::string{"unknown"}));
    const AssetType inferred_type = type_for_path(source_path);
    if (metadata_type != inferred_type) {
        ENGINE_LOG_ERROR("[ASSET DB] Metadata type mismatch in '{}.meta'", source_path.string());
        return std::nullopt;
    }
    return AssetRecord{*handle, *relative, inferred_type, AssetStatus::Ready};
}

bool AssetDatabase::write_metadata(const AssetRecord& record) const {
    const fs::path source_path = m_project_root / fs::path(record.source_path);
    const nlohmann::json json{
        {"version", k_metadata_version},
        {"handle", record.handle.to_string()},
        {"type", type_name(record.type)}
    };
    return FileSystem::write_text_atomically(source_path.string() + ".meta", json.dump(2) + "\n");
}

AssetHandle AssetDatabase::generate_handle() const {
    static std::mt19937_64 generator{
        static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) ^ std::random_device{}()
    };
    AssetHandle handle;
    do { handle.value = generator(); } while (!handle.is_valid() || m_by_handle.contains(handle.value));
    return handle;
}

void AssetDatabase::rebuild_indexes() {
    m_by_handle.clear();
    m_by_path.clear();
    for (std::size_t index = 0; index < m_records.size(); ++index) {
        m_by_handle.emplace(m_records[index].handle.value, index);
        m_by_path.emplace(m_records[index].source_path, index);
    }
}

} // namespace Engine
