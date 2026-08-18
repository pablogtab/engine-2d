#include "ViewportPanel.hpp"
#include "TilemapPalettePanel.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include "../commands/EntityCommands.hpp"
#include "../tools/EditorIcons.hpp"
#include "../../scene/GenericScene.hpp"
#include "../../core/Log.hpp"
#include "../../core/Time.hpp"
#include "../../ecs/Components.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

    if (m_scene_manager) {
        const RenderViewKind view = m_scene_manager->is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
        m_scene_manager->resize_view(view, m_render_width, m_render_height, 1.0f);
    }
    return true;
}

ViewportPanel::~ViewportPanel() {
    if (m_viewport_texture) {
        SDL_DestroyTexture(m_viewport_texture);
        m_viewport_texture = nullptr;
    }
}

void ViewportPanel::render_grid_settings_menu() {
    ImGui::Checkbox("Show grid (G)", &m_grid_settings.visible);
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

void ViewportPanel::render_transform_toolbar() {
    if (!m_transform_toolbox.begin("Transform Toolbar")) {
        m_transform_toolbox.end();
        return;
    }

    ImGui::SetCursorPosX(18.0f);
    if (editor_icon_button("move", EditorIcon::Move, "Move (W)", m_gizmo.get_tool() == TransformTool::Move)) {
        m_gizmo.set_tool(TransformTool::Move);
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (editor_icon_button("rotate", EditorIcon::Rotate, "Rotate (R)", m_gizmo.get_tool() == TransformTool::Rotate)) {
        m_gizmo.set_tool(TransformTool::Rotate);
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (editor_icon_button("scale", EditorIcon::Scale, "Scale (S)", m_gizmo.get_tool() == TransformTool::Scale)) {
        m_gizmo.set_tool(TransformTool::Scale);
    }
    ImGui::SameLine(0.0f, 5.0f);

    const bool multiple_selection = m_selection && m_selection->get_selected().size() > 1;
    if (multiple_selection) m_gizmo.set_space(GizmoSpace::World);
    const GizmoSpace space = m_gizmo.get_space();
    const char* space_tooltip = multiple_selection
        ? "Space: World (multiple selections use World)"
        : (space == GizmoSpace::World ? "Space: World (click for Local)" : "Space: Local (click for World)");
    const EditorIcon space_icon = space == GizmoSpace::World ? EditorIcon::World : EditorIcon::Local;
    if (editor_icon_button("space", space_icon, space_tooltip, true) && !multiple_selection) {
        m_gizmo.set_space(space == GizmoSpace::Local ? GizmoSpace::World : GizmoSpace::Local);
    }
    ImGui::SameLine(0.0f, 1.0f);

    auto& snap = m_gizmo.snap_settings();
    if (editor_icon_button("grid", EditorIcon::Grid, "Grid settings (G toggles visibility)", m_grid_settings.visible)) {
        ImGui::OpenPopup("Viewport Grid Settings");
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (editor_icon_button("snap", EditorIcon::Snap, "Snap (Shift temporarily inverts it)", snap.enabled)) {
        snap.enabled = !snap.enabled;
    }
    ImGui::SameLine(0.0f, 1.0f);
    if (editor_icon_button("settings", EditorIcon::Settings, "Configure snap increments")) {
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
    if (ImGui::MenuItem("Duplicate", "Cmd/Ctrl+D", false, can_mutate) && m_selection) {
        std::vector<EntityID> entities(m_selection->get_selected().begin(), m_selection->get_selected().end());
        m_command_history->execute_command(std::make_unique<DuplicateEntitiesCommand>(&em, m_selection, std::move(entities)));
    }
    if (ImGui::MenuItem("Delete", "Delete", false, can_mutate) && m_selection) {
        std::vector<EntityID> entities(m_selection->get_selected().begin(), m_selection->get_selected().end());
        m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, std::move(entities)));
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
        const GameViewSettings& game_view = m_project_settings->game_view();
        m_requested_width = game_view.width;
        m_requested_height = game_view.height;
    } else {
        m_requested_width = m_editor_view_width;
        m_requested_height = m_editor_view_height;
    }
    if (ensure_render_target() && m_renderer && m_viewport_texture) {
        SDL_SetRenderTarget(m_renderer, m_viewport_texture);
        SDL_SetRenderDrawColor(m_renderer, 15, 18, 25, 255);
        SDL_RenderClear(m_renderer);
        Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
        if (m_grid_settings.visible && m_scene_manager && m_scene_manager->is_editing()) {
            if (auto* generic_scene = dynamic_cast<GenericScene*>(active_scene)) {
                draw_viewport_grid_to_render_target(m_renderer, generic_scene->get_editor_camera(), m_gizmo.snap_settings().position_step, m_grid_settings);
            }
        }
    }
}

void ViewportPanel::end_render() {
    if (m_renderer) {
        SDL_SetRenderTarget(m_renderer, nullptr);
    }
}

void ViewportPanel::on_gui() {
    if (!is_open) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(m_name.c_str(), &is_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        // --- 1. Top Transport Controls (Play / Pause / Stop) ---
        ImGui::SetCursorPos(ImVec2(10.0f, 32.0f));
        ScenePlayState play_state = get_play_state();

        if (play_state == ScenePlayState::Edit) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.38f, 0.85f));
            if (ImGui::Button(" > Play ", ImVec2(75, 24))) {
                if (m_scene_manager) m_scene_manager->start_play();
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.85f));
            if (ImGui::Button(" [] Stop ", ImVec2(75, 24))) {
                if (m_scene_manager) m_scene_manager->stop_play();
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (play_state == ScenePlayState::Play) {
                if (ImGui::Button(" || Pause ", ImVec2(75, 24))) {
                    if (m_scene_manager) m_scene_manager->pause_play();
                }
            } else {
                if (ImGui::Button(" > Resume ", ImVec2(75, 24))) {
                    if (m_scene_manager) m_scene_manager->resume_play();
                }
            }
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        if (m_scene_manager && m_scene_manager->get_scene_count() > 0) {
            ImGui::SetNextItemWidth(260.0f);
            Scene* current_sc = m_scene_manager->get_active_scene();
            std::string preview_name = current_sc ? ("🎬 " + current_sc->get_name()) : "Select Scene";
            if (ImGui::BeginCombo("##viewport_scene_select", preview_name.c_str())) {
                const auto& scenes = m_scene_manager->get_scenes();
                for (size_t i = 0; i < scenes.size(); ++i) {
                    if (!scenes[i]) continue;
                    bool is_sel = (m_scene_manager->get_active_index() == static_cast<int>(i));
                    std::string item_name = std::to_string(i + 1) + ". " + scenes[i]->get_name();
                    if (ImGui::Selectable(item_name.c_str(), is_sel)) {
                        m_scene_manager->set_active_scene(i);
                    }
                    if (is_sel) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 250.0f);
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "Mode: %s | FPS: %.0f", 
            (play_state == ScenePlayState::Edit ? "EDIT" : (play_state == ScenePlayState::Play ? "PLAY" : "PAUSED")),
            ImGui::GetIO().Framerate);

        ImGui::Spacing();
        render_transform_toolbar();

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
                const GameViewSettings& game_view = m_project_settings->game_view();
                m_requested_width = game_view.width;
                m_requested_height = game_view.height;
                const float target_aspect = game_view.aspect_ratio();
                const float available_aspect = avail_size.x / avail_size.y;
                if (game_view.scaling == GameViewScaling::Fit) {
                    if (available_aspect > target_aspect) {
                        render_size = ImVec2(avail_size.y * target_aspect, avail_size.y);
                    } else {
                        render_size = ImVec2(avail_size.x, avail_size.x / target_aspect);
                    }
                } else if (game_view.scaling == GameViewScaling::Fill) {
                    if (available_aspect > target_aspect) {
                        const float visible_height = target_aspect / available_aspect;
                        uv_min.y = (1.0f - visible_height) * 0.5f;
                        uv_max.y = 1.0f - uv_min.y;
                    } else {
                        const float visible_width = available_aspect / target_aspect;
                        uv_min.x = (1.0f - visible_width) * 0.5f;
                        uv_max.x = 1.0f - uv_min.x;
                    }
                }
            }

            ImVec2 offset = ImVec2((avail_size.x - render_size.x) * 0.5f, (avail_size.y - render_size.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset.x, ImGui::GetCursorPosY() + offset.y));

            ImGui::Image(reinterpret_cast<ImTextureID>(m_viewport_texture), render_size, uv_min, uv_max);

            m_is_hovered = ImGui::IsItemHovered();
            m_is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

            if (m_is_hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
                ImGui::SetWindowFocus();
                m_is_focused = true;
            }

            ImVec2 img_min = ImGui::GetItemRectMin();
            ImVec2 img_max = ImGui::GetItemRectMax();
            ImVec2 img_size = ImVec2(img_max.x - img_min.x, img_max.y - img_min.y);
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 mouse_pos = io.MousePos;

            // Transform toolbar: viewport-only shortcuts avoid stealing text input.
            if (m_is_focused && !io.WantTextInput && !m_gizmo.is_using() && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt) {
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
            auto* generic_scene = dynamic_cast<GenericScene*>(active_scene);
            Camera2D* active_cam = generic_scene ? &generic_scene->get_editor_camera() : nullptr;

            // --- 3. Viewport Camera Navigation (Pan / Zoom / Focus 'F') ---
            if (is_editing && active_cam && generic_scene && m_selection) {
                m_camera_controller.handle_input(generic_scene, *m_selection, m_is_hovered, m_is_focused, m_viewport_mouse_pos, viewport_mouse_delta);
            }

            const bool is_guide_interaction = is_editing && active_cam
                ? m_reference_overlay.render(*active_cam, img_min, img_size, m_viewport_mouse_pos, m_gizmo.snap_settings().position_step, m_is_hovered)
                : false;

            // A short right-click opens contextual actions; dragging with the
            // right or middle mouse button pans the camera.
            const ImVec2 right_drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            const bool is_context_click = ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                right_drag.x * right_drag.x + right_drag.y * right_drag.y < 16.0f;
            if (is_editing && m_is_hovered && is_context_click) {
                m_context_entity = active_scene && active_cam
                    ? m_selection_tool.pick_entity(active_scene->get_entity_manager(), active_cam->screen_to_world(m_viewport_mouse_pos))
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

            // --- 4. Camera Frustum Gizmo (Wireframe Game Frame) ---
            if (is_editing && active_cam && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                
                glm::vec2 cam_pos(0.0f, 0.0f);
                float cam_zoom = 1.0f;
                const GameViewSettings output = m_project_settings
                    ? m_project_settings->game_view()
                    : GameViewSettings{};
                glm::vec2 vp_sz(static_cast<float>(output.width), static_cast<float>(output.height));
                bool is_cam_selected = false;
                bool found_camera = false;

                em.for_each<CameraComponent, TransformComponent>([&](EntityID id, const CameraComponent& cc, const TransformComponent& tc) {
                    bool sel = m_selection && m_selection->is_selected(id);
                    if (!found_camera || sel || cc.is_primary) {
                        cam_pos = glm::vec2(tc.position.x, tc.position.y);
                        cam_zoom = cc.zoom;
                        is_cam_selected = sel;
                        found_camera = true;
                    }
                });

                float half_w = (vp_sz.x * 0.5f) / (cam_zoom > 0.001f ? cam_zoom : 1.0f);
                float half_h = (vp_sz.y * 0.5f) / (cam_zoom > 0.001f ? cam_zoom : 1.0f);

                glm::vec3 p_min_world(cam_pos.x - half_w, cam_pos.y - half_h, 0.0f);
                glm::vec3 p_max_world(cam_pos.x + half_w, cam_pos.y + half_h, 0.0f);
                glm::vec2 s_min = active_cam->world_to_screen(p_min_world);
                glm::vec2 s_max = active_cam->world_to_screen(p_max_world);

                float scale_x = img_size.x / active_cam->get_viewport_size().x;
                float scale_y = img_size.y / active_cam->get_viewport_size().y;

                ImVec2 screen_rect_min(img_min.x + s_min.x * scale_x, img_min.y + s_max.y * scale_y);
                ImVec2 screen_rect_max(img_min.x + s_max.x * scale_x, img_min.y + s_min.y * scale_y);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 frame_col = is_cam_selected ? IM_COL32(59, 130, 246, 220) : IM_COL32(148, 163, 184, 80);
                float frame_thick = is_cam_selected ? 2.0f : 1.0f;
                dl->AddRect(screen_rect_min, screen_rect_max, frame_col, 0.0f, 0, frame_thick);
                
                char cam_label[64];
                snprintf(cam_label, sizeof(cam_label), "🎥 Camera Frame (%.0fx%.0f | Zoom: %.2fx)", vp_sz.x, vp_sz.y, cam_zoom);
                dl->AddText(ImVec2(screen_rect_min.x + 8.0f, screen_rect_min.y + 8.0f), is_cam_selected ? IM_COL32(96, 165, 250, 255) : IM_COL32(148, 163, 184, 150), cam_label);
            }

            // --- 5. Selection Outlines & Centroid Transform Gizmo ---
            bool is_tilemap_painting = (m_palette_panel && m_palette_panel->is_painting_active());
            bool is_gizmo_active = false;

            // --- 5. Selection Outlines & Transform Gizmo (Only when NOT actively painting tiles) ---
            if (is_editing && active_scene && active_cam && m_selection && !m_selection->empty() && !is_tilemap_painting) {
                // A. Selection Outlines
                m_selection_tool.render_selection_outlines(
                    active_scene->get_entity_manager(),
                    *m_selection,
                    *active_cam,
                    img_min,
                    img_size
                );

                // B. Transform Gizmo (Records to Command History on Drag Release)
                is_gizmo_active = m_gizmo.draw_gizmo(
                    active_scene->get_entity_manager(),
                    m_selection->get_selected(),
                    *active_cam,
                    img_min,
                    img_size,
                    m_is_hovered,
                    m_command_history.get()
                );
            }

            // --- 6. Marquee Box Selection & Raycast Picking (Active when NOT painting) ---
            if (is_editing && active_cam && m_selection && !is_tilemap_painting) {
                m_selection_tool.handle_selection_input(
                    active_scene,
                    *active_cam,
                    *m_selection,
                    img_min,
                    img_size,
                    m_is_hovered,
                    is_gizmo_active || m_gizmo.is_hovered() || m_gizmo.is_using() || is_guide_interaction
                );
            }

            // --- 6.5 Interactive Tilemap Painting Tool (Brush / Box Fill / Eraser) ---
            if (is_editing && active_scene && active_cam && is_tilemap_painting) {
                EntityManager& em = active_scene->get_entity_manager();
                EntityID tilemap_entity = 0;

                // 1. Check if currently selected entity has a TilemapComponent
                if (m_selection && !m_selection->empty()) {
                    EntityID primary = m_selection->get_primary();
                    if (em.is_alive(primary) && em.has_component<TilemapComponent>(primary)) {
                        tilemap_entity = primary;
                    }
                }

                // 2. Fallback: Find first entity in active scene with a TilemapComponent
                if (tilemap_entity == 0) {
                    em.for_each<TilemapComponent>([&](EntityID id, const TilemapComponent&) {
                        if (tilemap_entity == 0) tilemap_entity = id;
                    });
                }

                // 3. Fallback: If no Tilemap entity exists yet, create one automatically!
                if (tilemap_entity == 0) {
                    tilemap_entity = em.create_entity();
                    em.add_component<TagComponent>(tilemap_entity, TagComponent{"Tilemap"});
                    em.add_component<TransformComponent>(tilemap_entity, TransformComponent{glm::vec3(0.0f)});
                    em.add_component<TilemapComponent>(tilemap_entity, TilemapComponent{});
                }

                if (m_selection && m_selection->empty()) {
                    m_selection->select(tilemap_entity);
                }

                m_tilemap_painting_tool.handle_viewport_painting(
                    active_scene,
                    tilemap_entity,
                    *m_palette_panel,
                    *active_cam,
                    img_min,
                    img_size,
                    m_is_hovered,
                    m_command_history.get()
                );
            }

            // --- 7. Duplicate Selected Entities ---
            if (is_editing && m_is_hovered && ShortcutManager::get().is_triggered("editor.duplicate") && m_selection && !m_selection->empty() && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                if (m_command_history) {
                    std::vector<EntityID> entities(m_selection->get_selected().begin(), m_selection->get_selected().end());
                    m_command_history->execute_command(std::make_unique<DuplicateEntitiesCommand>(&em, m_selection, std::move(entities)));
                }
            }

            // --- 8. Delete Selected Entities ---
            if (is_editing && (m_is_focused || m_is_hovered) && !io.WantTextInput && ShortcutManager::get().is_triggered("editor.delete") && m_selection && !m_selection->empty() && active_scene) {
                EntityManager& em = active_scene->get_entity_manager();
                std::vector<EntityID> to_delete;
                for (EntityID s_id : m_selection->get_selected()) {
                    if (em.is_alive(s_id)) to_delete.push_back(s_id);
                }

                if (!to_delete.empty()) {
                    if (m_command_history) {
                        m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, to_delete));
                    }
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
