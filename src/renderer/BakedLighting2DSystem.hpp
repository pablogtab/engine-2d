#pragma once

#include "LightingBakeService.hpp"
#include "LightingPass.hpp"
#include "Texture.hpp"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

class Renderer2D;

class BakedLighting2DPass final : public ILightingPass {
public:
    [[nodiscard]] const char* name() const override { return "BakedLighting2D"; }
    [[nodiscard]] bool is_enabled() const override { return !m_tiles.empty(); }
    [[nodiscard]] RenderLayerMask affected_layers() const override { return m_affected_layers; }
    [[nodiscard]] LightingCompositionPhase composition_phase() const override {
        return LightingCompositionPhase::BakedMultiply;
    }
    [[nodiscard]] bool requires_baked_receiver_mask() const override { return true; }
    bool apply(const LightingContext& context) override;
    void invalidate_resources() override { clear(); }

    bool load(const LightingBakeManifest& manifest, SDL_Renderer* renderer);
    void clear();
    [[nodiscard]] const std::string& loaded_hash() const { return m_loaded_hash; }
    [[nodiscard]] bool using_stale_bake() const { return m_using_stale_bake; }

private:
    struct RuntimeTile {
        std::unique_ptr<Texture> texture;
        LightingBakeTile metadata;
    };
    std::vector<RuntimeTile> m_tiles;
    RenderLayerMask m_affected_layers{0};
    std::string m_loaded_hash;
    bool m_using_stale_bake{false};
};

class BakedLighting2DSystem {
public:
    BakedLighting2DSystem();
    void configure(
        const std::filesystem::path& project_root,
        const std::filesystem::path& scene_path,
        Renderer2D& renderer
    );
    void force_refresh() { m_next_poll = {}; }
    [[nodiscard]] const std::shared_ptr<BakedLighting2DPass>& runtime_pass() const { return m_runtime_pass; }

private:
    std::shared_ptr<BakedLighting2DPass> m_runtime_pass;
    std::filesystem::path m_scene_path;
    std::chrono::steady_clock::time_point m_next_poll{};
};

} // namespace Engine
