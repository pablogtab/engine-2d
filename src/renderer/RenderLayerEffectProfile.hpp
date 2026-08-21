#pragma once

#include "RenderLayer.hpp"
#include "../core/AssetHandle.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Engine {

class AssetDatabase;
class ILocalEffectPass;
class IMaterialBackend;

struct RenderLayerEffectPassDescription {
    AssetHandle material_handle{};
    bool enabled{true};
    bool operator==(const RenderLayerEffectPassDescription&) const = default;
};

struct RenderLayerEffectAssignment {
    AssetHandle profile_handle{};
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};
    bool enabled{true};
    bool operator==(const RenderLayerEffectAssignment&) const = default;
};

struct RenderLayerEffectProfile {
    std::string name{"Layer Effects"};
    std::vector<RenderLayerEffectPassDescription> passes;

    bool operator==(const RenderLayerEffectProfile&) const = default;
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] bool from_json(const std::string& json_text, std::string* error = nullptr);
    [[nodiscard]] bool save_asset(const std::string& filepath, AssetDatabase& database, std::string* error = nullptr) const;
    [[nodiscard]] static std::optional<RenderLayerEffectProfile> load_asset(
        AssetHandle handle, const AssetDatabase& database, std::string* error = nullptr
    );
    [[nodiscard]] static std::optional<RenderLayerEffectProfile> load_asset_for_authoring(
        AssetHandle handle, const AssetDatabase& database, std::string* error = nullptr
    );
    [[nodiscard]] std::vector<std::shared_ptr<ILocalEffectPass>> build_runtime_passes(
        const AssetDatabase& database,
        const std::shared_ptr<IMaterialBackend>& backend,
        std::string* error = nullptr
    ) const;
};

} // namespace Engine
