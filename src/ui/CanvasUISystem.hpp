#pragma once

#include "CanvasLayout.hpp"
#include "UIInputSystem.hpp"
#include "../core/AssetHandle.hpp"
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Engine {

class AssetDatabase;
class EntityManager;
class Font;
class IEntityRenderFilter;
class Renderer2D;
class Camera2D;

struct CanvasUIRenderStats {
    std::size_t canvases{0};
    std::size_t images{0};
    std::size_t button_backgrounds{0};
    std::size_t text_lines{0};
    std::size_t editor_frames{0};
};

class CanvasUISystem {
public:
    CanvasUISystem();
    ~CanvasUISystem();

    CanvasUIRenderStats render(
        EntityManager& entities,
        Renderer2D& renderer,
        const AssetDatabase& database,
        const glm::vec2& output_size,
        const SafeAreaInsets& safe_area = {},
        const UIInputSystem* input = nullptr,
        const IEntityRenderFilter* visibility_filter = nullptr
    );

    CanvasUIRenderStats render_editor_preview(
        EntityManager& entities,
        Renderer2D& renderer,
        const AssetDatabase& database,
        const Camera2D& editor_camera,
        const IEntityRenderFilter* visibility_filter = nullptr
    );

    void clear();

private:
    void render_canvas_contents(
        EntityManager& entities,
        Renderer2D& renderer,
        const AssetDatabase& database,
        EntityID canvas_entity,
        const CanvasViewport& viewport,
        float z_layer,
        const UIInputSystem* input,
        const IEntityRenderFilter* visibility_filter,
        bool draw_editor_frame,
        CanvasUIRenderStats& stats
    );
    Font* resolve_font(SDL_Renderer* renderer, AssetHandle handle, const AssetDatabase& database);

    std::unordered_map<AssetHandle, std::unique_ptr<Font>> m_fonts;
    std::unordered_set<AssetHandle> m_failed_fonts;
    std::unique_ptr<Font> m_default_font;
};

} // namespace Engine
