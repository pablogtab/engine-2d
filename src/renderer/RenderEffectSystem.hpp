#pragma once

#include "RenderLayerEffectProfile.hpp"
#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

class AssetDatabase;
class EntityManager;
class ILocalEffectPass;
class IMaterialBackend;
class Renderer2D;

// Resolves authored effect assignments into cached runtime passes. Scenes own
// only this reusable system and their ECS data; no effect recipe lives here.
class RenderEffectSystem {
public:
    [[nodiscard]] const std::vector<RenderLayerEffectAssignment>& layer_assignments() const {
        return m_layer_assignments;
    }
    bool set_layer_assignments(
        std::vector<RenderLayerEffectAssignment> assignments,
        const AssetDatabase& database
    );
    bool refresh(const AssetDatabase& database);
    void configure(EntityManager& entities, Renderer2D& renderer, const AssetDatabase& database);

private:
    std::vector<RenderLayerEffectAssignment> m_layer_assignments;
    std::vector<RenderLayerEffectProfile> m_layer_profiles;
    std::array<std::vector<std::shared_ptr<ILocalEffectPass>>, 32> m_layer_runtime_passes;
    std::shared_ptr<IMaterialBackend> m_layer_backend;
    bool m_layers_dirty{true};

    std::unordered_map<AssetHandle, std::vector<std::shared_ptr<ILocalEffectPass>>> m_entity_runtime_passes;
    std::unordered_set<AssetHandle> m_unavailable_entity_profiles;
    std::shared_ptr<IMaterialBackend> m_entity_backend;
    bool m_entities_dirty{true};
};

} // namespace Engine
