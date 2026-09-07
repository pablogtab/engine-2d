#pragma once

#include "../EditorPanel.hpp"
#include "../tools/TextureCompositionService.hpp"

#include <array>
#include <memory>
#include <string>

struct SDL_Renderer;

namespace Engine {

class EditorSelection;
class SceneManager;
class Texture;

class TextureComposerPanel final : public EditorPanel {
public:
    TextureComposerPanel(
        SDL_Renderer* renderer,
        SceneManager* scene_manager,
        std::shared_ptr<EditorSelection> selection
    );

    void on_gui() override;
    void on_context_changed(const EditorPanelContext& context) override;
    void on_render_resources_invalidated() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    // Captures authored sprite geometry from the current viewport selection.
    // It never mutates or hides the source entities.
    bool import_viewport_selection();

private:
    void rebuild_preview();
    void export_png();
    void save_recipe(const std::filesystem::path& output);
    void load_recipe();
    void add_dropped_texture(const std::string& path);
    [[nodiscard]] std::filesystem::path resolved_output_path(std::string& error) const;
    [[nodiscard]] std::filesystem::path recipe_path_for(const std::filesystem::path& output) const;

    std::string m_name{"Texture Composer"};
    SDL_Renderer* m_renderer{nullptr};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
    TextureCompositionSettings m_settings;
    TextureCompositionResult m_result;
    std::unique_ptr<Texture> m_preview;
    std::array<char, 512> m_output_path{};
    float m_preview_zoom{1.0f};
    float m_pixels_per_world_unit{1.0f};
    int m_padding{8};
    std::string m_status;
    std::string m_error;
};

} // namespace Engine
