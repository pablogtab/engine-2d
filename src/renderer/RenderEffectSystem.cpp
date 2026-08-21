#include "RenderEffectSystem.hpp"
#include "Renderer2D.hpp"
#include "LocalEffectPass.hpp"
#include "../core/AssetDatabase.hpp"
#include "../core/Log.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include <algorithm>

namespace Engine {

bool RenderEffectSystem::set_layer_assignments(
    std::vector<RenderLayerEffectAssignment> assignments,
    const AssetDatabase& database
) {
    std::vector<RenderLayerEffectProfile> profiles;
    profiles.reserve(assignments.size());
    std::array<bool, 32> occupied{};
    for (const auto& assignment : assignments) {
        if (assignment.render_layer > 31 || occupied[assignment.render_layer]) return false;
        const auto profile = RenderLayerEffectProfile::load_asset(assignment.profile_handle, database);
        if (!profile) return false;
        occupied[assignment.render_layer] = true;
        profiles.push_back(*profile);
    }
    m_layer_assignments = std::move(assignments);
    m_layer_profiles = std::move(profiles);
    m_layers_dirty = true;
    return true;
}

bool RenderEffectSystem::refresh(const AssetDatabase& database) {
    m_entity_runtime_passes.clear();
    m_unavailable_entity_profiles.clear();
    m_entity_backend.reset();
    m_entities_dirty = true;

    const auto authored_assignments = m_layer_assignments;
    if (set_layer_assignments(authored_assignments, database)) return true;
    m_layer_profiles.clear();
    for (auto& passes : m_layer_runtime_passes) passes.clear();
    m_layer_backend.reset();
    m_layers_dirty = false;
    return false;
}

void RenderEffectSystem::configure(
    EntityManager& entities,
    Renderer2D& renderer,
    const AssetDatabase& database
) {
    const auto& material_backend = renderer.get_material_backend();
    if (m_layers_dirty || material_backend != m_layer_backend) {
        for (auto& passes : m_layer_runtime_passes) passes.clear();
        for (std::size_t index = 0; index < m_layer_profiles.size(); ++index) {
            const auto& assignment = m_layer_assignments[index];
            if (!assignment.enabled) continue;
            const auto& profile = m_layer_profiles[index];
            std::string error;
            auto passes = profile.build_runtime_passes(database, material_backend, &error);
            const bool has_enabled_pass = std::any_of(profile.passes.begin(), profile.passes.end(), [](const auto& pass) {
                return pass.enabled;
            });
            if (has_enabled_pass && passes.empty()) {
                ENGINE_LOG_WARN("[LAYER EFFECT] Profile '{}' unavailable: {}", profile.name, error);
                continue;
            }
            m_layer_runtime_passes[assignment.render_layer] = std::move(passes);
        }
        m_layer_backend = material_backend;
        m_layers_dirty = false;
    }

    if (m_entities_dirty || material_backend != m_entity_backend) {
        m_entity_runtime_passes.clear();
        m_unavailable_entity_profiles.clear();
        m_entity_backend = material_backend;
        m_entities_dirty = false;
    }
    entities.for_each<RenderEffectComponent>([&](EntityID, const RenderEffectComponent& effect) {
        if (!effect.enabled || !effect.profile_handle.is_valid() ||
            m_entity_runtime_passes.contains(effect.profile_handle) ||
            m_unavailable_entity_profiles.contains(effect.profile_handle)) {
            return;
        }
        std::string error;
        const auto profile = RenderLayerEffectProfile::load_asset(effect.profile_handle, database, &error);
        if (!profile) {
            m_unavailable_entity_profiles.insert(effect.profile_handle);
            ENGINE_LOG_WARN("[ENTITY EFFECT] Profile '{}' unavailable: {}", effect.profile_handle.to_string(), error);
            return;
        }
        auto passes = profile->build_runtime_passes(database, material_backend, &error);
        const bool has_enabled_pass = std::any_of(profile->passes.begin(), profile->passes.end(), [](const auto& pass) {
            return pass.enabled;
        });
        if (has_enabled_pass && passes.empty()) {
            m_unavailable_entity_profiles.insert(effect.profile_handle);
            ENGINE_LOG_WARN("[ENTITY EFFECT] Profile '{}' unavailable: {}", profile->name, error);
            return;
        }
        m_entity_runtime_passes.emplace(effect.profile_handle, std::move(passes));
    });

    for (const auto& [profile_handle, passes] : m_entity_runtime_passes) {
        renderer.set_entity_effect_profile(profile_handle, passes);
    }
    for (RenderLayer layer = 0; layer < m_layer_runtime_passes.size(); ++layer) {
        if (!m_layer_runtime_passes[layer].empty()) {
            renderer.set_render_layer_effects(layer, m_layer_runtime_passes[layer]);
        }
    }
}

} // namespace Engine
