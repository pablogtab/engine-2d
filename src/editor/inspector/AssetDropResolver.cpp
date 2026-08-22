#include "AssetDropResolver.hpp"

#include <algorithm>

namespace Engine {

AssetDropResolution resolve_asset_drop(
    const AssetDatabase& database,
    const std::filesystem::path& source_path,
    std::span<const AssetType> accepted_types
) {
    if (source_path.empty()) return {AssetDropStatus::EmptyPath, nullptr};

    const AssetRecord* record = database.find_by_path(source_path);
    if (!record) return {AssetDropStatus::NotFound, nullptr};
    if (record->status != AssetStatus::Ready) {
        return {AssetDropStatus::NotReady, record};
    }

    const bool accepts_any_known_type = accepted_types.empty() &&
        record->type != AssetType::Unknown;
    const bool accepts_explicit_type = std::find(
        accepted_types.begin(), accepted_types.end(), record->type
    ) != accepted_types.end();
    if (!accepts_any_known_type && !accepts_explicit_type) {
        return {AssetDropStatus::IncompatibleType, record};
    }
    return {AssetDropStatus::Accepted, record};
}

const char* asset_drop_status_message(AssetDropStatus status) {
    switch (status) {
        case AssetDropStatus::Accepted: return "Release to assign asset";
        case AssetDropStatus::EmptyPath: return "Invalid asset payload";
        case AssetDropStatus::NotFound: return "Asset is not registered in this project";
        case AssetDropStatus::NotReady: return "Asset is not ready";
        case AssetDropStatus::IncompatibleType: return "Asset type is not compatible with this field";
    }
    return "Asset cannot be assigned";
}

} // namespace Engine
