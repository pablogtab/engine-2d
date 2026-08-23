#include "AssetBrowserQuery.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Engine {
namespace {

namespace fs = std::filesystem;

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<std::string> tokenize(const std::string& query) {
    std::vector<std::string> tokens;
    std::string current;
    bool quoted = false;
    for (const char character : query) {
        if (character == '"') {
            quoted = !quoted;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(character)) && !quoted) {
            if (!current.empty()) {
                tokens.push_back(lower_copy(std::move(current)));
                current.clear();
            }
            continue;
        }
        current += character;
    }
    if (!current.empty()) tokens.push_back(lower_copy(std::move(current)));
    return tokens;
}

bool is_inside(const fs::path& path, const fs::path& root) {
    const fs::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    return !relative.empty() && relative != "." && relative.begin()->string() != "..";
}

bool type_matches_alias(const AssetBrowserEntry& entry, const std::string& alias) {
    if (alias == "folder" || alias == "dir" || alias == "directory") return entry.is_directory;
    if (entry.is_directory) return false;
    if (alias == "scene") return entry.type == AssetType::Scene;
    if (alias == "prefab") return entry.type == AssetType::Prefab;
    if (alias == "texture" || alias == "image") return entry.type == AssetType::Texture;
    if (alias == "font") return entry.type == AssetType::Font;
    if (alias == "audio" || alias == "sound") return entry.type == AssetType::Audio;
    if (alias == "shader") return entry.type == AssetType::Shader;
    if (alias == "material" || alias == "mat") return entry.type == AssetType::Material;
    if (alias == "script" || alias == "e2s") return entry.type == AssetType::Script;
    if (alias == "profile" || alias == "fx") {
        return entry.type == AssetType::PostProcessProfile ||
            entry.type == AssetType::RenderLayerEffectProfile;
    }
    if (alias == "postprocess" || alias == "postfx") {
        return entry.type == AssetType::PostProcessProfile;
    }
    if (alias == "layereffect" || alias == "layerfx") {
        return entry.type == AssetType::RenderLayerEffectProfile;
    }
    if (alias == "data" || alias == "json") return entry.type == AssetType::Data;
    if (alias == "other" || alias == "unknown") return entry.type == AssetType::Unknown;
    return false;
}

bool status_matches_alias(AssetStatus status, const std::string& alias) {
    if (alias == "ready") return status == AssetStatus::Ready;
    if (alias == "loading") return status == AssetStatus::Loading;
    if (alias == "missing") return status == AssetStatus::MissingSource;
    if (alias == "dependency" || alias == "missing_dependency") {
        return status == AssetStatus::MissingDependency;
    }
    if (alias == "failed") return status == AssetStatus::FailedImport;
    if (alias == "invalid") {
        return status == AssetStatus::InvalidMetadata || status == AssetStatus::DuplicateHandle;
    }
    if (alias == "issue" || alias == "issues") return status != AssetStatus::Ready;
    return false;
}

bool query_requests_directories(const std::string& query) {
    for (const std::string& token : tokenize(query)) {
        if (token == "t:folder" || token == "t:dir" || token == "t:directory" ||
            token == "type:folder" || token == "type:dir" || token == "type:directory") {
            return true;
        }
    }
    return false;
}

bool type_filter_matches(const AssetBrowserEntry& entry, AssetBrowserTypeFilter filter) {
    switch (filter) {
        case AssetBrowserTypeFilter::All: return true;
        case AssetBrowserTypeFilter::Folders: return entry.is_directory;
        case AssetBrowserTypeFilter::Scenes: return !entry.is_directory && entry.type == AssetType::Scene;
        case AssetBrowserTypeFilter::Prefabs: return !entry.is_directory && entry.type == AssetType::Prefab;
        case AssetBrowserTypeFilter::Textures: return !entry.is_directory && entry.type == AssetType::Texture;
        case AssetBrowserTypeFilter::Fonts: return !entry.is_directory && entry.type == AssetType::Font;
        case AssetBrowserTypeFilter::Audio: return !entry.is_directory && entry.type == AssetType::Audio;
        case AssetBrowserTypeFilter::Shaders: return !entry.is_directory && entry.type == AssetType::Shader;
        case AssetBrowserTypeFilter::Materials: return !entry.is_directory && entry.type == AssetType::Material;
        case AssetBrowserTypeFilter::Scripts: return !entry.is_directory && entry.type == AssetType::Script;
        case AssetBrowserTypeFilter::Profiles:
            return !entry.is_directory && (entry.type == AssetType::PostProcessProfile ||
                entry.type == AssetType::RenderLayerEffectProfile);
        case AssetBrowserTypeFilter::Data: return !entry.is_directory && entry.type == AssetType::Data;
        case AssetBrowserTypeFilter::Other: return !entry.is_directory && entry.type == AssetType::Unknown;
    }
    return false;
}

bool status_filter_matches(const AssetBrowserEntry& entry, AssetBrowserStatusFilter filter) {
    if (entry.is_directory) return filter != AssetBrowserStatusFilter::Issues;
    switch (filter) {
        case AssetBrowserStatusFilter::All: return true;
        case AssetBrowserStatusFilter::Ready: return entry.status == AssetStatus::Ready;
        case AssetBrowserStatusFilter::Issues: return entry.status != AssetStatus::Ready;
    }
    return false;
}

AssetBrowserEntry make_asset_entry(const AssetDatabase& database, const AssetRecord& record) {
    AssetBrowserEntry entry;
    entry.absolute_path = (database.project_root() / record.source_path).lexically_normal();
    entry.project_path = fs::path(record.source_path).generic_string();
    entry.name = entry.absolute_path.filename().string();
    entry.extension = lower_copy(entry.absolute_path.extension().string());
    entry.handle = record.handle;
    entry.type = record.type;
    entry.status = record.status;
    std::error_code error;
    if (fs::is_regular_file(entry.absolute_path, error)) {
        entry.size = fs::file_size(entry.absolute_path, error);
        error.clear();
        entry.modified = fs::last_write_time(entry.absolute_path, error);
    }
    return entry;
}

AssetBrowserEntry make_directory_entry(const AssetDatabase& database, const fs::path& path) {
    AssetBrowserEntry entry;
    entry.absolute_path = path.lexically_normal();
    entry.project_path = entry.absolute_path.lexically_relative(database.project_root()).generic_string();
    entry.name = entry.absolute_path.filename().string();
    entry.is_directory = true;
    return entry;
}

} // namespace

std::vector<AssetBrowserEntry> AssetBrowserQuery::collect(
    const AssetDatabase& database,
    const fs::path& assets_root,
    const fs::path& current_directory,
    const AssetBrowserQueryOptions& options
) {
    const fs::path root = fs::absolute(assets_root).lexically_normal();
    const fs::path current = fs::absolute(current_directory).lexically_normal();
    std::vector<AssetBrowserEntry> entries;

    const bool collect_project_directories = options.scope == AssetBrowserScope::Project &&
        (options.type == AssetBrowserTypeFilter::Folders || query_requests_directories(options.text));
    if ((options.scope == AssetBrowserScope::CurrentFolder || collect_project_directories) &&
        (current == root || is_inside(current, root))) {
        std::error_code error;
        if (collect_project_directories) {
            for (fs::recursive_directory_iterator iterator(root, error), end;
                 iterator != end && !error; iterator.increment(error)) {
                if (!iterator->is_directory(error)) continue;
                AssetBrowserEntry entry = make_directory_entry(database, iterator->path());
                if (type_filter_matches(entry, options.type) &&
                    status_filter_matches(entry, options.status) && matches_text(entry, options.text)) {
                    entries.push_back(std::move(entry));
                }
            }
        } else {
            for (fs::directory_iterator iterator(current, error), end;
                 iterator != end && !error; iterator.increment(error)) {
                if (!iterator->is_directory(error)) continue;
                AssetBrowserEntry entry = make_directory_entry(database, iterator->path());
                if (type_filter_matches(entry, options.type) &&
                    status_filter_matches(entry, options.status) && matches_text(entry, options.text)) {
                    entries.push_back(std::move(entry));
                }
            }
        }
    }

    for (const AssetRecord& record : database.records()) {
        AssetBrowserEntry entry = make_asset_entry(database, record);
        if (!is_inside(entry.absolute_path, root)) continue;
        if (options.scope == AssetBrowserScope::CurrentFolder &&
            entry.absolute_path.parent_path() != current) continue;
        if (!type_filter_matches(entry, options.type) ||
            !status_filter_matches(entry, options.status) ||
            !matches_text(entry, options.text)) continue;
        entries.push_back(std::move(entry));
    }

    const auto less = [&options](const AssetBrowserEntry& left, const AssetBrowserEntry& right) {
        if (left.is_directory != right.is_directory) return left.is_directory;
        int comparison = 0;
        switch (options.sort) {
            case AssetBrowserSort::Name:
                comparison = lower_copy(left.name).compare(lower_copy(right.name));
                break;
            case AssetBrowserSort::Type:
                comparison = std::string(AssetDatabase::type_name(left.type)).compare(
                    AssetDatabase::type_name(right.type)
                );
                break;
            case AssetBrowserSort::Modified:
                if (left.modified < right.modified) comparison = -1;
                else if (left.modified > right.modified) comparison = 1;
                break;
            case AssetBrowserSort::Size:
                if (left.size < right.size) comparison = -1;
                else if (left.size > right.size) comparison = 1;
                break;
        }
        if (comparison == 0) comparison = lower_copy(left.name).compare(lower_copy(right.name));
        return options.ascending ? comparison < 0 : comparison > 0;
    };
    std::stable_sort(entries.begin(), entries.end(), less);
    return entries;
}

bool AssetBrowserQuery::matches_text(const AssetBrowserEntry& entry, const std::string& query) {
    const std::string lower_name = lower_copy(entry.name);
    const std::string lower_path = lower_copy(entry.project_path);
    const std::string lower_extension = entry.extension.starts_with('.')
        ? entry.extension.substr(1) : entry.extension;
    const std::string handle = entry.handle.is_valid() ? entry.handle.to_string() : std::string{};
    for (const std::string& token : tokenize(query)) {
        const std::size_t separator = token.find(':');
        if (separator != std::string::npos) {
            const std::string key = token.substr(0, separator);
            const std::string value = token.substr(separator + 1);
            if (value.empty()) return false;
            if ((key == "t" || key == "type") && !type_matches_alias(entry, value)) return false;
            if (key == "status" && !entry.is_directory && !status_matches_alias(entry.status, value)) return false;
            if (key == "status" && entry.is_directory && value != "ready") return false;
            if (key == "ext" && lower_extension != value) return false;
            if (key == "path" && lower_path.find(value) == std::string::npos) return false;
            if ((key == "id" || key == "handle") && handle.find(value) == std::string::npos) return false;
            if (key == "t" || key == "type" || key == "status" || key == "ext" ||
                key == "path" || key == "id" || key == "handle") continue;
        }
        if (lower_name.find(token) == std::string::npos &&
            lower_path.find(token) == std::string::npos) return false;
    }
    return true;
}

const char* AssetBrowserQuery::type_filter_name(AssetBrowserTypeFilter filter) {
    switch (filter) {
        case AssetBrowserTypeFilter::All: return "All types";
        case AssetBrowserTypeFilter::Folders: return "Folders";
        case AssetBrowserTypeFilter::Scenes: return "Scenes";
        case AssetBrowserTypeFilter::Prefabs: return "Prefabs";
        case AssetBrowserTypeFilter::Textures: return "Textures";
        case AssetBrowserTypeFilter::Fonts: return "Fonts";
        case AssetBrowserTypeFilter::Audio: return "Audio";
        case AssetBrowserTypeFilter::Shaders: return "Shaders";
        case AssetBrowserTypeFilter::Materials: return "Materials";
        case AssetBrowserTypeFilter::Scripts: return "Scripts";
        case AssetBrowserTypeFilter::Profiles: return "Profiles";
        case AssetBrowserTypeFilter::Data: return "Data";
        case AssetBrowserTypeFilter::Other: return "Other";
    }
    return "All types";
}

const char* AssetBrowserQuery::status_filter_name(AssetBrowserStatusFilter filter) {
    switch (filter) {
        case AssetBrowserStatusFilter::All: return "All states";
        case AssetBrowserStatusFilter::Ready: return "Ready";
        case AssetBrowserStatusFilter::Issues: return "Issues";
    }
    return "All states";
}

const char* AssetBrowserQuery::sort_name(AssetBrowserSort sort) {
    switch (sort) {
        case AssetBrowserSort::Name: return "Name";
        case AssetBrowserSort::Type: return "Type";
        case AssetBrowserSort::Modified: return "Modified";
        case AssetBrowserSort::Size: return "Size";
    }
    return "Name";
}

const char* AssetBrowserQuery::status_name(AssetStatus status) {
    switch (status) {
        case AssetStatus::Loading: return "Loading";
        case AssetStatus::Ready: return "Ready";
        case AssetStatus::MissingSource: return "Missing source";
        case AssetStatus::MissingDependency: return "Missing dependency";
        case AssetStatus::FailedImport: return "Failed import";
        case AssetStatus::InvalidMetadata: return "Invalid metadata";
        case AssetStatus::DuplicateHandle: return "Duplicate handle";
    }
    return "Unknown";
}

} // namespace Engine
