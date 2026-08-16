#pragma once

#include "../panels/TilemapPalettePanel.hpp"
#include "../../scene/Scene.hpp"
#include "../../scene/Camera2D.hpp"
#include <imgui.h>
#include <unordered_map>
#include <cstdint>

namespace Engine {

class EditorCommandHistory;

class TilemapPaintingTool {
public:
    TilemapPaintingTool() = default;

    // Handles interactive tilemap painting in Viewport (Ghost Preview + Brush / Box Fill / Eraser + Undo/Redo)
    void handle_viewport_painting(
        Scene* active_scene,
        EntityID active_tilemap_entity,
        const TilemapPalettePanel& palette_panel,
        const Camera2D& camera,
        const ImVec2& img_min,
        const ImVec2& img_size,
        bool is_viewport_hovered,
        EditorCommandHistory* command_history = nullptr
    );

private:
    bool m_is_box_filling{false};
    bool m_was_painting{false};
    glm::ivec2 m_box_fill_start{0, 0};
    std::unordered_map<int64_t, uint16_t> m_stroke_old_tiles;
    std::unordered_map<int64_t, uint16_t> m_stroke_new_tiles;
};

} // namespace Engine
