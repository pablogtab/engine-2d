#pragma once

#include "../../core/AssetHandle.hpp"
#include <memory>

namespace Engine {
class AssetDatabase;
class Material;

class MaterialAsset {
public:
    [[nodiscard]] static std::shared_ptr<const Material> load(AssetHandle handle, const AssetDatabase& database);
};
} // namespace Engine
