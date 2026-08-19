#include "ViewportPanel.hpp"
#include "TilemapPalettePanel.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/HierarchyCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../commands/EditorEntityStateCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../tools/EditorIcons.hpp"
#include "../../scene/GenericScene.hpp"
#include "../../core/Log.hpp"
#include "../../core/Time.hpp"
#include "../../ecs/CameraSystem.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../core/AssetManager.hpp"
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
        m_scene_manager->configure_view(view, {
            m_render_width, m_render_height,
            game_view.reference_width, game_view.reference_height
        });
    } else {
        m_scene_manager->configure_view(view, {
            m_render_width, m_render_height,
            m_render_width, m_render_height
        });
    }
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
    if (editor_icon_button(
            "colliders",
            EditorIcon::Collider,
            m_collider_overlay.show_all()
                ? "Collider outlines: all (click to show selected only)"
                : "Collider outlines: selected only (click to show all)",
            m_collider_overlay.show_all()
        )) {
        m_collider_overlay.toggle_show_all();
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
    if (ImGui::MenuItem("Delete", "Delete", false, can_mutate) && m_selection) {
        std::vector<EntityID> entities(m_selection->get_selected().begin(), m_selection->get_selected().end());
        m_command_history->execute_command(std::make_unique<DeleteEntitiesCommand>(&em, m_selection, std::move(entities), m_entity_state_store.get()));
    }

    ImGui::Separator();
    const bool has_sel = m_selection && !m_selection->empty();
    std::string group_label = (m_selection && m_selection->count() > 1)
        ? "Group Selection (" + std::to_string(m_selection->count()) + ")"
        : "Group Selection";
    if (ImGui::MenuItem(group_label.c_str(), "Cmd/Ctrl+G", false, can_mutate && has_sel) && m_selection) {
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
        SDL_SetRenderDrawColor(m_renderer, 15, 18, 25, 255);
        SDL_RenderClear(m_renderer);
        Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
        if (auto* generic_scene = dynamic_cast<GenericScene*>(active_scene)) {
            m_visibility_filter.set_entity_manager(&generic_scene->get_entity_manager());
            generic_scene->set_render_filter(&m_visibility_filter);
            if (m_grid_settings.visible && m_scene_manager && m_scene_manager->is_editing()) {
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
                const ProjectDisplaySettings& game_view = m_project_settings->display();
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

                const ImVec2 framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale;
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
                m_requested_width = m_game_preview_width;
                m_requested_height = m_game_preview_height;
            }

            ImVec2 offset = ImVec2((avail_size.x - render_size.x) * 0.5f, (avail_size.y - render_size.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset.x, ImGui::GetCursorPosY() + offset.y));

            ImGui::Image(reinterpret_cast<ImTextureID>(m_viewport_texture), render_size, uv_min, uv_max);

            m_is_hovered = ImGui::IsItemHovered();
            const bool was_viewport_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            m_is_focused = was_viewport_focused;
            const bool viewport_focus_click = m_is_hovered && !was_viewport_focused &&
                (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Middle));

            if (m_is_hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
                // An Image does not naturally take keyboard focus from an
                // active text/scalar editor. Explicitly release that editor,
                // then consume this click as focus transfer so it cannot also
                // start a gizmo, marquee or painting gesture with stale state.
                if (viewport_focus_click) ImGui::ClearActiveID();
                ImGui::SetWindowFocus();
                m_is_focused = true;
            }

            const bool viewport_tools_hovered = m_is_hovered && !viewport_focus_click;

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
            if (generic_scene) {
                m_visibility_filter.set_entity_manager(&generic_scene->get_entity_manager());
            }
            Camera2D* active_cam = generic_scene ? &generic_scene->get_active_camera(is_editing) : nullptr;
            if (m_project_settings) {
                const auto& game_view = m_project_settings->display();
                m_selection_tool.set_camera_output_size(glm::vec2(game_view.reference_width, game_view.reference_height));
            }

            // --- 2.5 Drag and Drop Target for Content Browser Assets ---
            if (is_editing && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET")) {
                    const char* path_str = static_cast<const char*>(payload->Data);
                    if (path_str && active_scene) {
                        std::filesystem::path asset_path(path_str);
                        std::string ext = asset_path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
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
                            TransformComponent desired_tr{glm::vec3(world_pos.x, world_pos.y, 0.0f)};
                            EntityManager& em = active_scene->get_entity_manager();
                            if (m_command_history) {
                                m_command_history->execute_command(
                                    std::make_unique<InstantiatePrefabCommand>(
                                        &em, m_selection, path_str, false, desired_tr, 0, m_renderer, m_entity_state_store.get()
                                    )
                                );
                            } else {
                                InstantiatePrefabCommand cmd(&em, m_selection, path_str, false, desired_tr, 0, m_renderer, m_entity_state_store.get());
                                cmd.execute();
                            }
                        } else if (ext == ".scene") {
                            if (m_on_load_scene) {
                                m_on_load_scene(path_str);
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // --- 3. Viewport Camera Navigation (Pan / Zoom / Focus 'F') ---
            if (is_editing && active_cam && generic_scene && m_selection) {
                m_camera_controller.handle_input(generic_scene, *m_selection, viewport_tools_hovered, m_is_focused, m_viewport_mouse_pos, viewport_mouse_delta);
            }

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
                m_collider_overlay.render(
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
                    img_size,
                    &m_visibility_filter
                );

                // B. Transform Gizmo (Records to Command History on Drag Release)
                is_gizmo_active = m_gizmo.draw_gizmo(
                    active_scene->get_entity_manager(),
                    m_selection->get_selected(),
                    *active_cam,
                    img_min,
                    img_size,
                    viewport_tools_hovered,
                    m_command_history.get(),
                    &m_visibility_filter
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
                    viewport_tools_hovered,
                    is_gizmo_active || m_gizmo.is_hovered() || m_gizmo.is_using() || is_guide_interaction,
                    &m_visibility_filter
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
                    viewport_tools_hovered,
                    m_command_history.get()
                );
            }

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
