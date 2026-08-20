#pragma once

#include "../EditorPanel.hpp"
#include <string>
#include <filesystem>
#include <functional>
#include <memory>

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

    void refresh();

private:
    std::string m_name{"Asset Browser"};
    std::filesystem::path m_root_path;
    std::filesystem::path m_current_path;
    AssetDatabase* m_asset_database{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::function<void(const std::string&)> m_on_load_scene;
    std::function<void(const std::string&)> m_on_open_prefab;
    std::string m_selected_file{""};
    char m_new_scene_name_buf[128]{"NewScene"};
    char m_new_material_name_buf[128]{"NewMaterial"};
    char m_new_post_process_name_buf[128]{"NewPostProcessProfile"};
    bool m_show_new_scene_popup{false};
    bool m_show_new_material_popup{false};
    bool m_show_new_post_process_popup{false};
    float m_thumbnail_size{72.0f};
    float m_padding{14.0f};
};

} // namespace Engine
