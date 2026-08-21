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
#include <unordered_set>

namespace fs = std::filesystem;

namespace Engine {
namespace {
constexpr int k_database_version = 1;
std::string normalize_path(const fs::path& path) { return path.lexically_normal().generic_string(); }
AssetType type_from_name(const std::string& name) {
    if (name == "scene") return AssetType::Scene; if (name == "prefab") return AssetType::Prefab;
    if (name == "texture") return AssetType::Texture; if (name == "audio") return AssetType::Audio;
    if (name == "shader") return AssetType::Shader; if (name == "material") return AssetType::Material;
    if (name == "post_process_profile") return AssetType::PostProcessProfile; if (name == "render_layer_effect_profile") return AssetType::RenderLayerEffectProfile; if (name == "data") return AssetType::Data;
    return AssetType::Unknown;
}
AssetStatus status_from_name(const std::string& value) {
    if (value == "loading") return AssetStatus::Loading;
    if (value == "missing") return AssetStatus::MissingSource;
    if (value == "missing_dependency") return AssetStatus::MissingDependency;
    return value == "failed" ? AssetStatus::FailedImport : AssetStatus::Ready;
}
const char* status_name(AssetStatus value) {
    if (value == AssetStatus::Loading) return "loading";
    if (value == AssetStatus::MissingSource) return "missing";
    if (value == AssetStatus::MissingDependency) return "missing_dependency";
    return value == AssetStatus::FailedImport ? "failed" : "ready";
}
} // namespace

std::string AssetHandle::to_string() const { std::ostringstream s; s << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << value; return s.str(); }
std::optional<AssetHandle> AssetHandle::from_string(const std::string& text) {
    if (text.empty() || text.size() > 16) return std::nullopt;
    std::uint64_t value = 0; std::istringstream s(text); s >> std::hex >> value;
    if (s.fail() || s.peek() != std::char_traits<char>::eof() || value == 0) return std::nullopt;
    return AssetHandle{value};
}

AssetDatabase::AssetDatabase(fs::path root) : m_project_root(fs::absolute(std::move(root)).lexically_normal()) {}
AssetDatabase& AssetDatabase::get() { static AssetDatabase database; return database; }
fs::path AssetDatabase::database_path() const { return m_project_root / "ProjectSettings/AssetDatabase.json"; }

AssetScanResult AssetDatabase::scan(const fs::path& assets_root) {
    m_scanned_root = assets_root;
    const bool loaded = load_database();
    AssetScanResult result;
    const fs::path root = resolve_source_path(assets_root);
    std::error_code error;
    if (!fs::is_directory(root, error)) return result;
    std::unordered_set<std::string> seen;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(root, error), end; it != end; it.increment(error)) {
        if (error) { error.clear(); continue; }
        if (it->is_regular_file()) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        const auto path = to_project_relative(file); if (!path) continue;
        seen.insert(*path);
        const auto found = m_by_path.find(*path);
        if (found == m_by_path.end()) {
            if (register_record({generate_handle(), *path, type_for_path(file), AssetStatus::Ready})) ++result.imported;
            else ++result.invalid;
        } else {
            auto& record = m_records[found->second]; record.status = AssetStatus::Ready; record.type = type_for_path(file);
        }
    }
    for (auto& record : m_records) {
        if (!seen.contains(record.source_path)) record.status = AssetStatus::MissingSource;
    }
    rebuild_dependencies();
    for (const auto& record : m_records) if (record.status == AssetStatus::Ready) ++result.ready;
    if (!save_database()) ++result.invalid;
    notify_assets_changed();
    ENGINE_LOG_INFO("[ASSET DB] Scan complete: {} ready, {} imported, {} invalid{}", result.ready, result.imported, result.invalid, loaded ? "" : " (new DB)");
    return result;
}
AssetScanResult AssetDatabase::rescan() { return scan(m_scanned_root); }
void AssetDatabase::clear() { m_records.clear(); m_by_handle.clear(); m_by_path.clear(); m_dependencies.clear(); m_dependents.clear(); }

std::optional<AssetHandle> AssetDatabase::import_asset(const fs::path& source_path) {
    std::error_code error; const auto file = resolve_source_path(source_path);
    if (!fs::is_regular_file(file, error)) return std::nullopt;
    const auto path = to_project_relative(file); if (!path) return std::nullopt;
    const auto found = m_by_path.find(*path);
    if (found != m_by_path.end()) {
        m_records[found->second].status = AssetStatus::Ready;
        m_records[found->second].type = type_for_path(file);
        rebuild_dependencies();
        if (!save_database()) return std::nullopt;
        notify_assets_changed();
        return m_records[found->second].handle;
    }
    AssetRecord record{generate_handle(), *path, type_for_path(file), AssetStatus::Ready};
    if (!register_record(record)) return std::nullopt;
    rebuild_dependencies();
    if (!save_database()) return std::nullopt;
    notify_assets_changed();
    return record.handle;
}

bool AssetDatabase::move_asset(const fs::path& source, const fs::path& destination) {
    const auto from = to_project_relative(source), to = to_project_relative(destination);
    if (!from || !to || m_by_path.contains(*to)) return false;
    const auto found = m_by_path.find(*from); if (found == m_by_path.end()) return false;
    m_records[found->second].source_path = *to; m_records[found->second].status = AssetStatus::Ready; rebuild_indexes(); rebuild_dependencies();
    if (!save_database()) return false;
    notify_assets_changed();
    return true;
}
bool AssetDatabase::remove_asset(const fs::path& source) {
    const auto path = to_project_relative(source); if (!path) return false;
    const auto found = m_by_path.find(*path); if (found == m_by_path.end()) return false;
    m_records[found->second].status = AssetStatus::MissingSource; rebuild_dependencies();
    if (!save_database()) return false;
    notify_assets_changed();
    return true;
}
bool AssetDatabase::update_import_settings(AssetHandle handle, AssetImportSettings settings) {
    if (settings.pixels_per_unit <= 0.0f || settings.pivot_x < 0.0f || settings.pivot_x > 1.0f ||
        settings.pivot_y < 0.0f || settings.pivot_y > 1.0f || settings.filter.empty() || settings.compression.empty()) return false;
    const auto found = m_by_handle.find(handle.value);
    if (found == m_by_handle.end()) return false;
    m_records[found->second].import_settings = std::move(settings);
    return save_database();
}

const AssetRecord* AssetDatabase::find(AssetHandle handle) const { const auto i = m_by_handle.find(handle.value); return i == m_by_handle.end() ? nullptr : &m_records[i->second]; }
const AssetRecord* AssetDatabase::find_by_path(const fs::path& path) const { const auto relative = to_project_relative(path); if (!relative) return nullptr; const auto i = m_by_path.find(*relative); return i == m_by_path.end() ? nullptr : &m_records[i->second]; }
std::string AssetDatabase::resolve(AssetHandle handle) const { const auto* record = find(handle); return !record || record->status != AssetStatus::Ready ? "" : (m_project_root / record->source_path).lexically_normal().string(); }
const std::vector<AssetHandle>& AssetDatabase::dependencies(AssetHandle handle) const { static const std::vector<AssetHandle> empty; const auto found = m_dependencies.find(handle.value); return found == m_dependencies.end() ? empty : found->second; }
const std::vector<AssetHandle>& AssetDatabase::dependents(AssetHandle handle) const { static const std::vector<AssetHandle> empty; const auto found = m_dependents.find(handle.value); return found == m_dependents.end() ? empty : found->second; }

AssetType AssetDatabase::type_for_path(const fs::path& path) {
    std::string ext = path.extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (ext == ".scene") return AssetType::Scene; if (ext == ".prefab") return AssetType::Prefab;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") return AssetType::Texture;
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") return AssetType::Audio;
    if (ext == ".shader") return AssetType::Shader;
    if (ext == ".material") return AssetType::Material;
    if (ext == ".postprocess") return AssetType::PostProcessProfile;
    if (ext == ".layereffect") return AssetType::RenderLayerEffectProfile;
    return ext == ".json" ? AssetType::Data : AssetType::Unknown;
}
const char* AssetDatabase::type_name(AssetType type) { switch (type) { case AssetType::Scene:return "scene"; case AssetType::Prefab:return "prefab"; case AssetType::Texture:return "texture"; case AssetType::Audio:return "audio"; case AssetType::Shader:return "shader"; case AssetType::Material:return "material"; case AssetType::PostProcessProfile:return "post_process_profile"; case AssetType::RenderLayerEffectProfile:return "render_layer_effect_profile"; case AssetType::Data:return "data"; default:return "unknown"; } }

std::optional<std::string> AssetDatabase::to_project_relative(const fs::path& path) const { const auto relative = resolve_source_path(path).lexically_relative(m_project_root); if (relative.empty() || relative == "." || relative.begin()->string() == "..") return std::nullopt; return normalize_path(relative); }
fs::path AssetDatabase::resolve_source_path(const fs::path& path) const { fs::path result = FileSystem::resolve_path(path.string()); if (result.is_relative()) result = m_project_root / result; return fs::absolute(result).lexically_normal(); }
bool AssetDatabase::register_record(AssetRecord record) { if (!record.handle.is_valid() || record.source_path.empty() || m_by_handle.contains(record.handle.value) || m_by_path.contains(record.source_path)) return false; const auto i=m_records.size(); m_records.push_back(std::move(record)); m_by_handle.emplace(m_records.back().handle.value,i); m_by_path.emplace(m_records.back().source_path,i); return true; }

void AssetDatabase::rebuild_dependencies() {
    m_dependencies.clear();
    m_dependents.clear();
    for (const auto& record : m_records) {
        if (record.status == AssetStatus::MissingSource) continue;
        const auto json = nlohmann::json::parse(FileSystem::read_text((m_project_root / record.source_path).string()), nullptr, false);
        if (json.is_discarded()) continue;
        std::vector<AssetHandle> dependencies;
        const auto append = [&dependencies](const nlohmann::json& value) {
            if (!value.is_string()) return;
            if (const auto handle = AssetHandle::from_string(value.get<std::string>()); handle) dependencies.push_back(*handle);
        };
        if (record.type == AssetType::Material) append(json.value("shader_handle", nlohmann::json{}));
        if (record.type == AssetType::RenderLayerEffectProfile && json.contains("passes") && json["passes"].is_array()) {
            for (const auto& pass : json["passes"]) if (pass.is_object()) append(pass.value("material_handle", nlohmann::json{}));
        }
        if (record.type == AssetType::Shader && json.contains("variants") && json["variants"].is_array()) {
            for (const auto& variant : json["variants"]) if (variant.is_object()) append(variant.value("source_handle", nlohmann::json{}));
        }
        if (!dependencies.empty()) {
            m_dependencies.emplace(record.handle.value, dependencies);
            for (const auto dependency : dependencies) m_dependents[dependency.value].push_back(record.handle);
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& record : m_records) {
            if (record.status != AssetStatus::Ready) continue;
            const auto found = m_dependencies.find(record.handle.value);
            if (found == m_dependencies.end()) continue;
            const bool missing = std::any_of(found->second.begin(), found->second.end(), [this](AssetHandle dependency) {
                const AssetRecord* record = find(dependency);
                return !record || record->status != AssetStatus::Ready;
            });
            if (missing) { record.status = AssetStatus::MissingDependency; changed = true; }
        }
    }
}

void AssetDatabase::notify_assets_changed() {
    if (m_on_assets_changed) m_on_assets_changed();
}

bool AssetDatabase::load_database() {
    clear(); const auto text = FileSystem::read_text(database_path().string()); if (text.empty()) return false;
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || json.value("version", 0) != k_database_version || !json.contains("assets") || !json["assets"].is_array()) return false;
    for (const auto& item : json["assets"]) {
        const auto handle = AssetHandle::from_string(item.value("handle", std::string{}));
        if (!handle || !item["path"].is_string()) { clear(); return false; }
        AssetImportSettings settings;
        if (const auto imported = item.find("import_settings"); imported != item.end() && imported->is_object()) {
            settings.filter = imported->value("filter", settings.filter);
            settings.pixels_per_unit = imported->value("pixels_per_unit", settings.pixels_per_unit);
            settings.pivot_x = imported->value("pivot_x", settings.pivot_x);
            settings.pivot_y = imported->value("pivot_y", settings.pivot_y);
            settings.compression = imported->value("compression", settings.compression);
            settings.platform_overrides = imported->value("platform_overrides", settings.platform_overrides);
        }
        if (!register_record({*handle, item["path"].get<std::string>(), type_from_name(item.value("type", "unknown")), status_from_name(item.value("status", "ready")), std::move(settings)})) { clear(); return false; }
    } return true;
}
bool AssetDatabase::save_database() const {
    nlohmann::json assets=nlohmann::json::array(); for (const auto& r:m_records) assets.push_back({{"handle",r.handle.to_string()},{"path",r.source_path},{"type",type_name(r.type)},{"status",status_name(r.status)},{"import_settings",{{"filter",r.import_settings.filter},{"pixels_per_unit",r.import_settings.pixels_per_unit},{"pivot_x",r.import_settings.pivot_x},{"pivot_y",r.import_settings.pivot_y},{"compression",r.import_settings.compression},{"platform_overrides",r.import_settings.platform_overrides}}}});
    return FileSystem::write_text_atomically(database_path().string(), nlohmann::json{{"version",k_database_version},{"assets",assets}}.dump(2)+"\n");
}
AssetHandle AssetDatabase::generate_handle() const { static std::mt19937_64 g{static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())^std::random_device{}()}; AssetHandle h; do {h.value=g();} while(!h.is_valid()||m_by_handle.contains(h.value)); return h; }
void AssetDatabase::rebuild_indexes() { m_by_handle.clear();m_by_path.clear();for(std::size_t i=0;i<m_records.size();++i){m_by_handle.emplace(m_records[i].handle.value,i);m_by_path.emplace(m_records[i].source_path,i);} }
} // namespace Engine
