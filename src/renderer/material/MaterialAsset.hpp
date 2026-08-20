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

struct MaterialAssetDescription {
    std::string name;
    AssetHandle shader_handle{};
    std::vector<std::vector<std::byte>> uniforms;
};

class MaterialAsset {
public:
    [[nodiscard]] static std::shared_ptr<const Material> load(AssetHandle handle, const AssetDatabase& database);
    [[nodiscard]] static std::optional<MaterialAssetDescription> read(
        AssetHandle handle, const AssetDatabase& database, std::string* error = nullptr
    );
    [[nodiscard]] static bool save(
        AssetHandle handle,
        const MaterialAssetDescription& description,
        AssetDatabase& database,
        std::string* error = nullptr
    );
};
} // namespace Engine
