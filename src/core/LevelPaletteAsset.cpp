#include "LevelPaletteAsset.hpp"

#include "AssetDatabase.hpp"
#include "FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Engine {

namespace {

constexpr const char* LEVEL_PALETTE_TYPE = "Engine2D.LevelPalette";
constexpr std::size_t MAX_CATEGORIES = 256;
constexpr std::size_t MAX_ENTRIES = 4096;
constexpr float MAX_WORLD_VALUE = 1'000'000.0f;

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

bool fail(std::string* error, const std::string& message) {
    set_error(error, message);
    return false;
}

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool valid_identifier(std::string_view value) {
    if (value.empty() || value.size() > 64 || value.front() == '.' || value.back() == '.') {
        return false;
    }
    for (const char character : value) {
        const bool letter = (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        if (!letter && !digit && character != '_' && character != '-' && character != '.') {
            return false;
        }
    }
    return true;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool valid_project_asset_path(const std::string& value) {
    if (value.empty()) return false;
    const std::filesystem::path path(value);
    if (path.is_absolute()) return false;
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    const auto first = normalized.begin();
    if (first == normalized.end() || lowercase(first->string()) != "assets") return false;
    return std::none_of(normalized.begin(), normalized.end(), [](const auto& part) {
        return part == "..";
    });
}

bool valid_path_extension(const std::string& value, AssetType expected_type) {
    if (value.empty()) return true;
    const std::string extension = lowercase(std::filesystem::path(value).extension().string());
    if (expected_type == AssetType::Texture) {
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
            extension == ".bmp";
    }
    if (expected_type == AssetType::Prefab) return extension == ".prefab";
    return false;
}

bool validate_reference(
    const LevelPaletteAssetReference& reference,
    AssetType expected_type,
    const char* description,
    const AssetDatabase* database,
    std::string* error
) {
    if (!reference.handle.is_valid() && reference.path.empty()) {
        return fail(error, std::string(description) + " must provide an asset handle or path");
    }
    if (!reference.path.empty() && !valid_project_asset_path(reference.path)) {
        return fail(error, std::string(description) + " path must be project-relative under Assets/");
    }
    if (!valid_path_extension(reference.path, expected_type)) {
        return fail(error, std::string(description) + " path has an incompatible file extension");
    }
    if (!database) return true;

    const AssetRecord* record = reference.handle.is_valid()
        ? database->find(reference.handle)
        : nullptr;
    if (!record && !reference.path.empty()) record = database->find_by_path(reference.path);
    if (!record) {
        return fail(error, std::string(description) + " does not resolve in the Asset Database");
    }
    if (record->status != AssetStatus::Ready) {
        return fail(error, std::string(description) + " must reference a ready asset");
    }
    if (record->type != expected_type) {
        return fail(error, std::string(description) + " references the wrong asset type");
    }
    return true;
}

bool canonicalize_reference(
    LevelPaletteAssetReference& reference,
    AssetType expected_type,
    const char* description,
    const AssetDatabase& database,
    std::string* error
) {
    const AssetRecord* record = nullptr;
    if (reference.handle.is_valid()) {
        record = database.find(reference.handle);
    }
    if (!record && !reference.path.empty()) {
        record = database.find_by_path(reference.path);
    }
    if (!record) {
        return fail(error, std::string(description) + " does not resolve in the Asset Database");
    }
    if (record->status != AssetStatus::Ready) {
        return fail(error, std::string(description) + " must reference a ready asset");
    }
    if (record->type != expected_type) {
        return fail(error, std::string(description) + " references the wrong asset type");
    }
    reference.handle = record->handle;
    reference.path = record->source_path;
    return true;
}

bool parse_vec2(
    const nlohmann::json& value,
    glm::vec2& output,
    const std::string& description,
    std::string* error
) {
    if (!value.is_array() || value.size() != 2 ||
        !value[0].is_number() || !value[1].is_number()) {
        return fail(error, description + " must be a two-number array");
    }
    output = {value[0].get<float>(), value[1].get<float>()};
    return true;
}

bool parse_ivec4(
    const nlohmann::json& value,
    glm::ivec4& output,
    const std::string& description,
    std::string* error
) {
    if (!value.is_array() || value.size() != 4) {
        return fail(error, description + " must be a four-integer array");
    }
    for (const auto& item : value) {
        if (!item.is_number_integer() && !item.is_number_unsigned()) {
            return fail(error, description + " must be a four-integer array");
        }
    }
    output = {
        value[0].get<int>(), value[1].get<int>(),
        value[2].get<int>(), value[3].get<int>()
    };
    return true;
}

bool parse_asset_reference(
    const nlohmann::json& value,
    const char* prefix,
    LevelPaletteAssetReference& output,
    std::string* error
) {
    const std::string handle_key = std::string(prefix) + "_handle";
    const std::string path_key = std::string(prefix) + "_path";
    if (value.contains(handle_key)) {
        if (!value.at(handle_key).is_string()) {
            return fail(error, handle_key + " must be a hexadecimal string");
        }
        const auto handle = AssetHandle::from_string(value.at(handle_key).get<std::string>());
        if (!handle) return fail(error, handle_key + " is invalid");
        output.handle = *handle;
    }
    if (value.contains(path_key)) {
        if (!value.at(path_key).is_string()) return fail(error, path_key + " must be a string");
        output.path = value.at(path_key).get<std::string>();
    }
    if (!output.handle.is_valid() && output.path.empty()) {
        return fail(error, std::string("Entry must contain ") + handle_key + " or " + path_key);
    }
    return true;
}

bool parse_placement(
    const nlohmann::json& value,
    LevelPalettePlacement& output,
    std::string* error
) {
    if (!value.is_object()) return fail(error, "Entry placement must be an object");
    for (const char* field : {
             "snap", "spacing", "rotation_degrees", "flip_x", "flip_y",
             "sorting_layer", "order_in_layer", "render_layer"
         }) {
        if (!value.contains(field)) {
            return fail(error, std::string("Entry placement is missing '") + field + "'");
        }
    }
    if (!parse_vec2(value.at("snap"), output.snap, "Entry placement snap", error)) return false;
    if (!value.at("spacing").is_number() || !value.at("rotation_degrees").is_number() ||
        !value.at("flip_x").is_boolean() || !value.at("flip_y").is_boolean()) {
        return fail(error, "Entry placement scalar fields have invalid types");
    }
    output.spacing = value.at("spacing").get<float>();
    output.rotation_degrees = value.at("rotation_degrees").get<float>();
    output.depth_z = 0.0f;
    if (value.contains("depth_z")) {
        if (!value.at("depth_z").is_number()) {
            return fail(error, "Entry placement 'depth_z' must be a number");
        }
        output.depth_z = value.at("depth_z").get<float>();
    }
    output.flip_x = value.at("flip_x").get<bool>();
    output.flip_y = value.at("flip_y").get<bool>();

    for (const char* field : {"sorting_layer", "order_in_layer", "render_layer"}) {
        if (!value.at(field).is_number_integer() && !value.at(field).is_number_unsigned()) {
            return fail(error, std::string("Entry placement '") + field + "' must be an integer");
        }
    }
    const std::int64_t sorting_layer = value.at("sorting_layer").get<std::int64_t>();
    const std::int64_t order_in_layer = value.at("order_in_layer").get<std::int64_t>();
    const std::int64_t render_layer = value.at("render_layer").get<std::int64_t>();
    if (sorting_layer < std::numeric_limits<std::int16_t>::min() ||
        sorting_layer > std::numeric_limits<std::int16_t>::max() ||
        order_in_layer < std::numeric_limits<std::int16_t>::min() ||
        order_in_layer > std::numeric_limits<std::int16_t>::max() ||
        render_layer < 0 || render_layer > 31) {
        return fail(error, "Entry placement layer values are out of range");
    }
    output.sorting_layer = static_cast<std::int16_t>(sorting_layer);
    output.order_in_layer = static_cast<std::int16_t>(order_in_layer);
    output.render_layer = static_cast<RenderLayer>(render_layer);
    return true;
}

nlohmann::json reference_json(
    const LevelPaletteAssetReference& reference,
    const char* prefix
) {
    nlohmann::json result = nlohmann::json::object();
    if (reference.handle.is_valid()) {
        result[std::string(prefix) + "_handle"] = reference.handle.to_string();
    }
    if (!reference.path.empty()) result[std::string(prefix) + "_path"] = reference.path;
    return result;
}

} // namespace

nlohmann::json LevelPaletteAsset::to_json() const {
    nlohmann::json document{
        {"version", version},
        {"type", LEVEL_PALETTE_TYPE},
        {"name", name},
        {"grid", {{"origin", {grid_origin.x, grid_origin.y}}}},
        {"categories", nlohmann::json::array()},
        {"entries", nlohmann::json::array()}
    };
    for (const LevelPaletteCategory& category : categories) {
        document["categories"].push_back({{"id", category.id}, {"label", category.label}});
    }
    for (const LevelPaletteEntry& entry : entries) {
        nlohmann::json serialized{
            {"id", entry.id},
            {"label", entry.label},
            {"category", entry.category},
            {"kind", entry.kind == LevelPaletteEntryKind::SpriteRegion ? "sprite_region" : "prefab"},
            {"placement", {
                {"snap", {entry.placement.snap.x, entry.placement.snap.y}},
                {"spacing", entry.placement.spacing},
                {"rotation_degrees", entry.placement.rotation_degrees},
                {"depth_z", entry.placement.depth_z},
                {"flip_x", entry.placement.flip_x},
                {"flip_y", entry.placement.flip_y},
                {"sorting_layer", entry.placement.sorting_layer},
                {"order_in_layer", entry.placement.order_in_layer},
                {"render_layer", entry.placement.render_layer}
            }}
        };
        if (entry.kind == LevelPaletteEntryKind::SpriteRegion) {
            serialized.update(reference_json(entry.sprite_region.texture, "texture"));
            serialized["rect_px"] = {
                entry.sprite_region.rect_px.x, entry.sprite_region.rect_px.y,
                entry.sprite_region.rect_px.z, entry.sprite_region.rect_px.w
            };
            serialized["world_size"] = {
                entry.sprite_region.world_size.x, entry.sprite_region.world_size.y
            };
            serialized["pivot"] = {entry.sprite_region.pivot.x, entry.sprite_region.pivot.y};
        } else {
            serialized.update(reference_json(entry.prefab.prefab, "prefab"));
        }
        document["entries"].push_back(std::move(serialized));
    }
    return document;
}

std::optional<LevelPaletteAsset> LevelPaletteAsset::from_json(
    const nlohmann::json& json,
    std::string* error,
    const AssetDatabase* database
) {
    if (error) error->clear();
    if (!json.is_object()) {
        set_error(error, "Level palette JSON must be an object");
        return std::nullopt;
    }
    bool current_version = false;
    if (json.contains("version")) {
        const auto& serialized_version = json.at("version");
        if (serialized_version.is_number_unsigned()) {
            current_version = serialized_version.get<std::uint64_t>() == CURRENT_VERSION;
        } else if (serialized_version.is_number_integer()) {
            current_version = serialized_version.get<std::int64_t>() == CURRENT_VERSION;
        }
    }
    if (!current_version) {
        set_error(error, "Level palette uses an unsupported or missing schema version");
        return std::nullopt;
    }
    if (!json.contains("type") || !json.at("type").is_string() ||
        json.at("type").get<std::string>() != LEVEL_PALETTE_TYPE) {
        set_error(error, "Level palette type must be 'Engine2D.LevelPalette'");
        return std::nullopt;
    }
    if (!json.contains("name") || !json.at("name").is_string() ||
        !json.contains("grid") || !json.at("grid").is_object() ||
        !json.at("grid").contains("origin") ||
        !json.contains("categories") || !json.at("categories").is_array() ||
        !json.contains("entries") || !json.at("entries").is_array()) {
        set_error(error, "Level palette is missing a required top-level field");
        return std::nullopt;
    }
    if (json.at("categories").size() > MAX_CATEGORIES ||
        json.at("entries").size() > MAX_ENTRIES) {
        set_error(error, "Level palette exceeds the supported category or entry count");
        return std::nullopt;
    }

    LevelPaletteAsset asset;
    try {
        asset.version = CURRENT_VERSION;
        asset.name = json.at("name").get<std::string>();
        if (!parse_vec2(json.at("grid").at("origin"), asset.grid_origin, "Level palette grid origin", error)) {
            return std::nullopt;
        }

        asset.categories.reserve(json.at("categories").size());
        for (const auto& serialized_category : json.at("categories")) {
            if (!serialized_category.is_object() ||
                !serialized_category.contains("id") || !serialized_category.at("id").is_string() ||
                !serialized_category.contains("label") || !serialized_category.at("label").is_string()) {
                set_error(error, "Every level palette category must contain string id and label fields");
                return std::nullopt;
            }
            asset.categories.push_back({
                serialized_category.at("id").get<std::string>(),
                serialized_category.at("label").get<std::string>()
            });
        }

        asset.entries.reserve(json.at("entries").size());
        for (const auto& serialized_entry : json.at("entries")) {
            if (!serialized_entry.is_object()) {
                set_error(error, "Every level palette entry must be an object");
                return std::nullopt;
            }
            for (const char* field : {"id", "label", "category", "kind", "placement"}) {
                if (!serialized_entry.contains(field)) {
                    set_error(error, std::string("Level palette entry is missing '") + field + "'");
                    return std::nullopt;
                }
            }
            if (!serialized_entry.at("id").is_string() ||
                !serialized_entry.at("label").is_string() ||
                !serialized_entry.at("category").is_string() ||
                !serialized_entry.at("kind").is_string()) {
                set_error(error, "Level palette entry identity fields must be strings");
                return std::nullopt;
            }

            LevelPaletteEntry entry;
            entry.id = serialized_entry.at("id").get<std::string>();
            entry.label = serialized_entry.at("label").get<std::string>();
            entry.category = serialized_entry.at("category").get<std::string>();
            const std::string kind = serialized_entry.at("kind").get<std::string>();
            if (kind == "sprite_region") {
                entry.kind = LevelPaletteEntryKind::SpriteRegion;
                if (!parse_asset_reference(serialized_entry, "texture", entry.sprite_region.texture, error) ||
                    !serialized_entry.contains("rect_px") ||
                    !parse_ivec4(serialized_entry.at("rect_px"), entry.sprite_region.rect_px, "Entry rect_px", error) ||
                    !serialized_entry.contains("world_size") ||
                    !parse_vec2(serialized_entry.at("world_size"), entry.sprite_region.world_size, "Entry world_size", error) ||
                    !serialized_entry.contains("pivot") ||
                    !parse_vec2(serialized_entry.at("pivot"), entry.sprite_region.pivot, "Entry pivot", error)) {
                    return std::nullopt;
                }
            } else if (kind == "prefab") {
                entry.kind = LevelPaletteEntryKind::Prefab;
                if (!parse_asset_reference(serialized_entry, "prefab", entry.prefab.prefab, error)) {
                    return std::nullopt;
                }
            } else {
                set_error(error, "Level palette entry kind must be 'sprite_region' or 'prefab'");
                return std::nullopt;
            }
            if (!parse_placement(serialized_entry.at("placement"), entry.placement, error)) {
                return std::nullopt;
            }
            asset.entries.push_back(std::move(entry));
        }
    } catch (const nlohmann::json::exception& exception) {
        set_error(error, std::string("Invalid level palette field: ") + exception.what());
        return std::nullopt;
    }

    if (database && !asset.canonicalize_references(*database, error)) return std::nullopt;
    if (!asset.validate(error, database)) return std::nullopt;
    return asset;
}

std::optional<LevelPaletteAsset> LevelPaletteAsset::load_from_file(
    const std::filesystem::path& path,
    std::string* error,
    const AssetDatabase* database
) {
    if (error) error->clear();
    if (lowercase(path.extension().string()) != ".levelpalette") {
        set_error(error, "Level palette assets must use the .levelpalette extension");
        return std::nullopt;
    }
    const std::string text = FileSystem::read_text(path.string());
    if (text.empty()) {
        set_error(error, "Failed to read level palette or file is empty: " + path.string());
        return std::nullopt;
    }
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded()) {
        set_error(error, "Failed parsing level palette JSON: " + path.string());
        return std::nullopt;
    }
    return from_json(json, error, database);
}

bool LevelPaletteAsset::save_to_file(
    const std::filesystem::path& path,
    std::string* error,
    const AssetDatabase* database
) const {
    if (error) error->clear();
    if (lowercase(path.extension().string()) != ".levelpalette") {
        return fail(error, "Level palette assets must use the .levelpalette extension");
    }
    LevelPaletteAsset canonical = *this;
    if (database && !canonical.canonicalize_references(*database, error)) return false;
    if (!canonical.validate(error, database)) return false;
    const std::string serialized = canonical.to_json().dump(2) + '\n';
    if (!FileSystem::write_text_atomically(path.string(), serialized)) {
        return fail(error, "Failed writing level palette atomically to: " + path.string());
    }
    return true;
}

bool LevelPaletteAsset::validate(std::string* error, const AssetDatabase* database) const {
    if (error) error->clear();
    if (version != CURRENT_VERSION) return fail(error, "Level palette schema version is unsupported");
    if (name.empty() || name.size() > 128) {
        return fail(error, "Level palette name must contain between 1 and 128 characters");
    }
    if (!finite(grid_origin)) return fail(error, "Level palette grid origin must be finite");
    if (categories.empty() || categories.size() > MAX_CATEGORIES) {
        return fail(error, "Level palette must contain between 1 and 256 categories");
    }
    if (entries.size() > MAX_ENTRIES) {
        return fail(error, "Level palette cannot contain more than 4096 entries");
    }

    std::unordered_set<std::string> category_ids;
    for (const LevelPaletteCategory& category : categories) {
        if (!valid_identifier(category.id)) {
            return fail(error, "Level palette category id is invalid: " + category.id);
        }
        if (category.label.empty() || category.label.size() > 128) {
            return fail(error, "Level palette category label must contain between 1 and 128 characters");
        }
        if (!category_ids.insert(category.id).second) {
            return fail(error, "Level palette category id is duplicated: " + category.id);
        }
    }

    std::unordered_set<std::string> entry_ids;
    for (const LevelPaletteEntry& entry : entries) {
        if (!valid_identifier(entry.id)) {
            return fail(error, "Level palette entry id is invalid: " + entry.id);
        }
        if (!entry_ids.insert(entry.id).second) {
            return fail(error, "Level palette entry id is duplicated: " + entry.id);
        }
        if (entry.label.empty() || entry.label.size() > 128) {
            return fail(error, "Level palette entry label must contain between 1 and 128 characters");
        }
        if (!category_ids.contains(entry.category)) {
            return fail(error, "Level palette entry references an unknown category: " + entry.category);
        }

        const LevelPalettePlacement& placement = entry.placement;
        if (!finite(placement.snap) || placement.snap.x < 0.0f || placement.snap.y < 0.0f ||
            placement.snap.x > MAX_WORLD_VALUE || placement.snap.y > MAX_WORLD_VALUE ||
            !std::isfinite(placement.spacing) || placement.spacing <= 0.0f ||
            placement.spacing > MAX_WORLD_VALUE || !std::isfinite(placement.rotation_degrees) ||
            !std::isfinite(placement.depth_z) || std::abs(placement.depth_z) > MAX_WORLD_VALUE ||
            placement.render_layer > 31) {
            return fail(error, "Level palette entry placement contains invalid values: " + entry.id);
        }

        if (entry.kind == LevelPaletteEntryKind::SpriteRegion) {
            const glm::ivec4 rect = entry.sprite_region.rect_px;
            const std::int64_t right = static_cast<std::int64_t>(rect.x) + rect.z;
            const std::int64_t bottom = static_cast<std::int64_t>(rect.y) + rect.w;
            if (rect.x < 0 || rect.y < 0 || rect.z <= 0 || rect.w <= 0 ||
                right > std::numeric_limits<int>::max() || bottom > std::numeric_limits<int>::max()) {
                return fail(error, "Level palette sprite rect is invalid: " + entry.id);
            }
            if (!finite(entry.sprite_region.world_size) ||
                entry.sprite_region.world_size.x <= 0.0f || entry.sprite_region.world_size.y <= 0.0f ||
                entry.sprite_region.world_size.x > MAX_WORLD_VALUE ||
                entry.sprite_region.world_size.y > MAX_WORLD_VALUE) {
                return fail(error, "Level palette sprite world_size is invalid: " + entry.id);
            }
            if (!finite(entry.sprite_region.pivot) ||
                entry.sprite_region.pivot.x < 0.0f || entry.sprite_region.pivot.x > 1.0f ||
                entry.sprite_region.pivot.y < 0.0f || entry.sprite_region.pivot.y > 1.0f) {
                return fail(error, "Level palette sprite pivot must be finite and between 0 and 1: " + entry.id);
            }
            if (!validate_reference(
                    entry.sprite_region.texture, AssetType::Texture,
                    "Level palette sprite texture", database, error
                )) {
                return false;
            }
        } else if (entry.kind == LevelPaletteEntryKind::Prefab) {
            if (!validate_reference(
                    entry.prefab.prefab, AssetType::Prefab,
                    "Level palette prefab", database, error
                )) {
                return false;
            }
        } else {
            return fail(error, "Level palette entry has an unknown kind: " + entry.id);
        }
    }
    return true;
}

bool LevelPaletteAsset::canonicalize_references(
    const AssetDatabase& database,
    std::string* error
) {
    if (error) error->clear();
    LevelPaletteAsset canonical = *this;
    for (LevelPaletteEntry& entry : canonical.entries) {
        if (entry.kind == LevelPaletteEntryKind::SpriteRegion) {
            if (!canonicalize_reference(
                    entry.sprite_region.texture, AssetType::Texture,
                    "Level palette sprite texture", database, error
                )) {
                return false;
            }
        } else if (entry.kind == LevelPaletteEntryKind::Prefab) {
            if (!canonicalize_reference(
                    entry.prefab.prefab, AssetType::Prefab,
                    "Level palette prefab", database, error
                )) {
                return false;
            }
        } else {
            return fail(error, "Level palette entry has an unknown kind: " + entry.id);
        }
    }
    *this = std::move(canonical);
    return true;
}

const LevelPaletteEntry* LevelPaletteAsset::find_entry(std::string_view id) const {
    const auto found = std::find_if(entries.begin(), entries.end(), [id](const auto& entry) {
        return entry.id == id;
    });
    return found == entries.end() ? nullptr : &*found;
}

} // namespace Engine
