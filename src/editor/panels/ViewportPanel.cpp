#include "ViewportPanel.hpp"
#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"
#include "../stage/PrefabStageController.hpp"
#include "TilemapPalettePanel.hpp"
#include "LevelComposerPanel.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/HierarchyCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../commands/EditorEntityStateCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../tools/EditorIcons.hpp"
#include "../tools/ViewportNavigation.hpp"
#include "../../core/Log.hpp"
#include "../../core/Time.hpp"
#include "../../ecs/CameraSystem.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../renderer/Renderer2D.hpp"
#include "../../renderer/SpriteSortKey.hpp"
#include "../../scene/EntitySubtreeArchive.hpp"
#include "../../scene/PrefabSerializer.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

namespace {

void draw_viewport_grid(const Camera2D& camera, const ImVec2& image_min, const ImVec2& image_size, float cell_size, const ViewportGridSettings& settings) {
    if (cell_size <= 0.0f || image_size.x <= 0.0f || image_size.y <= 0.0f) return;

    const glm::vec2 viewport_size = camera.get_viewport_size();
    const float scale_x = image_size.x / viewport_size.x;
    const float scale_y = image_size.y / viewport_size.y;
    const glm::vec2 world_a = camera.screen_to_world(glm::vec2(0.0f, 0.0f));
    const glm::vec2 world_b = camera.screen_to_world(viewport_size);
    const float min_x = std::min(world_a.x, world_b.x);
    const float max_x = std::max(world_a.x, world_b.x);
    const float min_y = std::min(world_a.y, world_b.y);
    const float max_y = std::max(world_a.y, world_b.y);

    const glm::vec2 origin_screen = camera.world_to_screen(glm::vec3(0.0f));
    const glm::vec2 step_screen = camera.world_to_screen(glm::vec3(cell_size, 0.0f, 0.0f)) - origin_screen;
    const float pixels_per_cell = std::max(std::abs(step_screen.x * scale_x), 0.001f);
    // Keep the authoring grid calm at normal and distant zoom levels. The
    // visual spacing changes, but every line remains a multiple of the snap cell.
    const int display_stride = std::max(1, static_cast<int>(std::ceil(settings.target_spacing_pixels / pixels_per_cell)));
    const float display_step = cell_size * static_cast<float>(display_stride);
    const int first_x = static_cast<int>(std::floor(min_x / display_step));
    const int last_x = static_cast<int>(std::ceil(max_x / display_step));
    const int first_y = static_cast<int>(std::floor(min_y / display_step));
    const int last_y = static_cast<int>(std::ceil(max_y / display_step));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const auto to_screen = [&](const glm::vec2& world) {
        const glm::vec2 screen = camera.world_to_screen(glm::vec3(world, 0.0f));
        return ImVec2(image_min.x + screen.x * scale_x, image_min.y + screen.y * scale_y);
    };
    const auto draw_vertical = [&](int index, ImU32 color, float thickness) {
        const float world_x = static_cast<float>(index) * display_step;
        draw_list->AddLine(to_screen(glm::vec2(world_x, min_y)), to_screen(glm::vec2(world_x, max_y)), color, thickness);
    };
    const auto draw_horizontal = [&](int index, ImU32 color, float thickness) {
        const float world_y = static_cast<float>(index) * display_step;
        draw_list->AddLine(to_screen(glm::vec2(min_x, world_y)), to_screen(glm::vec2(max_x, world_y)), color, thickness);
    };

    for (int index = first_x; index <= last_x; ++index) {
        if (index % settings.major_line_interval == 0) continue;
        draw_vertical(index, IM_COL32(103, 112, 145, static_cast<int>(settings.minor_opacity * 255.0f)), 1.0f);
    }
    for (int index = first_y; index <= last_y; ++index) {
        if (index % settings.major_line_interval == 0) continue;
        draw_horizontal(index, IM_COL32(103, 112, 145, static_cast<int>(settings.minor_opacity * 255.0f)), 1.0f);
    }
    for (int index = first_x; index <= last_x; ++index) {
        if (settings.show_axes && index == 0) draw_vertical(index, IM_COL32(111, 153, 246, 130), 1.4f);
        else if (index % settings.major_line_interval == 0) draw_vertical(index, IM_COL32(103, 112, 145, static_cast<int>(settings.major_opacity * 255.0f)), 1.0f);
    }
    for (int index = first_y; index <= last_y; ++index) {
        if (settings.show_axes && index == 0) draw_horizontal(index, IM_COL32(111, 153, 246, 130), 1.4f);
        else if (index % settings.major_line_interval == 0) draw_horizontal(index, IM_COL32(103, 112, 145, static_cast<int>(settings.major_opacity * 255.0f)), 1.0f);
    }
}

void draw_viewport_grid_to_render_target(SDL_Renderer* renderer, const Camera2D& camera, float cell_size, const ViewportGridSettings& settings) {
    if (!renderer || cell_size <= 0.0f) return;

    const glm::vec2 viewport_size = camera.get_viewport_size();
    const glm::vec2 world_a = camera.screen_to_world(glm::vec2(0.0f, 0.0f));
    const glm::vec2 world_b = camera.screen_to_world(viewport_size);
    const float min_x = std::min(world_a.x, world_b.x);
    const float max_x = std::max(world_a.x, world_b.x);
    const float min_y = std::min(world_a.y, world_b.y);
    const float max_y = std::max(world_a.y, world_b.y);

    const glm::vec2 origin_screen = camera.world_to_screen(glm::vec3(0.0f));
    const glm::vec2 step_screen = camera.world_to_screen(glm::vec3(cell_size, 0.0f, 0.0f)) - origin_screen;
    const float pixels_per_cell = std::max(std::abs(step_screen.x), 0.001f);
    const int display_stride = std::max(1, static_cast<int>(std::ceil(settings.target_spacing_pixels / pixels_per_cell)));
    const float display_step = cell_size * static_cast<float>(display_stride);
    const int first_x = static_cast<int>(std::floor(min_x / display_step));
    const int last_x = static_cast<int>(std::ceil(max_x / display_step));
    const int first_y = static_cast<int>(std::floor(min_y / display_step));
    const int last_y = static_cast<int>(std::ceil(max_y / display_step));

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto to_screen = [&](const glm::vec2& world) { return camera.world_to_screen(glm::vec3(world, 0.0f)); };
    const auto draw_line = [&](const glm::vec2& from, const glm::vec2& to, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha) {
        SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
        SDL_RenderLine(renderer, from.x, from.y, to.x, to.y);
    };
    const auto draw_vertical = [&](int index, Uint8 alpha) {
        const float world_x = static_cast<float>(index) * display_step;
        draw_line(to_screen(glm::vec2(world_x, min_y)), to_screen(glm::vec2(world_x, max_y)), 103, 112, 145, alpha);
    };
    const auto draw_horizontal = [&](int index, Uint8 alpha) {
        const float world_y = static_cast<float>(index) * display_step;
        draw_line(to_screen(glm::vec2(min_x, world_y)), to_screen(glm::vec2(max_x, world_y)), 103, 112, 145, alpha);
    };

    const Uint8 minor_alpha = static_cast<Uint8>(settings.minor_opacity * 255.0f);
    const Uint8 major_alpha = static_cast<Uint8>(settings.major_opacity * 255.0f);
    for (int index = first_x; index <= last_x; ++index) {
        if (index % settings.major_line_interval != 0) draw_vertical(index, minor_alpha);
    }
    for (int index = first_y; index <= last_y; ++index) {
        if (index % settings.major_line_interval != 0) draw_horizontal(index, minor_alpha);
    }
    for (int index = first_x; index <= last_x; ++index) {
        if (settings.show_axes && index == 0) {
            const float x = to_screen(glm::vec2(0.0f, min_y)).x;
            draw_line(glm::vec2(x, 0.0f), glm::vec2(x, viewport_size.y), 111, 153, 246, 130);
        } else if (index % settings.major_line_interval == 0) {
            draw_vertical(index, major_alpha);
        }
    }
    for (int index = first_y; index <= last_y; ++index) {
        if (settings.show_axes && index == 0) {
            const float y = to_screen(glm::vec2(min_x, 0.0f)).y;
            draw_line(glm::vec2(0.0f, y), glm::vec2(viewport_size.x, y), 111, 153, 246, 130);
        } else if (index % settings.major_line_interval == 0) {
            draw_horizontal(index, major_alpha);
        }
    }
}

} // namespace

ViewportPanel::ViewportPanel(
    SDL_Renderer* renderer, 
    SceneManager* scene_manager, 
    std::shared_ptr<EditorSelection> selection,
    std::shared_ptr<EditorCommandHistory> command_history,
    ProjectSettings* project_settings
)
    : m_renderer(renderer), 
      m_scene_manager(scene_manager), 
      m_project_settings(project_settings),
      m_selection(selection),
      m_command_history(command_history) {
    
    if (!m_selection) {
        m_selection = std::make_shared<EditorSelection>();
    }

    ensure_render_target();

    Time::set_time_scale(0.0f); // Default to Edit Mode (paused) on launch
}

void ViewportPanel::on_context_changed(const EditorPanelContext& context) {
    // A placement preview belongs to the scene and history that were active
    // when the stroke began. Never carry it across a tab or Prefab Stage switch.
    m_level_composer_tool.cancel();
    set_command_history(context.command_history);
    set_entity_state_store(context.entity_state_store);
    set_selection(context.selection);
}

bool ViewportPanel::ensure_render_target() {
    if (!m_renderer) return false;
    m_requested_width = std::max(m_requested_width, 1);
    m_requested_height = std::max(m_requested_height, 1);
    if (m_viewport_texture && m_render_width == m_requested_width && m_render_height == m_requested_height) return true;

    SDL_Texture* replacement = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        m_requested_width,
        m_requested_height
    );
    if (!replacement) {
        ENGINE_LOG_ERROR("[VIEWPORT] Failed to create render target {}x{}: {}", m_requested_width, m_requested_height, SDL_GetError());
        return m_viewport_texture != nullptr;
    }
    SDL_SetTextureScaleMode(replacement, SDL_SCALEMODE_LINEAR);
    if (m_viewport_texture) SDL_DestroyTexture(m_viewport_texture);
    m_viewport_texture = replacement;
    m_render_width = m_requested_width;
    m_render_height = m_requested_height;

    return true;
}

void ViewportPanel::configure_scene_view() {
    if (!m_scene_manager) return;
    const RenderViewKind view = m_scene_manager->is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
    if (view == RenderViewKind::Game && m_project_settings) {
        const auto& game_view = m_project_settings->display();
        if (has_device_preview()) {
            m_game_safe_area_render = scale_device_safe_area(
                active_device_preview(), m_render_width, m_render_height
            );
        } else {
            const float scale_x = static_cast<float>(m_render_width) /
                static_cast<float>(std::max(game_view.reference_width, 1));
            const float scale_y = static_cast<float>(m_render_height) /
                static_cast<float>(std::max(game_view.reference_height, 1));
            m_game_safe_area_render = {
                m_safe_area_preview.left * scale_x,
                m_safe_area_preview.top * scale_y,
                m_safe_area_preview.right * scale_x,
                m_safe_area_preview.bottom * scale_y
            };
        }
        m_scene_manager->configure_view(view, {
            m_render_width, m_render_height,
            game_view.reference_width, game_view.reference_height,
            m_game_safe_area_render
        });
    } else {
        m_scene_manager->configure_view(view, {
            m_render_width, m_render_height,
            m_render_width, m_render_height,
            m_safe_area_preview,
            m_show_safe_area_overlay
        });
    }
}

bool ViewportPanel::has_device_preview() const {
    return m_device_preview_profile != 0;
}

DevicePreviewProfile ViewportPanel::active_device_preview() const {
    if (m_device_preview_profile > 0 &&
        m_device_preview_profile <= k_device_preview_profiles.size()) {
        return orient_device_preview(
            k_device_preview_profiles[m_device_preview_profile - 1],
            m_device_preview_orientation
        );
    }
    return orient_device_preview(m_custom_device_preview, m_device_preview_orientation);
}

void ViewportPanel::render_device_preview_toolbar() {
    ImGui::PushID("DevicePreviewToolbar");
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Device");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);

    const std::size_t custom_index = k_device_preview_profiles.size() + 1;
    const char* preview_name = "Free Aspect";
    if (m_device_preview_profile > 0 &&
        m_device_preview_profile <= k_device_preview_profiles.size()) {
        preview_name = k_device_preview_profiles[m_device_preview_profile - 1].name.data();
    } else if (m_device_preview_profile == custom_index) {
        preview_name = "Custom";
    }

    if (ImGui::BeginCombo("##profile", preview_name)) {
        if (ImGui::Selectable("Free Aspect", m_device_preview_profile == 0)) {
            m_device_preview_profile = 0;
        }
        for (std::size_t index = 0; index < k_device_preview_profiles.size(); ++index) {
            const bool selected = m_device_preview_profile == index + 1;
            if (ImGui::Selectable(k_device_preview_profiles[index].name.data(), selected)) {
                m_device_preview_profile = index + 1;
            }
        }
        if (ImGui::Selectable("Custom", m_device_preview_profile == custom_index)) {
            m_device_preview_profile = custom_index;
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!has_device_preview());
    if (editor_icon_button(
            "rotate", EditorIcon::Rotate,
            "Rotate simulated device", false, ImVec2(24.0f, 24.0f)
        )) {
        m_device_preview_orientation =
            m_device_preview_orientation == DevicePreviewOrientation::Portrait
                ? DevicePreviewOrientation::Landscape
                : DevicePreviewOrientation::Portrait;
    }
    ImGui::SameLine();
    if (editor_icon_button(
            "settings", EditorIcon::Settings,
            "Device preview details", false, ImVec2(24.0f, 24.0f)
        )) {
        ImGui::OpenPopup("Device Preview Settings");
    }
    ImGui::EndDisabled();

    if (has_device_preview()) {
        const DevicePreviewProfile preview = active_device_preview();
        ImGui::SameLine();
        ImGui::TextDisabled("%d x %d", preview.width, preview.height);
    }

    if (ImGui::BeginPopup("Device Preview Settings")) {
        ImGui::TextUnformatted("Device Preview");
        ImGui::Separator();
        const bool custom = m_device_preview_profile == custom_index;
        ImGui::BeginDisabled(!custom);
        int resolution[2]{m_custom_device_preview.width, m_custom_device_preview.height};
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::InputInt2("Resolution", resolution)) {
            m_custom_device_preview.width = resolution[0];
            m_custom_device_preview.height = resolution[1];
        }
        float insets[4]{
            m_custom_device_preview.safe_area.left,
            m_custom_device_preview.safe_area.top,
            m_custom_device_preview.safe_area.right,
            m_custom_device_preview.safe_area.bottom
        };
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::InputFloat4("Safe L/T/R/B", insets, "%.0f")) {
            m_custom_device_preview.safe_area = {
                insets[0], insets[1], insets[2], insets[3]
            };
        }
        ImGui::EndDisabled();
        if (!custom) ImGui::TextDisabled("Select Custom to edit these values.");
        m_custom_device_preview = sanitize_device_preview(m_custom_device_preview);
        ImGui::Checkbox("Show safe area overlay", &m_show_safe_area_overlay);
        const DevicePreviewProfile preview = active_device_preview();
        ImGui::Spacing();
        ImGui::Text(
            "Output: %d x %d | Safe: %.0f / %.0f / %.0f / %.0f",
            preview.width,
            preview.height,
            preview.safe_area.left,
            preview.safe_area.top,
            preview.safe_area.right,
            preview.safe_area.bottom
        );
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

ViewportPanel::~ViewportPanel() {
    if (m_viewport_texture) {
        SDL_DestroyTexture(m_viewport_texture);
        m_viewport_texture = nullptr;
    }
}

bool ViewportPanel::get_ai_view_state(ViewportAiState& state, std::string& error) const {
    error.clear();
    if (!m_scene_manager || !m_scene_manager->is_editing()) {
        error = "Viewport navigation is available only in Edit mode";
        return false;
    }
    const Camera2D* camera = m_scene_manager->get_render_camera(RenderViewKind::Editor);
    if (!camera) {
        error = "The active scene does not expose an editor camera";
        return false;
    }
    const SDL_FRect bounds = camera->get_world_bounds();
    state.center = camera->get_position();
    state.world_min = {bounds.x, bounds.y};
    state.world_max = {bounds.x + bounds.w, bounds.y + bounds.h};
    state.zoom = camera->get_zoom();
    state.render_width = m_render_width;
    state.render_height = m_render_height;
    state.render_generation = m_render_generation;
    return true;
}

bool ViewportPanel::ai_pan(const glm::vec2& world_delta, std::string& error) {
    error.clear();
    if (!m_scene_manager || !m_scene_manager->is_editing()) {
        error = "Viewport navigation is available only in Edit mode";
        return false;
    }
    if (!ViewportNavigation::pan_world(m_scene_manager->get_render_camera(RenderViewKind::Editor), world_delta)) {
        error = "The active scene does not expose an editor camera";
        return false;
    }
    return true;
}

bool ViewportPanel::ai_zoom(float factor, std::string& error) {
    error.clear();
    if (!m_scene_manager || !m_scene_manager->is_editing()) {
        error = "Viewport navigation is available only in Edit mode";
        return false;
    }
    if (!ViewportNavigation::zoom(m_scene_manager->get_render_camera(RenderViewKind::Editor), factor)) {
        error = "The active scene does not expose an editor camera";
        return false;
    }
    return true;
}

bool ViewportPanel::ai_focus_entities(
    const std::vector<EntityID>& entities,
    float padding,
    std::string& error
) {
    error.clear();
    if (!m_scene_manager || !m_scene_manager->is_editing()) {
        error = "Viewport navigation is available only in Edit mode";
        return false;
    }
    Scene* scene = m_scene_manager->get_active_scene();
    Camera2D* camera = m_scene_manager->get_render_camera(RenderViewKind::Editor);
    if (!scene || !camera) {
        error = "The active scene does not expose an editor camera";
        return false;
    }
    for (const EntityID entity : entities) {
        if (!scene->get_entity_manager().is_alive(entity)) {
            error = "Viewport target entity does not exist: " + std::to_string(entity);
            return false;
        }
    }
    if (!ViewportNavigation::frame_entities(scene, camera, entities, padding)) {
        error = "Viewport targets do not have visible transform bounds";
        return false;
    }
    return true;
}

bool ViewportPanel::ai_frame_all(float padding, std::string& error) {
    error.clear();
    if (!m_scene_manager || !m_scene_manager->is_editing()) {
        error = "Viewport navigation is available only in Edit mode";
        return false;
    }
    Scene* scene = m_scene_manager->get_active_scene();
    Camera2D* camera = m_scene_manager->get_render_camera(RenderViewKind::Editor);
    if (!scene || !camera) {
        error = "The active scene does not expose an editor camera";
        return false;
    }
    if (!ViewportNavigation::frame_all(scene, camera, padding)) {
        error = "The active scene has no visible transform bounds";
        return false;
    }
    return true;
}

bool ViewportPanel::ai_reset(std::string& error) {
    error.clear();
    if (!m_scene_manager || !m_scene_manager->is_editing()) {
        error = "Viewport navigation is available only in Edit mode";
        return false;
    }
    if (!ViewportNavigation::reset(m_scene_manager->get_render_camera(RenderViewKind::Editor))) {
        error = "The active scene does not expose an editor camera";
        return false;
    }
    return true;
}

bool ViewportPanel::capture_viewport_png(
    const std::string& path,
    std::size_t& byte_count,
    std::string& error
) {
    byte_count = 0;
    error.clear();
    if (!m_renderer || !m_viewport_texture || m_render_width <= 0 || m_render_height <= 0) {
        error = "The viewport render target is unavailable";
        return false;
    }
    constexpr int MAX_CAPTURE_DIMENSION = 2048;
    if (m_render_width > MAX_CAPTURE_DIMENSION || m_render_height > MAX_CAPTURE_DIMENSION) {
        error = "The viewport exceeds the 2048 pixel capture limit";
        return false;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(m_renderer);
    if (!SDL_SetRenderTarget(m_renderer, m_viewport_texture)) {
        error = std::string("Could not select the viewport render target: ") + SDL_GetError();
        return false;
    }
    SDL_Surface* surface = SDL_RenderReadPixels(m_renderer, nullptr);
    static_cast<void>(SDL_SetRenderTarget(m_renderer, previous_target));
    if (!surface) {
        error = std::string("Could not read viewport pixels: ") + SDL_GetError();
        return false;
    }

    const std::filesystem::path output(path);
    std::error_code filesystem_error;
    if (output.has_parent_path()) {
        std::filesystem::create_directories(output.parent_path(), filesystem_error);
    }
    const bool saved = !filesystem_error && SDL_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    if (!saved) {
        error = filesystem_error
            ? "Could not create the viewport capture directory"
            : std::string("Could not encode the viewport PNG: ") + SDL_GetError();
        return false;
    }
    byte_count = std::filesystem::file_size(output, filesystem_error);
    if (filesystem_error || byte_count == 0) {
        error = "The viewport PNG was empty or unreadable";
        std::filesystem::remove(output, filesystem_error);
        return false;
    }
    return true;
}

bool ViewportPanel::prepare_prefab_preview(
    const std::string& path,
    const glm::vec2& world_position,
    float rotation_degrees
) {
    return prepare_prefab_preview(
        path, std::vector<glm::vec2>{world_position}, rotation_degrees, 0.0f
    );
}

bool ViewportPanel::prepare_prefab_preview(
    const std::string& path,
    const std::vector<glm::vec2>& world_positions,
    float rotation_degrees,
    float depth_z
) {
    if (path.empty() || world_positions.empty()) return false;
    const glm::vec2 base_position = world_positions.front();
    if (m_prefab_preview_path != path || !m_prefab_preview_entities ||
        !m_prefab_preview_entities->is_alive(m_prefab_preview_root)) {
        const std::string json = PrefabSerializer::read_json_from_file(path);
        if (json.empty()) return false;

        auto entities = std::make_unique<EntityManager>();
        ComponentSerializationContext context{
            .renderer = m_renderer,
            .asset_manager = &AssetManager::get(),
            .asset_database = &AssetDatabase::get()
        };
        const TransformComponent desired{
            glm::vec3(base_position, depth_z), glm::vec2(1.0f), rotation_degrees
        };
        SubtreeInstantiationResult result = EntitySubtreeArchive::instantiate_from_json(
            *entities, json, desired, 0, context
        );
        if (!result.success || result.root_entity == 0) return false;

        m_prefab_preview_path = path;
        m_prefab_preview_root = result.root_entity;
        m_prefab_preview_entities = std::move(entities);
    }

    m_prefab_preview_positions = world_positions;
    m_prefab_preview_depth_z = depth_z;
    if (m_prefab_preview_entities->has_component<TransformComponent>(m_prefab_preview_root)) {
        auto& root = m_prefab_preview_entities->get_component<TransformComponent>(m_prefab_preview_root);
        root.position.x = base_position.x;
        root.position.y = base_position.y;
        root.position.z = depth_z;
        root.rotation = rotation_degrees;
    }
    TransformSystem::update(*m_prefab_preview_entities);
    return true;
}

void ViewportPanel::render_prefab_preview(
    const Camera2D& camera,
    const ImVec2& image_min,
    const ImVec2& image_size
) {
    if (!m_prefab_preview_visible || !m_prefab_preview_entities) return;
    const glm::vec2 camera_size = camera.get_viewport_size();
    if (camera_size.x <= 0.0f || camera_size.y <= 0.0f) return;

    const auto to_screen = [&](const glm::vec3& world) {
        const glm::vec2 point = camera.world_to_screen(world);
        return ImVec2(
            image_min.x + point.x * image_size.x / camera_size.x,
            image_min.y + point.y * image_size.y / camera_size.y
        );
    };
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    struct BasePreviewSprite {
        const TransformComponent* transform{nullptr};
        const SpriteComponent* sprite{nullptr};
        Texture* texture{nullptr};
        RenderLayer render_layer{DEFAULT_RENDER_LAYER};
    };
    std::vector<BasePreviewSprite> base_sprites;
    m_prefab_preview_entities->for_each<TransformComponent, SpriteComponent>(
        [&](EntityID, const TransformComponent& transform, const SpriteComponent& sprite) {
            if (!transform.world_visible || !sprite.visible) return;
            Texture* texture = sprite.texture
                ? sprite.texture : AssetManager::get().get_white_texture(m_renderer);
            if (!texture || !texture->get_sdl_texture()) return;
            const RenderLayer render_layer = std::min<RenderLayer>(sprite.render_layer, 31);
            if (!camera.renders_layer(render_layer)) return;
            base_sprites.push_back({&transform, &sprite, texture, render_layer});
        }
    );

    struct PreviewSprite {
        const BasePreviewSprite* base{nullptr};
        glm::vec3 world_position{0.0f};
        std::uint64_t sort_key{0};
    };
    std::vector<PreviewSprite> preview_sprites;
    preview_sprites.reserve(base_sprites.size() * m_prefab_preview_positions.size());
    const glm::vec2 base_position = m_prefab_preview_positions.front();
    for (const glm::vec2 instance_position : m_prefab_preview_positions) {
        const glm::vec2 offset = instance_position - base_position;
        for (const BasePreviewSprite& base : base_sprites) {
            glm::vec3 world_position = base.transform->world_position;
            world_position.x += offset.x;
            world_position.y += offset.y;
            preview_sprites.push_back({
                &base,
                world_position,
                SpriteSortKey::world(
                    base.sprite->sorting_layer,
                    world_position.z,
                    world_position.y,
                    base.sprite->order_in_layer
                )
            });
        }
    }
    std::stable_sort(
        preview_sprites.begin(), preview_sprites.end(),
        [](const PreviewSprite& left, const PreviewSprite& right) {
            return left.sort_key < right.sort_key;
        }
    );

    for (const PreviewSprite& preview : preview_sprites) {
        const TransformComponent& transform = *preview.base->transform;
        const SpriteComponent& sprite = *preview.base->sprite;
        Texture* texture = preview.base->texture;

        const glm::vec2 size = sprite.size * glm::abs(transform.world_scale);
        const float min_x = -transform.world_pivot.x * size.x;
        const float max_x = (1.0f - transform.world_pivot.x) * size.x;
        const float min_y = -transform.world_pivot.y * size.y;
        const float max_y = (1.0f - transform.world_pivot.y) * size.y;
        const float radians = glm::radians(transform.world_rotation);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        // Camera2D converts Cartesian Y-up into screen Y-down. Therefore
        // the texture's top edge corresponds to max_y in world space.
        const std::array<glm::vec2, 4> local{{
            {min_x, max_y}, {max_x, max_y}, {max_x, min_y}, {min_x, min_y}
        }};
        ImVec2 corners[4];
        for (std::size_t index = 0; index < local.size(); ++index) {
            const glm::vec2 rotated(
                local[index].x * cosine - local[index].y * sine,
                local[index].x * sine + local[index].y * cosine
            );
            corners[index] = to_screen(
                preview.world_position + glm::vec3(rotated, 0.0f)
            );
        }

        ImVec2 uv0{0.0f, 0.0f};
        ImVec2 uv1{1.0f, 0.0f};
        ImVec2 uv2{1.0f, 1.0f};
        ImVec2 uv3{0.0f, 1.0f};
        if (sprite.use_src_rect && texture->get_width() > 0 && texture->get_height() > 0) {
            const float width = static_cast<float>(texture->get_width());
            const float height = static_cast<float>(texture->get_height());
            uv0 = {sprite.src_rect.x / width, sprite.src_rect.y / height};
            uv1 = {(sprite.src_rect.x + sprite.src_rect.w) / width, sprite.src_rect.y / height};
            uv2 = {(sprite.src_rect.x + sprite.src_rect.w) / width, (sprite.src_rect.y + sprite.src_rect.h) / height};
            uv3 = {sprite.src_rect.x / width, (sprite.src_rect.y + sprite.src_rect.h) / height};
        }
        if (sprite.flip_x) { std::swap(uv0, uv1); std::swap(uv3, uv2); }
        if (sprite.flip_y) { std::swap(uv0, uv3); std::swap(uv1, uv2); }
        const int alpha = std::clamp(static_cast<int>(sprite.color.a) * 3 / 4, 70, 190);
        draw_list->AddImageQuad(
            texture->get_sdl_texture(), corners[0], corners[1], corners[2], corners[3],
            uv0, uv1, uv2, uv3,
            IM_COL32(sprite.color.r, sprite.color.g, sprite.color.b, alpha)
        );
        draw_list->AddPolyline(
            corners, 4, IM_COL32(103, 220, 190, 230), ImDrawFlags_Closed, 1.2f
        );
    }

    if (preview_sprites.empty()) {
        for (const glm::vec2 position : m_prefab_preview_positions) {
            const ImVec2 center = to_screen(glm::vec3(position, m_prefab_preview_depth_z));
            draw_list->AddRect(
                ImVec2(center.x - 18.0f, center.y - 18.0f),
                ImVec2(center.x + 18.0f, center.y + 18.0f),
                IM_COL32(103, 220, 190, 230), 2.0f, 0, 1.5f
            );
        }
    }
    const ImVec2 label = to_screen(glm::vec3(
        m_prefab_preview_positions.back(), m_prefab_preview_depth_z
    ));
    const std::string name = std::filesystem::path(m_prefab_preview_path).stem().string();
    draw_list->AddText(
        ImVec2(label.x + 12.0f, label.y + 12.0f),
        IM_COL32(175, 245, 225, 245),
        name.c_str()
    );
}

void ViewportPanel::on_render_resources_invalidated() {
    if (m_viewport_texture) SDL_DestroyTexture(m_viewport_texture);
    m_viewport_texture = nullptr;
    m_render_width = 0;
    m_render_height = 0;
}

void ViewportPanel::render_grid_settings_menu() {
    ImGui::Checkbox("Show grid (G)", &m_grid_settings.visible);
    ImGui::Checkbox("Show UI safe area", &m_show_safe_area_overlay);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Visual density", &m_grid_settings.target_spacing_pixels, 12.0f, 88.0f, "%.0f px");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("Major line interval", &m_grid_settings.major_line_interval, 2, 10);

    float minor_opacity_percent = m_grid_settings.minor_opacity * 100.0f;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Minor opacity", &minor_opacity_percent, 2.0f, 30.0f, "%.0f%%")) {
        m_grid_settings.minor_opacity = minor_opacity_percent / 100.0f;
    }
    float major_opacity_percent = m_grid_settings.major_opacity * 100.0f;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Major opacity", &major_opacity_percent, 5.0f, 50.0f, "%.0f%%")) {
        m_grid_settings.major_opacity = major_opacity_percent / 100.0f;
    }
    ImGui::Checkbox("Show world axes", &m_grid_settings.show_axes);
    m_reference_overlay.render_settings_menu();
    if (ImGui::Button("Reset grid")) m_grid_settings = ViewportGridSettings{};
}

void ViewportPanel::render_arrange_menu() {
    const bool can_arrange = m_scene_manager && m_selection && m_selection->count() >= 2 && m_command_history;
    if (!can_arrange) {
        ImGui::TextDisabled("Select at least two entities");
        return;
    }

    Scene* active_scene = m_scene_manager->get_active_scene();
    if (!active_scene) return;
    EntityManager& entity_manager = active_scene->get_entity_manager();
    const auto apply = [&](ArrangeAction action) {
        ViewportArrangementTool::apply(entity_manager, m_selection->get_selected(), action, m_command_history.get());
    };

    if (ImGui::BeginMenu("Align")) {
        if (ImGui::MenuItem("Left")) apply(ArrangeAction::AlignLeft);
        if (ImGui::MenuItem("Center")) apply(ArrangeAction::AlignCenter);
        if (ImGui::MenuItem("Right")) apply(ArrangeAction::AlignRight);
        ImGui::Separator();
        if (ImGui::MenuItem("Bottom")) apply(ArrangeAction::AlignBottom);
        if (ImGui::MenuItem("Middle")) apply(ArrangeAction::AlignMiddle);
        if (ImGui::MenuItem("Top")) apply(ArrangeAction::AlignTop);
        ImGui::EndMenu();
    }

    const bool can_distribute = m_selection->count() >= 3;
    if (ImGui::BeginMenu("Distribute", can_distribute)) {
        if (ImGui::MenuItem("Horizontally")) apply(ArrangeAction::DistributeHorizontal);
        if (ImGui::MenuItem("Vertically")) apply(ArrangeAction::DistributeVertical);
        ImGui::EndMenu();
    }
}

bool ViewportPanel::is_position_snap_active() const {
    const ImGuiIO& io = ImGui::GetIO();
    const auto& snap = m_gizmo.snap_settings();
    const bool is_active = snap.enabled != io.KeyShift;
    return is_active && snap.position_step > 0.0f;
}

void ViewportPanel::render_transform_toolbar() {
    if (!m_transform_toolbox.begin("Transform Toolbar")) {
        m_transform_toolbox.end();
        return;
    }

    if (EditorUi::toolbar_button(
            "move", EditorIcon::Move, "Move (W)",
            m_gizmo.get_tool() == TransformTool::Move)) {
        m_gizmo.set_tool(TransformTool::Move);
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (EditorUi::toolbar_button(
            "rotate", EditorIcon::Rotate, "Rotate (R)",
            m_gizmo.get_tool() == TransformTool::Rotate)) {
        m_gizmo.set_tool(TransformTool::Rotate);
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (EditorUi::toolbar_button(
            "scale", EditorIcon::Scale, "Scale (S)",
            m_gizmo.get_tool() == TransformTool::Scale)) {
        m_gizmo.set_tool(TransformTool::Scale);
    }
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 1.0f);
    ImGui::SameLine(0.0f, 6.0f);

    const bool multiple_selection = m_selection && m_selection->get_selected().size() > 1;
    if (multiple_selection) m_gizmo.set_space(GizmoSpace::World);
    const GizmoSpace space = m_gizmo.get_space();
    const char* space_tooltip = multiple_selection
        ? "Space: World (multiple selections use World)"
        : (space == GizmoSpace::World ? "Space: World (click for Local)" : "Space: Local (click for World)");
    const EditorIcon space_icon = space == GizmoSpace::World ? EditorIcon::World : EditorIcon::Local;
    if (EditorUi::toolbar_button(
            "space", space_icon, space_tooltip, true, !multiple_selection) &&
        !multiple_selection) {
        m_gizmo.set_space(space == GizmoSpace::Local ? GizmoSpace::World : GizmoSpace::Local);
    }
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 1.0f);
    ImGui::SameLine(0.0f, 6.0f);

    auto& snap = m_gizmo.snap_settings();
    if (EditorUi::toolbar_button(
            "grid", EditorIcon::Grid, "Grid settings (G toggles visibility)",
            m_grid_settings.visible)) {
        ImGui::OpenPopup("Viewport Grid Settings");
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (EditorUi::toolbar_button(
            "snap", EditorIcon::Snap, "Snap (Shift temporarily inverts it)", snap.enabled)) {
        snap.enabled = !snap.enabled;
    }
    ImGui::SameLine(0.0f, 1.0f);
    PhysicsOverlayMode physics_overlay_mode = m_collider_overlay.mode();
    const char* physics_overlay_tooltip = physics_overlay_mode == PhysicsOverlayMode::Selected
        ? "Physics overlay: selected (click for all)"
        : physics_overlay_mode == PhysicsOverlayMode::All
            ? "Physics overlay: all (click to hide)"
            : "Physics overlay: hidden (click for selected)";
    if (EditorUi::toolbar_button(
            "colliders",
            EditorIcon::Collider,
            physics_overlay_tooltip,
            physics_overlay_mode == PhysicsOverlayMode::All
    )) {
        m_collider_overlay.cycle_mode();
        physics_overlay_mode = m_collider_overlay.mode();
    }
    const ImVec2 collider_button_min = ImGui::GetItemRectMin();
    const ImVec2 collider_button_max = ImGui::GetItemRectMax();
    if (physics_overlay_mode == PhysicsOverlayMode::None) {
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(collider_button_min.x + 4.0f, collider_button_max.y - 4.0f),
            ImVec2(collider_button_max.x - 4.0f, collider_button_min.y + 4.0f),
            IM_COL32(235, 92, 92, 240), 1.8f
        );
    } else if (physics_overlay_mode == PhysicsOverlayMode::Selected) {
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(collider_button_max.x - 8.0f, collider_button_max.y - 10.0f),
            IM_COL32(244, 183, 64, 255), "S"
        );
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("Physics Overlay Settings");
    if (ImGui::BeginPopup("Physics Overlay Settings")) {
        auto& options = m_collider_overlay.debug_options();
        ImGui::TextUnformatted("Physics debug overlay");
        ImGui::Checkbox("Colliders", &options.colliders); ImGui::Checkbox("Contacts", &options.contacts);
        ImGui::Checkbox("Normals", &options.normals); ImGui::Checkbox("Velocities", &options.velocities);
        ImGui::Checkbox("Centers of mass", &options.centers_of_mass); ImGui::Checkbox("Layers", &options.layers);
        const char* mode_label = m_collider_overlay.mode() == PhysicsOverlayMode::All
            ? "All" : m_collider_overlay.mode() == PhysicsOverlayMode::Selected ? "Selected" : "None";
        ImGui::Text("Visibility: %s", mode_label);
        int category = options.category_filter, mask = options.mask_filter;
        if (ImGui::InputInt("Category filter", &category)) options.category_filter = static_cast<std::uint16_t>(std::clamp(category, 0, 65535));
        if (ImGui::InputInt("Mask filter", &mask)) options.mask_filter = static_cast<std::uint16_t>(std::clamp(mask, 0, 65535));
        ImGui::TextDisabled("Green: solid  Cyan: trigger  Gold: one-way");
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (EditorUi::toolbar_button(
            "settings", EditorIcon::Settings, "Configure snap increments")) {
        ImGui::OpenPopup("Transform Snap Settings");
    }

    if (ImGui::BeginPopup("Transform Snap Settings")) {
        ImGui::TextUnformatted("Snap increments");
        ImGui::Separator();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::DragFloat("Position", &snap.position_step, 1.0f, 0.1f, 1024.0f, "%.0f");
        ImGui::SetNextItemWidth(140.0f);
        ImGui::DragFloat("Rotation", &snap.rotation_step, 1.0f, 1.0f, 180.0f, "%.0f deg");
        float scale_snap_percent = snap.scale_step * 100.0f;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::DragFloat("Scale", &scale_snap_percent, 1.0f, 1.0f, 100.0f, "%.0f%%")) {
            snap.scale_step = scale_snap_percent / 100.0f;
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Viewport Grid Settings")) {
        ImGui::TextUnformatted("Viewport grid");
        ImGui::Separator();
        render_grid_settings_menu();
        ImGui::EndPopup();
    }

    m_transform_toolbox.end();
}

void ViewportPanel::render_scene_tabs(ScenePlayState play_state) {
    if (!m_scene_manager || m_scene_manager->get_scene_count() == 0) return;

    const auto& scenes = m_scene_manager->get_scenes();
    const bool can_activate = play_state == ScenePlayState::Edit;
    const std::size_t open_scene_count = static_cast<std::size_t>(std::count_if(
        scenes.begin(), scenes.end(), [this](const std::shared_ptr<Scene>& scene) {
            return scene && (!m_is_scene_tab_open || m_is_scene_tab_open(scene.get()));
        }
    ));

    std::optional<std::size_t> close_requested;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));
    if (ImGui::BeginTabBar(
            "##scene_tabs",
            ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (std::size_t index = 0; index < scenes.size(); ++index) {
            if (!scenes[index]) continue;
            if (m_is_scene_tab_open && !m_is_scene_tab_open(scenes[index].get())) continue;

            const bool is_active = m_scene_manager->get_active_index() == static_cast<int>(index);
            const bool dirty = m_is_scene_dirty && m_is_scene_dirty(scenes[index].get());
            const bool can_close = can_activate && open_scene_count > 1;
            bool tab_open = true;
            ImGuiTabItemFlags flags = dirty ? ImGuiTabItemFlags_UnsavedDocument
                                            : ImGuiTabItemFlags_None;
            if (is_active) flags |= ImGuiTabItemFlags_SetSelected;
            const std::string label = scenes[index]->get_name() +
                "##scene_tab_" + std::to_string(index);

            ImGui::BeginDisabled(!can_activate);
            const bool selected = ImGui::BeginTabItem(
                label.c_str(), can_close ? &tab_open : nullptr, flags
            );
            const bool requested_activation = ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
                (ImGui::IsItemFocused() &&
                 (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                  ImGui::IsKeyPressed(ImGuiKey_Space, false)));
            if (!is_active && can_activate && requested_activation && m_on_activate_scene) {
                m_on_activate_scene(index);
            }
            if (selected) ImGui::EndTabItem();
            ImGui::EndDisabled();
            if (!tab_open && can_close) close_requested = index;
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();

    if (close_requested && m_on_close_scene) m_on_close_scene(*close_requested);
}

void ViewportPanel::render_prefab_stage_header() {
    if (!m_prefab_stage || !m_prefab_stage->is_active()) return;

    const auto& metrics = EditorTheme::metrics();
    const bool dirty = m_prefab_stage->is_dirty();
    if (!ImGui::BeginTable(
            "##prefab_stage_header", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }
    ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 190.0f * metrics.scale);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (EditorUi::action_button(
            "prefab_back", EditorIcon::ArrowLeft, "Scene",
            EditorUi::ButtonTone::Neutral, true,
            ImVec2(0.0f, metrics.toolbar_height - 4.0f * metrics.scale))) {
        if (m_on_close_prefab_stage) m_on_close_prefab_stage();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Return to authoring scene");
    ImGui::SameLine();
    EditorUi::badge("PREFAB", EditorUi::BadgeTone::Accent);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(
        dirty ? EditorTheme::palette().warning : EditorTheme::palette().text,
        "%s.prefab", m_prefab_stage->get_prefab_name().c_str()
    );

    ImGui::TableSetColumnIndex(1);
    if (dirty) {
        EditorUi::badge("UNSAVED", EditorUi::BadgeTone::Warning);
        ImGui::SameLine();
    }
    if (EditorUi::action_button(
            "save_prefab", EditorIcon::Save, "Save",
            EditorUi::ButtonTone::Primary, true,
            ImVec2(0.0f, metrics.toolbar_height - 4.0f * metrics.scale))) {
        if (m_on_save_prefab) m_on_save_prefab();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save prefab asset to disk (Cmd+S)");
    ImGui::EndTable();
}

void ViewportPanel::render_transport_toolbar(ScenePlayState play_state) {
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(6.0f * metrics.scale, 3.0f * metrics.scale)
    );
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panel);
    const bool visible = ImGui::BeginChild(
        "Viewport Transport", ImVec2(0.0f, metrics.toolbar_height), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    if (visible && ImGui::BeginTable(
            "##viewport_transport", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 260.0f * metrics.scale);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        const ImVec2 button_size(0.0f, metrics.toolbar_height - 6.0f * metrics.scale);
        const bool play_starting = m_scene_manager && m_scene_manager->play_load_progress().active;
        if (play_starting) {
            static_cast<void>(EditorUi::action_button(
                "play_starting", EditorIcon::Play, "Starting...",
                EditorUi::ButtonTone::Success, false, button_size
            ));
        } else if (play_state == ScenePlayState::Edit) {
            if (EditorUi::action_button(
                    "play", EditorIcon::Play, "Play",
                    EditorUi::ButtonTone::Success, true, button_size)) {
                if (m_scene_manager) m_scene_manager->start_play();
            }
        } else {
            if (EditorUi::action_button(
                    "stop", EditorIcon::Stop, "Stop",
                    EditorUi::ButtonTone::Danger, true, button_size)) {
                if (m_scene_manager) m_scene_manager->stop_play();
            }
            ImGui::SameLine();
            if (play_state == ScenePlayState::Play) {
                if (EditorUi::action_button(
                        "pause", EditorIcon::Pause, "Pause",
                        EditorUi::ButtonTone::Neutral, true, button_size)) {
                    if (m_scene_manager) m_scene_manager->pause_play();
                }
            } else {
                if (EditorUi::action_button(
                        "resume", EditorIcon::Play, "Resume",
                        EditorUi::ButtonTone::Success, true, button_size)) {
                    if (m_scene_manager) m_scene_manager->resume_play();
                }
                ImGui::SameLine();
                if (EditorUi::action_button(
                        "step", EditorIcon::StepForward, "Step",
                        EditorUi::ButtonTone::Neutral, true, button_size)) {
                    if (m_scene_manager) m_scene_manager->step_play();
                }
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(78.0f * metrics.scale);
            float time_scale = m_scene_manager
                ? m_scene_manager->play_time_scale() : Time::get_time_scale();
            if (ImGui::DragFloat(
                    "##time_scale", &time_scale, 0.05f, 0.0f, 2.0f, "%.2fx")) {
                if (m_scene_manager) m_scene_manager->set_play_time_scale(time_scale);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Simulation time scale");
        }

        ImGui::TableSetColumnIndex(1);
        char status[160];
        if (play_starting) {
            std::snprintf(status, sizeof(status), "STARTING  |  %.0f FPS", ImGui::GetIO().Framerate);
        } else if (play_state != ScenePlayState::Edit && m_engine_renderer) {
            const RenderFrameMetrics& frame = m_engine_renderer->get_frame_metrics();
            std::snprintf(
                status, sizeof(status), "%s  |  %.0f FPS  |  %s%.0f%%  |  %dx%d",
                play_state == ScenePlayState::Play ? "PLAY" : "PAUSED",
                ImGui::GetIO().Framerate,
                frame.dynamic_resolution ? "DYN " : "",
                frame.render_scale * 100.0f,
                frame.internal_width,
                frame.internal_height
            );
        } else {
            std::snprintf(status, sizeof(status), "EDIT  |  %.0f FPS", ImGui::GetIO().Framerate);
        }
        const float status_width = ImGui::CalcTextSize(status).x;
        ImGui::SetCursorPosX(std::max(
            ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - status_width
        ));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", status);
        if (ImGui::IsItemHovered() && play_state != ScenePlayState::Edit && m_engine_renderer) {
            const RenderFrameMetrics& frame = m_engine_renderer->get_frame_metrics();
            ImGui::SetTooltip(
                frame.dynamic_resolution
                    ? "World pipeline: %dx%d\nNative Canvas/output: %dx%d\nRender scale: %.0f%%\nMeasured: %.1f FPS\nDynamic target: %.0f FPS"
                    : "World pipeline: %dx%d\nNative Canvas/output: %dx%d\nRender scale: %.0f%%",
                frame.internal_width,
                frame.internal_height,
                frame.output_width,
                frame.output_height,
                frame.render_scale * 100.0f,
                frame.measured_fps,
                frame.target_fps
            );
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void ViewportPanel::render_entity_context_menu(EntityManager& em, EntityID entity) {
    if (!em.is_alive(entity)) return;

    const auto component_label = [&](const char* label, bool present, bool& first) {
        if (!present) return;
        if (!first) ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("%s", label);
        first = false;
    };

    if (em.has_component<TagComponent>(entity)) {
        ImGui::TextUnformatted(em.get_component<TagComponent>(entity).tag.c_str());
    } else {
        ImGui::Text("Entity #%u", entity);
    }
    bool first_label = true;
    component_label("Sprite", em.has_component<SpriteComponent>(entity), first_label);
    component_label("Light", em.has_component<Light2DComponent>(entity), first_label);
    component_label("Shadow Caster", em.has_component<ShadowCaster2DComponent>(entity), first_label);
    component_label("Tilemap", em.has_component<TilemapComponent>(entity), first_label);
    component_label("Collider", em.has_component<ColliderComponent>(entity), first_label);
    component_label("RigidBody", em.has_component<RigidBodyComponent>(entity), first_label);
    ImGui::Separator();

    if (ImGui::MenuItem("Rename...")) {
        if (em.has_component<TagComponent>(entity)) {
            std::snprintf(m_context_rename_buffer.data(), m_context_rename_buffer.size(), "%s", em.get_component<TagComponent>(entity).tag.c_str());
            ImGui::OpenPopup("Rename Entity");
        }
    }

    if (em.has_component<TransformComponent>(entity)) {
        const TransformComponent before = em.get_component<TransformComponent>(entity);
        if (ImGui::MenuItem("Active", nullptr, before.visible)) {
            TransformComponent after = before;
            after.visible = !after.visible;
            if (m_command_history) {
                std::unordered_map<EntityID, TransformComponent> before_states{{entity, before}};
                std::unordered_map<EntityID, TransformComponent> after_states{{entity, after}};
                m_command_history->execute_command(std::make_unique<TransformEntitiesCommand>(
                    &em, std::move(before_states), std::move(after_states), "Toggle Entity Active"
                ));
            }
        }
    }

    if (em.has_component<SpriteComponent>(entity) && ImGui::BeginMenu("Sprite Renderer")) {
        const bool visible = em.get_component<SpriteComponent>(entity).visible;
        if (ImGui::MenuItem("Visible", nullptr, visible) && m_command_history) {
            m_command_history->execute_command(std::make_unique<ModifyEntityCommand>(
                &em,
                entity,
                [visible](EntityManager& manager, EntityID id) {
                    if (manager.has_component<SpriteComponent>(id)) manager.get_component<SpriteComponent>(id).visible = visible;
                },
                [visible](EntityManager& manager, EntityID id) {
                    if (manager.has_component<SpriteComponent>(id)) manager.get_component<SpriteComponent>(id).visible = !visible;
                },
                "Toggle Sprite Visibility"
            ));
        }
        ImGui::EndMenu();
    }

    if (em.has_component<HierarchyComponent>(entity)) {
        const auto& children = em.get_component<HierarchyComponent>(entity).children;
        if (ImGui::MenuItem("Select Children", nullptr, false, !children.empty()) && m_selection) {
            std::unordered_set<EntityID> selected_children;
            for (EntityID child : children) if (em.is_alive(child)) selected_children.insert(child);
            m_selection->set_selection(selected_children);
        }
    }

    ImGui::Separator();
    const bool can_mutate = static_cast<bool>(m_command_history);
    const bool is_locked = m_entity_state_store && m_entity_state_store->is_locked(entity);
    const bool is_visible_in_editor = !m_entity_state_store || m_entity_state_store->is_visible(entity);

    if (ImGui::MenuItem(is_locked ? "Unlock Entity" : "Lock Entity", nullptr, false, can_mutate)) {
        std::vector<EntityID> entities = (m_selection && m_selection->is_selected(entity))
            ? std::vector<EntityID>(m_selection->get_selected().begin(), m_selection->get_selected().end())
            : std::vector<EntityID>{entity};
        m_command_history->execute_command(std::make_unique<SetEditorEntityStateCommand>(
            &em, m_entity_state_store, EditorEntityStateField::Locked, entities, !is_locked
        ));
    }

    if (ImGui::MenuItem(is_visible_in_editor ? "Hide in Scene View" : "Show in Scene View", nullptr, false, can_mutate)) {
        std::vector<EntityID> entities = (m_selection && m_selection->is_selected(entity))
            ? std::vector<EntityID>(m_selection->get_selected().begin(), m_selection->get_selected().end())
            : std::vector<EntityID>{entity};
        m_command_history->execute_command(std::make_unique<SetEditorEntityStateCommand>(
            &em, m_entity_state_store, EditorEntityStateField::VisibleInEditor, entities, !is_visible_in_editor
        ));
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "Cmd/Ctrl+D", false, can_mutate) && m_selection) {
        std::vector<EntityID> entities(m_selection->get_selected().begin(), m_selection->get_selected().end());
        m_command_history->execute_command(std::make_unique<DuplicateEntitiesCommand>(&em, m_selection, std::move(entities), m_entity_state_store.get()));
    }
    bool can_delete = can_mutate;
    if (m_prefab_stage && m_prefab_stage->is_active()) {
        if (m_selection) {
            bool has_non_root = false;
            for (EntityID s_id : m_selection->get_selected()) {
                if (!m_prefab_stage->is_root_entity(s_id)) {
                    has_non_root = true;
                    break;
                }
            }
            if (!has_non_root) can_delete = false;
        } else if (m_prefab_stage->is_root_entity(entity)) {
            can_delete = false;
        }
    }

    if (ImGui::MenuItem("Delete", "Delete", false, can_delete) && m_selection) {
        std::vector<EntityID> entities;
        for (EntityID s_id : m_selection->get_selected()) {
            if (m_prefab_stage && m_prefab_stage->is_root_entity(s_id)) continue;
            entities.push_back(s_id);
        }
        if (!entities.empty()) {
            m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, std::move(entities), m_entity_state_store.get()));
        }
    }

    ImGui::Separator();
    const bool has_sel = m_selection && !m_selection->empty();
    std::string group_label = (m_selection && m_selection->count() > 1)
        ? "Group Selection (" + std::to_string(m_selection->count()) + ")"
        : "Group Selection";
    bool selection_has_prefab_root = false;
    if (m_prefab_stage && m_prefab_stage->is_active() && m_selection) {
        for (EntityID sel_id : m_selection->get_selected()) {
            if (m_prefab_stage->is_root_entity(sel_id)) {
                selection_has_prefab_root = true;
                break;
            }
        }
    }
    if (ImGui::MenuItem(group_label.c_str(), "Cmd/Ctrl+G", false, can_mutate && has_sel && !selection_has_prefab_root) && m_selection) {
        std::vector<EntityID> entities(m_selection->get_selected().begin(), m_selection->get_selected().end());
        m_command_history->execute_command(std::make_unique<GroupSelectedEntitiesCommand>(&em, m_selection, std::move(entities), "Group", m_entity_state_store.get()));
    }

    bool has_group = false;
    if (has_sel) {
        for (EntityID sel_id : m_selection->get_selected()) {
            if (em.is_alive(sel_id) && em.has_component<GroupComponent>(sel_id)) {
                has_group = true;
                break;
            }
        }
    }
    if (has_group && ImGui::MenuItem("Ungroup", "Cmd/Ctrl+Shift+G", false, can_mutate) && m_selection) {
        std::vector<EntityID> entities(m_selection->get_selected().begin(), m_selection->get_selected().end());
        m_command_history->execute_command(std::make_unique<UngroupEntitiesCommand>(&em, m_selection, std::move(entities), m_entity_state_store.get()));
    }
}

void ViewportPanel::render_rename_entity_popup(EntityManager& em) {
    if (!ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const bool valid_entity = m_context_entity != 0 && em.is_alive(m_context_entity) && em.has_component<TagComponent>(m_context_entity);
    if (!valid_entity) {
        ImGui::TextDisabled("The entity is no longer available.");
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::SetNextItemWidth(260.0f);
    ImGui::SetKeyboardFocusHere();
    const bool submitted = ImGui::InputText("Name", m_context_rename_buffer.data(), m_context_rename_buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    const bool confirm = ImGui::Button("Rename") || submitted;
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

    if (confirm) {
        const std::string before = em.get_component<TagComponent>(m_context_entity).tag;
        const std::string after = m_context_rename_buffer.data();
        if (!after.empty() && after != before && m_command_history) {
            const EntityID entity = m_context_entity;
            m_command_history->execute_command(std::make_unique<ModifyEntityCommand>(
                &em,
                entity,
                [before](EntityManager& manager, EntityID id) {
                    if (manager.has_component<TagComponent>(id)) manager.get_component<TagComponent>(id).tag = before;
                },
                [after](EntityManager& manager, EntityID id) {
                    if (manager.has_component<TagComponent>(id)) manager.get_component<TagComponent>(id).tag = after;
                },
                "Rename Entity"
            ));
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ViewportPanel::begin_render() {
    if (m_scene_manager && !m_scene_manager->is_editing() && m_project_settings) {
        m_requested_width = m_game_preview_width;
        m_requested_height = m_game_preview_height;
    } else {
        m_requested_width = m_editor_view_width;
        m_requested_height = m_editor_view_height;
    }
    if (ensure_render_target() && m_renderer && m_viewport_texture) {
        configure_scene_view();
        SDL_SetRenderTarget(m_renderer, m_viewport_texture);
        Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
        const SDL_Color clear_color = m_scene_manager ? m_scene_manager->get_clear_color() : SDL_Color{24, 28, 36, 255};
        SDL_SetRenderDrawColor(m_renderer, clear_color.r, clear_color.g, clear_color.b, clear_color.a);
        SDL_RenderClear(m_renderer);
        if (active_scene) {
            m_visibility_filter.set_entity_manager(&active_scene->get_entity_manager());
            m_scene_manager->set_render_filter(&m_visibility_filter);
        }
        const Camera2D* editor_camera = m_scene_manager ? m_scene_manager->get_render_camera(RenderViewKind::Editor) : nullptr;
        if (editor_camera && m_grid_settings.visible && m_scene_manager && m_scene_manager->is_editing()) {
            draw_viewport_grid_to_render_target(m_renderer, *editor_camera, m_gizmo.snap_settings().position_step, m_grid_settings);
        }
    }
}

void ViewportPanel::end_render() {
    if (m_renderer) {
        SDL_SetRenderTarget(m_renderer, nullptr);
    }
    ++m_render_generation;
}

void ViewportPanel::on_gui() {
    if (!is_open) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(m_name.c_str(), &is_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {

        const bool in_prefab_stage = m_prefab_stage && m_prefab_stage->is_active();
        ScenePlayState play_state = get_play_state();

        if (in_prefab_stage) {
            render_prefab_stage_header();
        } else {
            render_scene_tabs(play_state);
            render_transport_toolbar(play_state);
        }
        render_transform_toolbar();
        if (play_state != ScenePlayState::Edit) render_device_preview_toolbar();

        // --- 2. Canvas Image Rendering ---
        ImVec2 avail_size = ImGui::GetContentRegionAvail();
        if (avail_size.x > 0.0f && avail_size.y > 0.0f && m_viewport_texture) {
            const bool is_editing_view = play_state == ScenePlayState::Edit;
            ImVec2 render_size = avail_size;
            ImVec2 uv_min(0.0f, 0.0f);
            ImVec2 uv_max(1.0f, 1.0f);

            if (is_editing_view) {
                const ImVec2 framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale;
                m_editor_view_width = std::max(1, static_cast<int>(std::round(avail_size.x * framebuffer_scale.x)));
                m_editor_view_height = std::max(1, static_cast<int>(std::round(avail_size.y * framebuffer_scale.y)));
            } else if (m_project_settings) {
                const ProjectDisplaySettings& game_view = m_project_settings->display();
                const ImVec2 framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale;
                if (has_device_preview()) {
                    const DevicePreviewProfile preview = active_device_preview();
                    const PresentationMapping mapping = calculate_presentation(
                        glm::vec2(preview.width, preview.height),
                        glm::vec2(avail_size.x, avail_size.y),
                        PresentationMode::Fit
                    );
                    render_size = ImVec2(mapping.display_size.x, mapping.display_size.y);
                    m_game_preview_width = preview.width;
                    m_game_preview_height = preview.height;
                } else {
                    const glm::vec2 presentation_source = game_view.presentation == PresentationMode::Responsive
                        ? glm::vec2(std::max(m_render_width, 1), std::max(m_render_height, 1))
                        : glm::vec2(game_view.reference_width, game_view.reference_height);
                    const PresentationMapping mapping = calculate_presentation(
                        presentation_source,
                        glm::vec2(avail_size.x, avail_size.y),
                        game_view.presentation
                    );
                    render_size = ImVec2(mapping.display_size.x, mapping.display_size.y);
                    uv_min = ImVec2(mapping.uv_min.x, mapping.uv_min.y);
                    uv_max = ImVec2(mapping.uv_max.x, mapping.uv_max.y);

                    if (game_view.presentation == PresentationMode::Responsive) {
                        const float desired_width = std::max(render_size.x * framebuffer_scale.x, 1.0f);
                        const float desired_height = std::max(render_size.y * framebuffer_scale.y, 1.0f);
                        const float reference_area = static_cast<float>(game_view.reference_width) * game_view.reference_height;
                        const float desired_area = desired_width * desired_height;
                        const float quality_scale = desired_area > reference_area
                            ? std::sqrt(reference_area / desired_area)
                            : 1.0f;
                        m_game_preview_width = std::max(1, static_cast<int>(std::round(desired_width * quality_scale)));
                        m_game_preview_height = std::max(1, static_cast<int>(std::round(desired_height * quality_scale)));
                    } else {
                        const float preview_scale = std::clamp(std::max(
                            render_size.x * framebuffer_scale.x / static_cast<float>(game_view.reference_width),
                            render_size.y * framebuffer_scale.y / static_cast<float>(game_view.reference_height)
                        ), 0.25f, 1.0f);
                        m_game_preview_width = std::max(1, static_cast<int>(std::round(game_view.reference_width * preview_scale)));
                        m_game_preview_height = std::max(1, static_cast<int>(std::round(game_view.reference_height * preview_scale)));
                    }
                }
                m_requested_width = m_game_preview_width;
                m_requested_height = m_game_preview_height;
            }

            ImVec2 offset = ImVec2((avail_size.x - render_size.x) * 0.5f, (avail_size.y - render_size.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset.x, ImGui::GetCursorPosY() + offset.y));

            ImGui::Image(reinterpret_cast<ImTextureID>(m_viewport_texture), render_size, uv_min, uv_max);

            if (!is_editing_view && has_device_preview() && m_show_safe_area_overlay) {
                const ImVec2 preview_min = ImGui::GetItemRectMin();
                const ImVec2 preview_max = ImGui::GetItemRectMax();
                const float scale_x = render_size.x /
                    static_cast<float>(std::max(m_render_width, 1));
                const float scale_y = render_size.y /
                    static_cast<float>(std::max(m_render_height, 1));
                const ImVec2 safe_min{
                    preview_min.x + m_game_safe_area_render.left * scale_x,
                    preview_min.y + m_game_safe_area_render.top * scale_y
                };
                const ImVec2 safe_max{
                    preview_max.x - m_game_safe_area_render.right * scale_x,
                    preview_max.y - m_game_safe_area_render.bottom * scale_y
                };
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                draw_list->AddRect(
                    preview_min, preview_max,
                    IM_COL32(90, 100, 120, 230), 0.0f, 0, 1.0f
                );
                draw_list->AddRect(
                    safe_min, safe_max,
                    IM_COL32(255, 188, 74, 235), 0.0f, 0, 1.5f
                );
                draw_list->AddText(
                    {safe_min.x + 5.0f, safe_min.y + 4.0f},
                    IM_COL32(255, 188, 74, 220),
                    "Safe Area"
                );
            }

            m_is_hovered = ImGui::IsItemHovered();
            const bool was_viewport_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            m_is_focused = was_viewport_focused;
            const bool viewport_focus_click = m_is_hovered && !was_viewport_focused &&
                (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Middle));

            if (m_is_hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
                // An Image does not naturally take keyboard focus from an
                // active text/scalar editor. Explicitly release that editor.
                // Camera and gizmo gestures still wait for an already-focused
                // viewport, but selection is allowed on this first click.
                if (viewport_focus_click) ImGui::ClearActiveID();
                ImGui::SetWindowFocus();
                m_is_focused = true;
            }

            const bool viewport_tools_hovered = m_is_hovered && !viewport_focus_click;

            ImVec2 img_min = ImGui::GetItemRectMin();
            ImVec2 img_max = ImGui::GetItemRectMax();
            ImVec2 img_size = ImVec2(img_max.x - img_min.x, img_max.y - img_min.y);
            if (m_scene_manager && m_scene_manager->play_load_progress().active) {
                const SceneLoadProgress& progress = m_scene_manager->play_load_progress();
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                const ImVec2 panel_min(img_min.x + img_size.x * 0.22f, img_min.y + img_size.y * 0.43f);
                const ImVec2 panel_max(img_min.x + img_size.x * 0.78f, img_min.y + img_size.y * 0.61f);
                draw_list->AddRectFilled(panel_min, panel_max, IM_COL32(18, 21, 30, 238), 8.0f);
                draw_list->AddText(ImVec2(panel_min.x + 18.0f, panel_min.y + 16.0f), IM_COL32(232, 235, 245, 255), progress.label.c_str());
                if (!progress.indeterminate) {
                    char percentage[16];
                    std::snprintf(percentage, sizeof(percentage), "%d%%", static_cast<int>(progress.value * 100.0f));
                    draw_list->AddText(
                        ImVec2(panel_max.x - 52.0f, panel_min.y + 16.0f),
                        IM_COL32(170, 176, 196, 255), percentage
                    );
                }
                draw_list->AddText(ImVec2(panel_min.x + 18.0f, panel_min.y + 38.0f), IM_COL32(158, 165, 184, 255), progress.detail.c_str());
                const ImVec2 bar_min(panel_min.x + 18.0f, panel_min.y + 66.0f);
                const ImVec2 bar_max(panel_max.x - 18.0f, panel_min.y + 80.0f);
                draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(55, 60, 76, 255), 4.0f);
                if (progress.indeterminate) {
                    const float width = bar_max.x - bar_min.x;
                    const float segment = width * 0.24f;
                    const float travel = width + segment;
                    const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.65f, 1.0f);
                    const float start = bar_min.x - segment + travel * phase;
                    draw_list->PushClipRect(bar_min, bar_max, true);
                    draw_list->AddRectFilled(
                        ImVec2(start, bar_min.y), ImVec2(start + segment, bar_max.y),
                        IM_COL32(95, 76, 210, 255), 4.0f
                    );
                    draw_list->PopClipRect();
                } else {
                    draw_list->AddRectFilled(
                        bar_min,
                        ImVec2(bar_min.x + (bar_max.x - bar_min.x) * progress.value, bar_max.y),
                        IM_COL32(95, 76, 210, 255),
                        4.0f
                    );
                }
            }
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 mouse_pos = io.MousePos;

            // Transform toolbar: viewport-only shortcuts avoid stealing text input.
            if (m_is_focused && !io.WantTextInput && !m_gizmo.is_using() &&
                !m_ui_layout_gizmo.is_using() && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt) {
                if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmo.set_tool(TransformTool::Move);
                if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmo.set_tool(TransformTool::Rotate);
                if (ImGui::IsKeyPressed(ImGuiKey_S)) m_gizmo.set_tool(TransformTool::Scale);
                if (ImGui::IsKeyPressed(ImGuiKey_G)) m_grid_settings.visible = !m_grid_settings.visible;
            }

            if (img_size.x > 0.0f && img_size.y > 0.0f) {
                float norm_x = (mouse_pos.x - img_min.x) / img_size.x;
                float norm_y = (mouse_pos.y - img_min.y) / img_size.y;
                const float texture_u = uv_min.x + norm_x * (uv_max.x - uv_min.x);
                const float texture_v = uv_min.y + norm_y * (uv_max.y - uv_min.y);
                m_viewport_mouse_pos = glm::vec2(texture_u * static_cast<float>(m_render_width),
                                                 texture_v * static_cast<float>(m_render_height));
            }

            const glm::vec2 viewport_mouse_delta(
                io.MouseDelta.x * static_cast<float>(m_render_width) * (uv_max.x - uv_min.x) / std::max(img_size.x, 1.0f),
                io.MouseDelta.y * static_cast<float>(m_render_height) * (uv_max.y - uv_min.y) / std::max(img_size.y, 1.0f)
            );

            bool is_editing = m_scene_manager ? m_scene_manager->is_editing() : true;
            Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
            if (active_scene) {
                m_visibility_filter.set_entity_manager(&active_scene->get_entity_manager());
            }
            Camera2D* active_cam = m_scene_manager
                ? m_scene_manager->get_render_camera(is_editing ? RenderViewKind::Editor : RenderViewKind::Game)
                : nullptr;
            if (m_project_settings) {
                const auto& game_view = m_project_settings->display();
                m_selection_tool.set_camera_output_size(glm::vec2(game_view.reference_width, game_view.reference_height));
            }

            // --- 2.5 Drag and Drop Target for Content Browser Assets ---
            m_prefab_preview_visible = false;
            if (is_editing && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                        "CONTENT_BROWSER_ASSET",
                        ImGuiDragDropFlags_AcceptBeforeDelivery
                    )) {
                    const char* path_str = static_cast<const char*>(payload->Data);
                    if (path_str && active_scene) {
                        std::filesystem::path asset_path(path_str);
                        std::string ext = asset_path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        if ((ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") &&
                            payload->IsDelivery()) {
                            glm::vec2 world_pos(0.0f, 0.0f);
                            if (active_cam) {
                                world_pos = active_cam->screen_to_world(m_viewport_mouse_pos);
                            }

                            std::string tag_name = asset_path.stem().string();
                            if (tag_name.empty()) tag_name = "New_Sprite";

                            constexpr glm::vec2 DEFAULT_SPRITE_SIZE{64.0f, 64.0f};
                            Texture* tex = AssetManager::get().get_texture(m_renderer, path_str);
                            glm::vec2 sprite_size = tex ? glm::vec2(static_cast<float>(tex->get_width()), static_cast<float>(tex->get_height())) : DEFAULT_SPRITE_SIZE;

                            EntitySnapshot snapshot;
                            snapshot.registered_components.set(TagComponent{tag_name});
                            snapshot.registered_components.set(TransformComponent{glm::vec3(world_pos.x, world_pos.y, 0.0f)});
                            snapshot.registered_components.set(SpriteComponent{
                                tex ? tex : AssetManager::get().get_white_texture(m_renderer),
                                sprite_size,
                                SDL_Color{255, 255, 255, 255}
                            });

                            EntityManager& em = active_scene->get_entity_manager();
                            if (m_command_history) {
                                m_command_history->execute_command(
                                    std::make_unique<CreateEntityCommand>(&em, m_selection, std::move(snapshot), m_entity_state_store.get())
                                );
                            } else {
                                CreateEntityCommand cmd(&em, m_selection, std::move(snapshot), m_entity_state_store.get());
                                cmd.execute();
                            }
                        } else if (ext == ".prefab") {
                            glm::vec2 world_pos(0.0f, 0.0f);
                            if (active_cam) {
                                world_pos = active_cam->screen_to_world(m_viewport_mouse_pos);
                            }

                            // Snap to grid using unified position snap logic
                            if (is_position_snap_active()) {
                                const float step = m_gizmo.snap_settings().position_step;
                                world_pos.x = std::round(world_pos.x / step) * step;
                                world_pos.y = std::round(world_pos.y / step) * step;
                            }

                            m_prefab_preview_visible = prepare_prefab_preview(path_str, world_pos);

                            if (payload->IsDelivery()) {
                                EntityManager& em = active_scene->get_entity_manager();
                                EntityID target_parent = 0;
                                bool can_instantiate = true;
                                if (m_prefab_stage && m_prefab_stage->is_active()) {
                                    target_parent = m_prefab_stage->get_root_entity();
                                    if (target_parent == 0 || !em.is_alive(target_parent)) {
                                        ENGINE_LOG_WARN("[VIEWPORT] Cannot instantiate prefab: Prefab Stage root entity is zero or invalid.");
                                        can_instantiate = false;
                                    } else if (m_entity_state_store && m_entity_state_store->is_locked(target_parent)) {
                                        ENGINE_LOG_WARN("[VIEWPORT] Cannot instantiate prefab: Prefab Stage root entity is locked.");
                                        can_instantiate = false;
                                    }
                                }

                                if (can_instantiate) {
                                    TransformComponent desired_tr{glm::vec3(world_pos.x, world_pos.y, 0.0f)};
                                    if (m_command_history) {
                                        m_command_history->execute_command(
                                            std::make_unique<InstantiatePrefabCommand>(
                                                &em, m_selection, path_str, false, desired_tr, target_parent, m_renderer, m_entity_state_store.get()
                                            )
                                        );
                                    } else {
                                        InstantiatePrefabCommand cmd(&em, m_selection, path_str, false, desired_tr, target_parent, m_renderer, m_entity_state_store.get());
                                        cmd.execute();
                                    }
                                }
                                m_prefab_preview_visible = false;
                            }
                        } else if (ext == ".scene" && payload->IsDelivery()) {

                            if (m_on_load_scene) {
                                m_on_load_scene(path_str);
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // --- 3. Viewport Camera Navigation (Pan / Zoom / Focus 'F') ---
            if (is_editing && active_cam && active_scene && m_selection) {
                m_camera_controller.handle_input(active_scene, active_cam, *m_selection, viewport_tools_hovered, m_is_focused, m_viewport_mouse_pos, viewport_mouse_delta);
            }

            // Every editor decoration shares the exact image bounds. Individual
            // overlays may add tighter clips, but none may draw into dock tabs,
            // rulers, adjacent panels, or the console below the viewport.
            ImDrawList* viewport_draw_list = ImGui::GetWindowDrawList();
            viewport_draw_list->PushClipRect(
                img_min,
                ImVec2(img_min.x + img_size.x, img_min.y + img_size.y),
                true
            );
            const bool is_level_composing = is_editing && m_level_composer_panel &&
                m_level_composer_panel->is_placement_active();
            if ((!is_editing || !active_scene || !active_cam) &&
                m_level_composer_tool.is_stroking()) {
                m_level_composer_tool.cancel();
            }
            if (is_editing && active_cam && m_level_composer_panel) {
                const EntityID required_root =
                    m_prefab_stage && m_prefab_stage->is_active()
                    ? m_prefab_stage->get_root_entity()
                    : 0;
                m_level_composer_tool.handle(
                    active_scene,
                    *m_level_composer_panel,
                    *active_cam,
                    img_min,
                    img_size,
                    viewport_tools_hovered,
                    m_renderer,
                    m_selection,
                    m_command_history.get(),
                    m_entity_state_store.get(),
                    [this](
                        const std::string& prefab_path,
                        const std::vector<glm::vec2>& world_positions,
                        float rotation_degrees,
                        float depth_z
                    ) {
                        m_prefab_preview_visible = prepare_prefab_preview(
                            prefab_path, world_positions, rotation_degrees, depth_z
                        );
                    },
                    required_root
                );
            }
            if (active_cam) render_prefab_preview(*active_cam, img_min, img_size);

            const bool is_guide_interaction = is_editing && active_cam
                ? m_reference_overlay.render(*active_cam, img_min, img_size, m_viewport_mouse_pos, m_gizmo.snap_settings().position_step, viewport_tools_hovered)
                : false;

            // A short right-click opens contextual actions; dragging with the
            // right or middle mouse button pans the camera.
            const ImVec2 right_drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            const bool is_context_click = ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                right_drag.x * right_drag.x + right_drag.y * right_drag.y < 16.0f;
            if (is_editing && viewport_tools_hovered && is_context_click) {
                m_context_entity = active_scene && active_cam
                    ? m_selection_tool.pick_entity(active_scene->get_entity_manager(), active_cam->screen_to_world(m_viewport_mouse_pos), active_cam, &m_visibility_filter)
                    : 0;
                if (m_context_entity != 0 && m_selection && !m_selection->is_selected(m_context_entity)) {
                    m_selection->select(m_context_entity);
                }
                ImGui::OpenPopup("Viewport Context Menu");
            }
            // The viewport itself is edge-to-edge; contextual menus need their
            // own comfortable inset instead of inheriting that zero padding.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            if (ImGui::BeginPopup("Viewport Context Menu")) {
                if (m_context_entity != 0 && active_scene && active_scene->get_entity_manager().is_alive(m_context_entity)) {
                    render_entity_context_menu(active_scene->get_entity_manager(), m_context_entity);
                    if (m_selection && m_selection->count() >= 2) {
                        ImGui::Separator();
                        if (ImGui::BeginMenu("Arrange Selection")) {
                            render_arrange_menu();
                            ImGui::EndMenu();
                        }
                    }
                } else if (ImGui::BeginMenu("Grid")) {
                    render_grid_settings_menu();
                    ImGui::EndMenu();
                }

                if (m_context_entity == 0 && m_selection && m_selection->count() >= 2) {
                    ImGui::Separator();
                    if (ImGui::BeginMenu("Arrange Selection")) {
                        render_arrange_menu();
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();

            if (active_scene) render_rename_entity_popup(active_scene->get_entity_manager());

            // Collider outlines are editor-owned and work for every generic
            // scene in both Edit and Play. Selected entities are always shown;
            // the toolbar toggle reveals the complete physics layout.
            if (active_scene && active_cam) {
                PhysicsSystem::set_debug_options(m_collider_overlay.effective_debug_options());
                EditorSelection runtime_selection;
                const EditorSelection* overlay_selection = m_selection.get();
                if (!is_editing && m_scene_manager && m_scene_manager->runtime_selected_entity() != 0) {
                    runtime_selection.select(m_scene_manager->runtime_selected_entity(), false);
                    overlay_selection = &runtime_selection;
                }
                m_collider_overlay.render(
                    active_scene->get_entity_manager(),
                    overlay_selection,
                    *active_cam,
                    img_min,
                    img_size,
                    uv_min,
                    uv_max,
                    &m_visibility_filter
                );
                m_lighting_overlay.render(
                    active_scene->get_entity_manager(),
                    m_selection.get(),
                    *active_cam,
                    img_min,
                    img_size,
                    uv_min,
                    uv_max,
                    &m_visibility_filter
                );
            }

            // --- 4. Camera Frustum Gizmo (Wireframe Game Frame) ---
            if (is_editing && active_cam && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                
                glm::vec2 cam_pos(0.0f, 0.0f);
                float cam_zoom = 1.0f;
                float cam_rotation = 0.0f;
                const ProjectDisplaySettings output = m_project_settings
                    ? m_project_settings->display()
                    : ProjectDisplaySettings{};
                glm::vec2 vp_sz(static_cast<float>(output.reference_width), static_cast<float>(output.reference_height));
                EntityID displayed_camera = CameraSystem::find_primary(em);
                em.for_each<CameraComponent>([&](EntityID id, const CameraComponent&) {
                    if (m_selection && m_selection->is_selected(id)) displayed_camera = id;
                });
                const bool found_camera = displayed_camera != 0 && em.has_component<TransformComponent>(displayed_camera);
                const bool is_cam_selected = found_camera && m_selection && m_selection->is_selected(displayed_camera);
                if (found_camera) {
                    const auto& component = em.get_component<CameraComponent>(displayed_camera);
                    const auto& transform = em.get_component<TransformComponent>(displayed_camera);
                    cam_pos = glm::vec2(transform.world_position);
                    cam_zoom = component.zoom;
                    cam_rotation = transform.world_rotation;
                }

                em.for_each<CameraComponent, TransformComponent>([&](EntityID id, const CameraComponent& camera_component, const TransformComponent& transform) {
                    const glm::vec2 icon_viewport = active_cam->world_to_screen(transform.world_position);
                    const ImVec2 icon_center(
                        img_min.x + icon_viewport.x * img_size.x / active_cam->get_viewport_size().x,
                        img_min.y + icon_viewport.y * img_size.y / active_cam->get_viewport_size().y
                    );
                    const bool selected = m_selection && m_selection->is_selected(id);
                    const ImU32 color = selected
                        ? IM_COL32(116, 170, 255, 255)
                        : (camera_component.is_primary ? IM_COL32(110, 205, 155, 230) : IM_COL32(154, 164, 184, 210));
                    draw_editor_icon(ImGui::GetWindowDrawList(), EditorIcon::Camera, icon_center, 18.0f, color);
                });

                if (found_camera) {
                    const float half_w = (vp_sz.x * 0.5f) / std::max(cam_zoom, 0.001f);
                    const float half_h = (vp_sz.y * 0.5f) / std::max(cam_zoom, 0.001f);
                    const float radians = glm::radians(-cam_rotation);
                    const float cosine = std::cos(radians);
                    const float sine = std::sin(radians);
                    const std::array<glm::vec2, 4> local{{
                        {-half_w, half_h}, {half_w, half_h}, {half_w, -half_h}, {-half_w, -half_h}
                    }};
                    ImVec2 screen_corners[4];
                    ImVec2 label_position(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
                    for (size_t index = 0; index < local.size(); ++index) {
                        const glm::vec2 rotated(
                            local[index].x * cosine - local[index].y * sine,
                            local[index].x * sine + local[index].y * cosine
                        );
                        const glm::vec2 viewport_point = active_cam->world_to_screen(glm::vec3(cam_pos + rotated, 0.0f));
                        screen_corners[index] = ImVec2(
                            img_min.x + viewport_point.x * img_size.x / active_cam->get_viewport_size().x,
                            img_min.y + viewport_point.y * img_size.y / active_cam->get_viewport_size().y
                        );
                        label_position.x = std::min(label_position.x, screen_corners[index].x);
                        label_position.y = std::min(label_position.y, screen_corners[index].y);
                    }

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImU32 frame_col = is_cam_selected ? IM_COL32(59, 130, 246, 220) : IM_COL32(148, 163, 184, 80);
                    const float frame_thick = is_cam_selected ? 2.0f : 1.0f;
                    dl->AddPolyline(screen_corners, 4, frame_col, ImDrawFlags_Closed, frame_thick);

                    char cam_label[64];
                    snprintf(cam_label, sizeof(cam_label), "Camera Frame (%.0fx%.0f | Zoom: %.2fx)", vp_sz.x, vp_sz.y, cam_zoom);
                    dl->AddText(ImVec2(label_position.x + 8.0f, label_position.y + 8.0f), is_cam_selected ? IM_COL32(96, 165, 250, 255) : IM_COL32(148, 163, 184, 150), cam_label);
                }
            }

            // --- 5. Selection Outlines & Centroid Transform Gizmo ---
            bool is_tilemap_painting = !is_level_composing &&
                (m_palette_panel && m_palette_panel->is_painting_active());
            const bool is_level_authoring = is_level_composing ||
                m_level_composer_tool.is_stroking();
            if (is_editing && active_scene && !is_tilemap_painting &&
                m_tilemap_painting_tool.has_active_stroke()) {
                m_tilemap_painting_tool.cancel_active_stroke(active_scene);
            }
            bool is_gizmo_active = false;

            // UI rectangles are themselves the move surface. Pick the visible
            // UI element before resolving its gizmo so an unselected Text,
            // Image, or Button can be selected directly and dragged in the
            // same gesture. This also prevents a previously selected,
            // overlapping UI rectangle from swallowing the click.
            if (is_editing && active_scene && active_cam && m_selection &&
                !is_tilemap_painting && !is_level_authoring &&
                !is_guide_interaction && m_is_hovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !io.KeyCtrl && !io.KeyShift && !io.KeyAlt) {
                EntityManager& em = active_scene->get_entity_manager();
                const EntityID hit = m_selection_tool.pick_entity(
                    em,
                    active_cam->screen_to_world(m_viewport_mouse_pos),
                    active_cam,
                    &m_visibility_filter
                );
                if (hit != 0 && em.has_component<UILayoutComponent>(hit) &&
                    !m_selection->is_selected(hit)) {
                    m_selection->select(hit);
                }
            }

            // --- 5. Selection Outlines & Transform Gizmo (Only when NOT actively painting tiles) ---
            if (is_editing && active_scene && active_cam && m_selection &&
                !m_selection->empty() && !is_tilemap_painting && !is_level_authoring) {
                // A. Selection Outlines
                m_selection_tool.render_selection_outlines(
                    active_scene->get_entity_manager(),
                    *m_selection,
                    *active_cam,
                    img_min,
                    img_size,
                    &m_visibility_filter
                );

                // B. UI layout uses Canvas pixels and anchors instead of world transforms.
                const bool ui_gizmo_active = m_ui_layout_gizmo.draw_gizmo(
                    active_scene->get_entity_manager(),
                    m_selection->get_selected(),
                    *active_cam,
                    img_min,
                    img_size,
                    viewport_tools_hovered,
                    is_position_snap_active(),
                    m_gizmo.snap_settings().position_step,
                    m_gizmo.snap_settings().rotation_step,
                    m_command_history.get(),
                    &m_visibility_filter
                );

                // C. World Transform gizmo ignores UI layout entities.
                const bool transform_gizmo_active = m_gizmo.draw_gizmo(
                    active_scene->get_entity_manager(),
                    m_selection->get_selected(),
                    *active_cam,
                    img_min,
                    img_size,
                    viewport_tools_hovered,
                    m_command_history.get(),
                    &m_visibility_filter
                );
                is_gizmo_active = ui_gizmo_active || transform_gizmo_active;
            }

            // --- 6. Marquee Box Selection & Raycast Picking (Active when NOT painting) ---
            if (is_editing && active_cam && m_selection &&
                !is_tilemap_painting && !is_level_authoring) {
                m_selection_tool.handle_selection_input(
                    active_scene,
                    *active_cam,
                    *m_selection,
                    img_min,
                    img_size,
                    m_is_hovered,
                    is_gizmo_active || m_gizmo.is_hovered() || m_gizmo.is_using() ||
                        m_ui_layout_gizmo.is_hovered() || m_ui_layout_gizmo.is_using() ||
                        is_guide_interaction,
                    &m_visibility_filter
                );
            }

            // --- 6.5 Interactive Tilemap Painting Tool (Brush / Box Fill / Eraser) ---
            if (is_editing && active_scene && active_cam && is_tilemap_painting) {
                const EntityID tilemap_entity = m_palette_panel->get_selected_tilemap_entity();

                m_tilemap_painting_tool.handle_viewport_painting(
                    active_scene,
                    tilemap_entity,
                    *m_palette_panel,
                    *active_cam,
                    img_min,
                    img_size,
                    viewport_tools_hovered,
                    m_command_history.get()
                );
            }

            viewport_draw_list->PopClipRect();

            // --- 7. Duplicate Selected Entities ---
            if (is_editing && (m_is_focused || m_is_hovered) && !io.WantTextInput && ShortcutManager::get().is_triggered("editor.duplicate") && m_selection && !m_selection->empty() && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                if (m_command_history) {
                    std::vector<EntityID> entities(m_selection->get_selected().begin(), m_selection->get_selected().end());
                    m_command_history->execute_command(std::make_unique<DuplicateEntitiesCommand>(&em, m_selection, std::move(entities), m_entity_state_store.get()));
                }
            }

            // --- 8. Delete Selected Entities ---
            if (is_editing && (m_is_focused || m_is_hovered) && !io.WantTextInput && ShortcutManager::get().is_triggered("editor.delete") && m_selection && !m_selection->empty() && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                std::vector<EntityID> to_delete;
                for (EntityID s_id : m_selection->get_selected()) {
                    if (m_prefab_stage && m_prefab_stage->is_root_entity(s_id)) continue;
                    if (em.is_alive(s_id)) to_delete.push_back(s_id);
                }

                if (!to_delete.empty()) {
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, to_delete, m_entity_state_store.get()));
                    }
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
