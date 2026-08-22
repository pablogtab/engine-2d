#pragma once

#include "../EditorPanel.hpp"
#include "../../core/AssetHandle.hpp"
#include "../../renderer/RenderLayerEffectProfile.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../tools/AssetBrowserQuery.hpp"
#include <array>
#include <string>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>

struct SDL_Renderer;
struct SDL_Texture;

namespace Engine {

enum class AssetBrowserViewMode {
    Grid,
    List,
    Details
};

class AssetDatabase;
class EditorCommandHistory;
class Material;
class IMaterialBackend;

class AssetBrowserPanel : public EditorPanel {
public:
    explicit AssetBrowserPanel(AssetDatabase* asset_database, std::string root_path = "Assets");
    ~AssetBrowserPanel() override;

    void on_gui() override;
    void on_render_resources_invalidated() override { destroy_material_previews(); }
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_on_load_scene_callback(std::function<void(const std::string&)> cb) {
        m_on_load_scene = std::move(cb);
    }
    void set_on_open_prefab_callback(std::function<void(const std::string&)> cb) {
        m_on_open_prefab = std::move(cb);
    }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) {
        m_command_history = std::move(command_history);
    }
    void set_renderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void set_material_backend(std::shared_ptr<IMaterialBackend> backend) {
        m_material_backend = std::move(backend);
    }

    void refresh();
    void reveal_asset_path(const std::string& path);
    [[nodiscard]] AssetHandle create_material_asset(std::string base_name = "NewMaterial");
    [[nodiscard]] AssetHandle duplicate_material_asset(AssetHandle source_handle);
    void open_material_asset(AssetHandle handle);
    [[nodiscard]] SDL_Texture* preview_material_asset(AssetHandle handle);
    void set_on_material_preview(
        std::function<void(AssetHandle, std::shared_ptr<const Material>)> callback
    ) { m_on_material_preview = std::move(callback); }
    void set_on_material_preview_reset(std::function<void()> callback) {
        m_on_material_preview_reset = std::move(callback);
    }

private:
    void navigate_to(const std::filesystem::path& directory);
    void activate_entry(const AssetBrowserEntry& entry);
    void draw_entry(const AssetBrowserEntry& entry);
    void draw_entry_interactions(const AssetBrowserEntry& entry);
    void draw_list_entry(const AssetBrowserEntry& entry);
    void draw_details_entries(const std::vector<AssetBrowserEntry>& entries);
    void draw_selected_asset_details();
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
    AssetDatabase* m_asset_database{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    std::shared_ptr<IMaterialBackend> m_material_backend;
    SDL_Texture* m_material_preview_source{nullptr};
    std::unordered_map<AssetHandle, SDL_Texture*> m_material_preview_outputs;
    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::function<void(const std::string&)> m_on_load_scene;
    std::function<void(const std::string&)> m_on_open_prefab;
    std::function<void(AssetHandle, std::shared_ptr<const Material>)> m_on_material_preview;
    std::function<void()> m_on_material_preview_reset;
    std::string m_selected_file{""};
    std::string m_revealed_reference{""};
    std::array<char, 256> m_search_query{};
    AssetBrowserScope m_search_scope{AssetBrowserScope::CurrentFolder};
    AssetBrowserTypeFilter m_type_filter{AssetBrowserTypeFilter::All};
    AssetBrowserStatusFilter m_status_filter{AssetBrowserStatusFilter::All};
    AssetBrowserSort m_sort{AssetBrowserSort::Name};
    bool m_sort_ascending{true};
    AssetBrowserViewMode m_view_mode{AssetBrowserViewMode::Grid};
    bool m_show_rename_popup{false};
    std::filesystem::path m_rename_source;
    std::array<char, 256> m_rename_buffer{};
    std::string m_rename_error;
    char m_new_scene_name_buf[128]{"NewScene"};
    char m_new_script_name_buf[128]{"NewScript"};
    char m_new_material_name_buf[128]{"NewMaterial"};
    char m_new_post_process_name_buf[128]{"NewPostProcessProfile"};
    char m_new_layer_effect_name_buf[128]{"NewLayerEffectProfile"};
    AssetHandle m_new_layer_effect_material_handle{};
    bool m_show_new_scene_popup{false};
    bool m_show_new_script_popup{false};
    bool m_show_new_material_popup{false};
    bool m_show_new_post_process_popup{false};
    bool m_show_new_layer_effect_popup{false};
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
};

} // namespace Engine
