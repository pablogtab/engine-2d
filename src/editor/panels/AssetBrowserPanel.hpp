#pragma once

#include "../EditorPanel.hpp"
#include "../../core/AssetHandle.hpp"
#include <array>
#include <string>
#include <filesystem>
#include <functional>
#include <memory>

struct SDL_Renderer;

namespace Engine {

class AssetDatabase;
class EditorCommandHistory;

class AssetBrowserPanel : public EditorPanel {
public:
    explicit AssetBrowserPanel(AssetDatabase* asset_database, std::string root_path = "assets");
    ~AssetBrowserPanel() override = default;

    void on_gui() override;
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

    void refresh();
    void reveal_asset_path(const std::string& path);

private:
    std::string m_name{"Asset Browser"};
    std::filesystem::path m_root_path;
    std::filesystem::path m_current_path;
    AssetDatabase* m_asset_database{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::function<void(const std::string&)> m_on_load_scene;
    std::function<void(const std::string&)> m_on_open_prefab;
    std::string m_selected_file{""};
    std::string m_revealed_reference{""};
    char m_new_scene_name_buf[128]{"NewScene"};
    char m_new_material_name_buf[128]{"NewMaterial"};
    char m_new_post_process_name_buf[128]{"NewPostProcessProfile"};
    bool m_show_new_scene_popup{false};
    bool m_show_new_material_popup{false};
    bool m_show_new_post_process_popup{false};
    bool m_show_edit_material_popup{false};
    AssetHandle m_edit_material_handle{};
    AssetHandle m_edit_material_shader_handle{};
    char m_edit_material_name_buf[128]{};
    std::array<std::array<char, 513>, 8> m_edit_material_uniforms{};
    std::size_t m_edit_material_uniform_count{0};
    std::string m_edit_material_error;
    float m_thumbnail_size{72.0f};
    float m_padding{14.0f};
};

} // namespace Engine
