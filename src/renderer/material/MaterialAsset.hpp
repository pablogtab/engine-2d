#pragma once

#include "../../core/AssetHandle.hpp"
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Engine {
class AssetDatabase;
class Material;
class EntityManager;

struct MaterialAssetDescription {
    std::string name;
    AssetHandle shader_handle{};
    std::vector<std::vector<std::byte>> uniforms;
};

struct MaterialAssetRefreshResult {
    std::size_t reloaded{0};
    std::size_t unavailable{0};
};

class MaterialAsset {
public:
    [[nodiscard]] static std::shared_ptr<const Material> load(AssetHandle handle, const AssetDatabase& database);
    [[nodiscard]] static std::shared_ptr<const Material> instantiate(
        const MaterialAssetDescription& description,
        const AssetDatabase& database,
        std::string* error = nullptr
    );
    [[nodiscard]] static std::optional<MaterialAssetDescription> read(
        AssetHandle handle, const AssetDatabase& database, std::string* error = nullptr
    );
    [[nodiscard]] static std::optional<std::string> serialize(
        const MaterialAssetDescription& description,
        const AssetDatabase& database,
        std::string* error = nullptr
    );
    [[nodiscard]] static bool save(
        AssetHandle handle,
        const MaterialAssetDescription& description,
        AssetDatabase& database,
        std::string* error = nullptr
    );
    [[nodiscard]] static MaterialAssetRefreshResult refresh_assigned(
        EntityManager& entities, const AssetDatabase& database
    );
    [[nodiscard]] static std::size_t preview_assigned(
        EntityManager& entities,
        AssetHandle handle,
        std::shared_ptr<const Material> material
    );
};
} // namespace Engine
