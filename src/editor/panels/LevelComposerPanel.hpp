#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../../core/LevelPaletteAsset.hpp"
#include "../../scene/SceneManager.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct SDL_Renderer;

namespace Engine {

struct AssetRecord;
class Texture;

// Project-owned palette editor for assembling 2D levels from rectangular atlas
// regions and reusable prefabs. Runtime scenes only receive ordinary Sprite or
// prefab entities; the composer itself remains editor-only.
class LevelComposerPanel final : public EditorPanel {
public:
    LevelComposerPanel(
        SDL_Renderer* renderer,
        SceneManager* scene_manager,
        std::shared_ptr<EditorSelection> selection
    );

    void on_gui() override;
    void on_context_changed(const EditorPanelContext& context) override;
    void on_render_resources_invalidated() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    [[nodiscard]] bool is_placement_active() const;
    [[nodiscard]] const LevelPaletteEntry* active_entry() const;
    [[nodiscard]] const LevelPaletteAsset& palette() const { return m_palette; }
    [[nodiscard]] glm::vec2 grid_origin() const { return m_palette.grid_origin; }
    [[nodiscard]] EntityID target_parent() const { return m_target_parent; }

    void stop_placement() { m_placement_active = false; }
    void rotate_active(float degrees);
    void toggle_active_flip_x();
    void toggle_active_flip_y();
    void open_palette(const std::filesystem::path& path);
    void report_tool_status(std::string message, bool is_error = false);
    void set_on_open_prefab_callback(std::function<void(const std::string&)> callback) {
        m_on_open_prefab = std::move(callback);
    }
    [[nodiscard]] bool has_unsaved_changes() const { return m_palette_dirty; }
    [[nodiscard]] bool save_unsaved_changes() { return save_palette(); }
    [[nodiscard]] bool discard_unsaved_changes();
    [[nodiscard]] std::string current_palette_label() const;
    [[nodiscard]] std::vector<std::string> live_asset_usages(AssetHandle handle) const;

private:
    void ensure_default_categories();
    void render_palette_toolbar();
    void render_entry_list();
    void render_entry_editor(LevelPaletteEntry& entry);
    void render_sprite_region_editor(LevelPaletteEntry& entry);
    void render_atlas_canvas(LevelPaletteEntry& entry, class Texture& texture);
    void render_parent_target();
    void render_pending_load_modal();
    void accept_asset_drop();

    void add_texture_entry(const AssetRecord& record);
    void add_prefab_entry(const AssetRecord& record);
    void duplicate_selected_entry();
    void remove_selected_entry();
    void select_entry(std::size_t index);
    void sync_entry_buffers();
    void apply_entry_buffers(LevelPaletteEntry& entry);

    [[nodiscard]] std::filesystem::path resolve_palette_path() const;
    [[nodiscard]] std::optional<std::filesystem::path> current_palette_path() const;
    bool load_palette(const std::filesystem::path& path);
    bool save_palette();
    bool save_palette_to(const std::filesystem::path& path);
    bool save_palette_as_requested();
    [[nodiscard]] bool is_project_palette_path(const std::filesystem::path& path) const;
    [[nodiscard]] std::string unique_entry_id(std::string base) const;
    [[nodiscard]] Texture* resolve_texture(const LevelPaletteEntry& entry) const;
    [[nodiscard]] bool is_entry_asset_ready(const LevelPaletteEntry& entry) const;

    std::string m_name{"Level Composer"};
    SDL_Renderer* m_renderer{nullptr};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store;

    LevelPaletteAsset m_palette;
    LevelPaletteAsset m_clean_palette;
    std::optional<std::size_t> m_selected_entry;
    EntityID m_target_parent{0};
    bool m_placement_active{false};
    bool m_palette_dirty{false};
    AssetHandle m_current_palette_handle{};
    std::string m_status;
    bool m_status_is_error{false};
    std::optional<std::filesystem::path> m_pending_load_path;
    bool m_open_pending_load_modal{false};
    std::function<void(const std::string&)> m_on_open_prefab;

    std::array<char, 256> m_palette_path_buf{};
    std::array<char, 129> m_palette_name_buf{};
    std::array<char, 128> m_entry_label_buf{};
    std::array<char, 128> m_entry_id_buf{};
    std::array<char, 128> m_entry_category_buf{};

    glm::ivec2 m_slice_cell{256, 256};
    glm::ivec2 m_slice_spacing{0, 0};
    glm::ivec2 m_slice_margin{0, 0};
    bool m_slice_to_grid{true};
    bool m_slice_dragging{false};
    glm::vec2 m_slice_drag_start{0.0f, 0.0f};
    float m_atlas_zoom{1.0f};
    bool m_auto_size_from_region{true};
};

} // namespace Engine
