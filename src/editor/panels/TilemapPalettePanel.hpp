#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../../tilemap/Tilemap.hpp"
#include <string>
#include <vector>
#include <memory>

struct SDL_Renderer;

namespace Engine {

class SceneManager;
class EditorCommandHistory;

enum class TileToolMode {
    Brush = 0,
    RandomBrush,
    BoxFill,
    Eraser
};

struct TileVariant {
    uint16_t tile_id{1};
    float weight{100.0f}; // Relative weight (e.g. 70, 20, 10)
};

class TilemapPalettePanel : public EditorPanel {
public:
    explicit TilemapPalettePanel(
        SDL_Renderer* renderer = nullptr,
        SceneManager* scene_manager = nullptr,
        std::shared_ptr<EditorSelection> selection = nullptr
    );
    ~TilemapPalettePanel() override = default;

    void on_gui() override;
    void on_context_changed(const EditorPanelContext& context) override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    [[nodiscard]] uint16_t get_active_tile_id() const { return m_active_tile_id; }
    [[nodiscard]] uint16_t get_random_tile_id() const;
    [[nodiscard]] TileToolMode get_tool_mode() const { return m_tool_mode; }
    [[nodiscard]] const std::string& get_custom_tileset_path() const { return m_tileset_path; }
    [[nodiscard]] bool has_selected_tilemap() const;
    [[nodiscard]] EntityID get_selected_tilemap_entity() const;
    [[nodiscard]] bool is_painting_active() const { return is_open && m_is_painting_active && has_selected_tilemap(); }
    [[nodiscard]] const std::vector<TileVariant>& get_random_pool() const { return m_random_pool; }
    [[nodiscard]] bool use_random_box_fill() const { return m_random_box_fill; }

    void add_to_random_pool(uint16_t tile_id, float weight = 100.0f);
    void remove_from_random_pool(uint16_t tile_id);
    [[nodiscard]] bool is_in_random_pool(uint16_t tile_id) const;

    void set_renderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void set_scene_manager(SceneManager* sm) { m_scene_manager = sm; }
    void set_selection(std::shared_ptr<EditorSelection> sel) { m_selection = sel; }

private:
    std::string m_name{"Tilemap Palette"};
    SDL_Renderer* m_renderer{nullptr};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    
    bool m_is_painting_active{true};
    TileToolMode m_tool_mode{TileToolMode::Brush};
    uint16_t m_active_tile_id{1}; // 1 = Grass Top default
    int m_tile_pixel_size{32};
    float m_palette_cell_size{48.0f}; // Zoomable preview cell size
    bool m_random_box_fill{false};   // In BoxFill mode, use random variants only when requested

    std::string m_tileset_path{"Assets/Textures/tilesets/terrain_tileset.png"};
    char m_tileset_input_buf[256]{"Assets/Textures/tilesets/terrain_tileset.png"};
    EntityID m_bound_tilemap_entity{0};
    bool m_tileset_input_active{false};
    bool m_pixel_size_input_active{false};

    // Multi-Variant Random Tile Pool
    std::vector<TileVariant> m_random_pool;

    void sync_from_selected_tilemap(bool force = false);
    bool apply_selected_tileset_settings(const std::string& path, int tile_pixel_size);
};

} // namespace Engine
