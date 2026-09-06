#pragma once

#include "../EditorPanel.hpp"
#include "../../core/AssetHandle.hpp"
#include "../../renderer/RenderLayerEffectProfile.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../tools/AssetBrowserQuery.hpp"
#include "../tools/AssetBrowserState.hpp"
#include <array>
#include <string>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_set>
#include <unordered_map>

struct SDL_Renderer;
struct SDL_Texture;

namespace Engine {

class AssetDatabase;
class EditorCommandHistory;
class Material;
class IMaterialBackend;
class Renderer2D;
class Texture;

class AssetBrowserPanel : public EditorPanel {
public:
    explicit AssetBrowserPanel(AssetDatabase* asset_database, std::string root_path = "Assets");
    ~AssetBrowserPanel() override;

    void on_gui() override;
    void on_render_resources_invalidated() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_on_load_scene_callback(std::function<void(const std::string&)> cb) {
        m_on_load_scene = std::move(cb);
    }
    void set_on_open_prefab_callback(std::function<void(const std::string&)> cb) {
        m_on_open_prefab = std::move(cb);
    }
    void set_on_open_level_palette_callback(std::function<void(const std::string&)> cb) {
        m_on_open_level_palette = std::move(cb);
    }
    void set_on_open_particle_effect(std::function<void(AssetHandle)> callback) {
        m_on_open_particle_effect = std::move(callback);
    }
    void set_on_open_vfx_preview(std::function<void(AssetHandle)> callback) {
        m_on_open_vfx_preview = std::move(callback);
    }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) {
        m_command_history = std::move(command_history);
    }
    void set_renderer(SDL_Renderer* renderer);
    void set_material_backend(std::shared_ptr<IMaterialBackend> backend);

    void refresh();
    void reveal_asset_path(const std::string& path);
    [[nodiscard]] AssetHandle create_material_asset(std::string base_name = "NewMaterial");
    [[nodiscard]] AssetHandle duplicate_material_asset(AssetHandle source_handle);
    void open_material_asset(AssetHandle handle);
    [[nodiscard]] SDL_Texture* preview_material_asset(AssetHandle handle);
    [[nodiscard]] bool is_focused() const { return m_is_focused; }
    [[nodiscard]] std::vector<std::filesystem::path> selected_assets_for_context() const {
        return selected_asset_paths();
    }
    void set_on_material_preview(
        std::function<void(AssetHandle, std::shared_ptr<const Material>)> callback
    ) { m_on_material_preview = std::move(callback); }
    void set_on_material_preview_reset(std::function<void()> callback) {
        m_on_material_preview_reset = std::move(callback);
    }
    void set_live_asset_usage_provider(
        std::function<std::vector<std::string>(AssetHandle)> provider
    ) {
        m_live_asset_usage_provider = std::move(provider);
    }

private:
    void navigate_to(const std::filesystem::path& directory);
    void activate_entry(const AssetBrowserEntry& entry);
    void draw_entry(const AssetBrowserEntry& entry);
    void draw_entry_interactions(const AssetBrowserEntry& entry);
    void draw_list_entry(const AssetBrowserEntry& entry);
    void draw_details_entries(const std::vector<AssetBrowserEntry>& entries);
    void draw_folder_tree();
    void draw_folder_tree_node(const std::filesystem::path& directory);
    void draw_folder_drop_target(const std::filesystem::path& directory);
    void draw_toolbar();
    void draw_selected_asset_details();
    void select_entry(const AssetBrowserEntry& entry);
    void select_only(const AssetBrowserEntry& entry);
    void clear_selection();
    [[nodiscard]] bool is_selected(const AssetBrowserEntry& entry) const;
    [[nodiscard]] std::vector<std::filesystem::path> selected_asset_paths() const;
    void move_asset_paths(
        const std::vector<std::filesystem::path>& paths,
        const std::filesystem::path& directory
    );
    void delete_selected_assets(bool allow_referenced = false);
    [[nodiscard]] std::vector<std::string> selected_asset_usages() const;
    void toggle_selected_favorites();
    void record_recent(AssetHandle handle);
    void load_preferences();
    void save_preferences() const;
    [[nodiscard]] SDL_Texture* preview_asset(const AssetBrowserEntry& entry);
    [[nodiscard]] SDL_Texture* render_prefab_preview(const AssetBrowserEntry& entry);
    [[nodiscard]] SDL_Texture* render_entity_asset_preview(const AssetBrowserEntry& entry);
    void destroy_asset_previews();
    void request_rename(const AssetBrowserEntry& entry);
    [[nodiscard]] SDL_Texture* render_material_preview(
        AssetHandle handle,
        const std::shared_ptr<const Material>& material
    );
    bool ensure_material_preview_source();
    void destroy_material_previews();

    std::string m_name{"Asset Browser"};
    std::filesystem::path m_root_path;
    std::filesystem::path m_current_path;
    std::filesystem::path m_preferences_path;
    AssetDatabase* m_asset_database{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    std::shared_ptr<IMaterialBackend> m_material_backend;
    std::unique_ptr<Renderer2D> m_prefab_preview_renderer;
    SDL_Texture* m_material_preview_source{nullptr};
    std::unordered_map<AssetHandle, SDL_Texture*> m_material_preview_outputs;
    std::unordered_map<AssetHandle, SDL_Texture*> m_entity_preview_outputs;
    std::unordered_map<AssetHandle, std::shared_ptr<Texture>> m_texture_previews;
    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::function<void(const std::string&)> m_on_load_scene;
    std::function<void(const std::string&)> m_on_open_prefab;
    std::function<void(const std::string&)> m_on_open_level_palette;
    std::function<void(AssetHandle)> m_on_open_particle_effect;
    std::function<void(AssetHandle)> m_on_open_vfx_preview;
    std::function<void(AssetHandle, std::shared_ptr<const Material>)> m_on_material_preview;
    std::function<void()> m_on_material_preview_reset;
    std::function<std::vector<std::string>(AssetHandle)> m_live_asset_usage_provider;
    std::string m_selected_file{""};
    std::unordered_set<std::string> m_selected_files;
    std::string m_selection_anchor;
    std::vector<std::string> m_visible_entry_paths;
    std::string m_revealed_reference{""};
    std::array<char, 256> m_search_query{};
    AssetBrowserScope m_search_scope{AssetBrowserScope::CurrentFolder};
    AssetBrowserTypeFilter m_type_filter{AssetBrowserTypeFilter::All};
    AssetBrowserStatusFilter m_status_filter{AssetBrowserStatusFilter::All};
    AssetBrowserSort m_sort{AssetBrowserSort::Name};
    bool m_sort_ascending{true};
    AssetBrowserViewMode m_view_mode{AssetBrowserViewMode::Grid};
    AssetBrowserCollection m_collection{AssetBrowserCollection::All};
    std::vector<AssetHandle> m_favorites;
    std::vector<AssetHandle> m_recent;
    bool m_show_delete_confirmation{false};
    std::string m_bulk_action_error;
    bool m_show_rename_popup{false};
    std::filesystem::path m_rename_source;
    std::array<char, 256> m_rename_buffer{};
    std::string m_rename_error;
    char m_new_scene_name_buf[128]{"NewScene"};
    char m_new_script_name_buf[128]{"NewScript"};
    char m_new_material_name_buf[128]{"NewMaterial"};
    char m_new_post_process_name_buf[128]{"NewPostProcessProfile"};
    char m_new_layer_effect_name_buf[128]{"NewLayerEffectProfile"};
    char m_new_particle_name_buf[128]{"NewParticleEffect"};
    AssetHandle m_new_layer_effect_material_handle{};
    bool m_show_new_scene_popup{false};
    bool m_show_new_script_popup{false};
    bool m_show_new_material_popup{false};
    bool m_show_new_post_process_popup{false};
    bool m_show_new_layer_effect_popup{false};
    bool m_show_new_particle_popup{false};
    bool m_show_edit_layer_effect_popup{false};
    bool m_show_edit_material_popup{false};
    AssetHandle m_edit_material_handle{};
    MaterialAssetDescription m_edit_material_draft;
    char m_edit_material_name_buf[128]{};
    std::array<std::array<char, 513>, 8> m_edit_material_uniforms{};
    std::size_t m_edit_material_uniform_count{0};
    std::string m_edit_material_error;
    bool m_edit_material_preview_active{false};
    std::shared_ptr<const Material> m_edit_material_preview_material;
    AssetHandle m_edit_layer_effect_handle{};
    RenderLayerEffectProfile m_edit_layer_effect_original;
    RenderLayerEffectProfile m_edit_layer_effect_draft;
    char m_edit_layer_effect_name_buf[128]{};
    std::string m_edit_layer_effect_error;
    float m_thumbnail_size{72.0f};
    float m_padding{14.0f};
    bool m_is_focused{false};
};

} // namespace Engine
