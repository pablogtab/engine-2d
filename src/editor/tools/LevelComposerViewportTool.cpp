#include "LevelComposerViewportTool.hpp"

#include "LevelComposerService.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EditorCommandGroup.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../panels/LevelComposerPanel.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../renderer/Texture.hpp"
#include "../../scene/Camera2D.hpp"
#include "../../scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

namespace Engine {

namespace {

std::string resolve_reference_path(const LevelPaletteAssetReference& reference) {
    std::string path;
    if (reference.handle.is_valid()) path = AssetDatabase::get().resolve(reference.handle);
    if (!path.empty()) return path;
    if (reference.path.empty()) return {};
    const std::filesystem::path authored(reference.path);
    return authored.is_absolute()
        ? authored.lexically_normal().string()
        : (AssetDatabase::get().project_root() / authored).lexically_normal().string();
}

glm::vec2 rotate_screen(glm::vec2 value, float degrees) {
    const float radians = glm::radians(degrees);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        value.x * cosine - value.y * sine,
        value.x * sine + value.y * cosine
    };
}

bool is_same_or_descendant(
    const EntityManager& entities,
    EntityID candidate,
    EntityID required_root
) {
    if (candidate == 0 || required_root == 0) return false;
    EntityID current = candidate;
    std::size_t remaining = entities.get_living_entity_count() + 1;
    while (current != 0 && remaining-- > 0) {
        if (current == required_root) return true;
        if (!entities.has_component<HierarchyComponent>(current)) return false;
        current = entities.get_component<HierarchyComponent>(current).parent;
    }
    return false;
}

} // namespace

void LevelComposerViewportTool::cancel() {
    m_stroking = false;
    m_stroke_scene = nullptr;
    m_stroke_history = nullptr;
    m_stroke_entry_id.clear();
    m_stroke_start = {0.0f, 0.0f};
    m_stroke_end = {0.0f, 0.0f};
}

std::unique_ptr<IEditorCommand> LevelComposerViewportTool::make_place_command(
    Scene& scene,
    const LevelPaletteEntry& entry,
    EntityID target_parent,
    const glm::vec2& world_position,
    SDL_Renderer* renderer,
    std::shared_ptr<EditorSelection> selection,
    const EditorEntityStateStore* state_store
) {
    EntityManager& entities = scene.get_entity_manager();
    if (target_parent != 0 &&
        (!entities.is_alive(target_parent) ||
         !entities.has_component<TransformComponent>(target_parent) ||
         (state_store && state_store->is_locked(target_parent)))) {
        return nullptr;
    }

    if (entry.kind == LevelPaletteEntryKind::Prefab) {
        const std::string path = resolve_reference_path(entry.prefab.prefab);
        if (path.empty()) return nullptr;
        TransformComponent transform{
            glm::vec3(world_position.x, world_position.y, entry.placement.depth_z),
            glm::vec2(1.0f),
            entry.placement.rotation_degrees
        };
        return std::make_unique<InstantiatePrefabCommand>(
            &entities,
            std::move(selection),
            path,
            false,
            transform,
            target_parent,
            renderer,
            state_store
        );
    }

    const std::string path = resolve_reference_path(entry.sprite_region.texture);
    if (path.empty()) return nullptr;
    Texture* texture = AssetManager::get().get_texture(renderer, path);
    if (!texture) return nullptr;

    TransformComponent world_transform{
        glm::vec3(world_position.x, world_position.y, entry.placement.depth_z),
        glm::vec2(1.0f),
        entry.placement.rotation_degrees,
        entry.sprite_region.pivot
    };
    TransformComponent authored_transform = world_transform;
    if (target_parent != 0) {
        TransformSystem::update(entities);
        const auto& parent_transform = entities.get_component<TransformComponent>(target_parent);
        const auto local = TransformSystem::world_to_local_transform(
            world_transform, &parent_transform
        );
        if (!local) return nullptr;
        authored_transform = *local;
    }

    SpriteComponent sprite{
        texture,
        entry.sprite_region.world_size,
        SDL_Color{255, 255, 255, 255}
    };
    sprite.texture_handle = entry.sprite_region.texture.handle;
    if (!sprite.texture_handle.is_valid()) {
        if (const AssetRecord* record = AssetDatabase::get().find_by_path(path)) {
            sprite.texture_handle = record->handle;
        }
    }
    const glm::ivec4& region = entry.sprite_region.rect_px;
    sprite.src_rect = {
        static_cast<float>(region.x), static_cast<float>(region.y),
        static_cast<float>(region.z), static_cast<float>(region.w)
    };
    sprite.use_src_rect = true;
    sprite.flip_x = entry.placement.flip_x;
    sprite.flip_y = entry.placement.flip_y;
    sprite.sorting_layer = entry.placement.sorting_layer;
    sprite.order_in_layer = entry.placement.order_in_layer;
    sprite.render_layer = entry.placement.render_layer;

    EntitySnapshot snapshot;
    snapshot.registered_components.set(TagComponent{
        entry.label.empty() ? entry.id : entry.label
    });
    snapshot.registered_components.set(authored_transform);
    snapshot.registered_components.set(std::move(sprite));
    if (target_parent != 0) {
        snapshot.has_hierarchy = true;
        snapshot.hierarchy.parent = target_parent;
    }
    return std::make_unique<CreateEntityCommand>(
        &entities, std::move(selection), std::move(snapshot), state_store
    );
}

void LevelComposerViewportTool::draw_sprite_ghost(
    const LevelPaletteEntry& entry,
    Texture& texture,
    const glm::vec2& world_position,
    const Camera2D& camera,
    const ImVec2& image_min,
    const ImVec2& image_size
) const {
    if (!texture.get_sdl_texture()) return;
    const glm::vec2 viewport_size = camera.get_viewport_size();
    if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) return;
    const float display_x = image_size.x / viewport_size.x;
    const float display_y = image_size.y / viewport_size.y;
    const glm::vec2 anchor_in_camera = camera.world_to_screen(
        glm::vec3(world_position.x, world_position.y, entry.placement.depth_z)
    );
    const ImVec2 anchor(
        image_min.x + anchor_in_camera.x * display_x,
        image_min.y + anchor_in_camera.y * display_y
    );

    const glm::vec2 sprite_size = entry.sprite_region.world_size *
        camera.get_effective_zoom() * camera.get_z_scale(entry.placement.depth_z);
    const glm::vec2 pivot = entry.sprite_region.pivot;
    glm::vec2 local[4]{
        {-pivot.x * sprite_size.x, -pivot.y * sprite_size.y},
        {(1.0f - pivot.x) * sprite_size.x, -pivot.y * sprite_size.y},
        {(1.0f - pivot.x) * sprite_size.x, (1.0f - pivot.y) * sprite_size.y},
        {-pivot.x * sprite_size.x, (1.0f - pivot.y) * sprite_size.y}
    };
    ImVec2 corners[4];
    const float view_rotation = entry.placement.rotation_degrees + camera.get_rotation();
    for (int index = 0; index < 4; ++index) {
        const glm::vec2 rotated = rotate_screen(local[index], view_rotation);
        corners[index] = {
            anchor.x + rotated.x * display_x,
            anchor.y + rotated.y * display_y
        };
    }

    const float width = static_cast<float>(std::max(1, texture.get_width()));
    const float height = static_cast<float>(std::max(1, texture.get_height()));
    const glm::ivec4& source = entry.sprite_region.rect_px;
    float u0 = source.x / width;
    float v0 = source.y / height;
    float u1 = (source.x + source.z) / width;
    float v1 = (source.y + source.w) / height;
    if (entry.placement.flip_x) std::swap(u0, u1);
    if (entry.placement.flip_y) std::swap(v0, v1);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddImageQuad(
        reinterpret_cast<ImTextureID>(texture.get_sdl_texture()),
        corners[0], corners[1], corners[2], corners[3],
        ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1), ImVec2(u0, v1),
        IM_COL32(255, 255, 255, 165)
    );
    draw_list->AddPolyline(corners, 4, IM_COL32(80, 205, 255, 235), ImDrawFlags_Closed, 2.0f);
    draw_list->AddLine(
        ImVec2(anchor.x - 5.0f, anchor.y), ImVec2(anchor.x + 5.0f, anchor.y),
        IM_COL32(255, 220, 90, 255), 1.5f
    );
    draw_list->AddLine(
        ImVec2(anchor.x, anchor.y - 5.0f), ImVec2(anchor.x, anchor.y + 5.0f),
        IM_COL32(255, 220, 90, 255), 1.5f
    );
}

void LevelComposerViewportTool::handle(
    Scene* scene,
    LevelComposerPanel& panel,
    const Camera2D& camera,
    const ImVec2& image_min,
    const ImVec2& image_size,
    bool viewport_hovered,
    SDL_Renderer* renderer,
    std::shared_ptr<EditorSelection> selection,
    EditorCommandHistory* command_history,
    const EditorEntityStateStore* state_store,
    PrefabPreviewCallback preview_prefab,
    EntityID required_root
) {
    if (!panel.is_placement_active() || !scene || !command_history || !renderer) {
        if (m_stroking) cancel();
        return;
    }

    const LevelPaletteEntry* entry = panel.active_entry();
    if (!entry) {
        if (m_stroking) cancel();
        return;
    }
    if (m_stroking &&
        (scene != m_stroke_scene || command_history != m_stroke_history ||
         entry->id != m_stroke_entry_id)) {
        panel.report_tool_status("Placement canceled because the editing context changed.", true);
        cancel();
    }

    ImGuiIO& input = ImGui::GetIO();
    if (!input.WantTextInput && viewport_hovered) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) panel.rotate_active(-90.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) panel.rotate_active(90.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) panel.toggle_active_flip_x();
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) panel.toggle_active_flip_y();
    }
    if (!input.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (m_stroking) cancel();
        panel.stop_placement();
        return;
    }

    glm::vec2 snapped_world = m_stroke_start;
    bool has_cursor_position = false;
    if (viewport_hovered && image_size.x > 0.0f && image_size.y > 0.0f) {
        const glm::vec2 viewport_size = camera.get_viewport_size();
        const glm::vec2 mouse_in_viewport(
            (input.MousePos.x - image_min.x) * viewport_size.x / image_size.x,
            (input.MousePos.y - image_min.y) * viewport_size.y / image_size.y
        );
        snapped_world = LevelComposerService::snap_position(
            camera.screen_to_world(mouse_in_viewport, entry->placement.depth_z),
            entry->placement.snap,
            panel.grid_origin()
        );
        has_cursor_position = true;
    }

    const bool unmodified_left = input.MouseDown[ImGuiMouseButton_Left] &&
        !input.KeyCtrl && !input.KeySuper && !input.KeyAlt;
    if (!m_stroking && viewport_hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && unmodified_left) {
        m_stroking = true;
        m_stroke_scene = scene;
        m_stroke_history = command_history;
        m_stroke_entry_id = entry->id;
        m_stroke_start = snapped_world;
        m_stroke_end = snapped_world;
    }

    if (m_stroking && input.MouseDown[ImGuiMouseButton_Left] && viewport_hovered) {
        m_stroke_end = snapped_world;
    }

    std::vector<glm::vec2> preview_positions;
    std::string placement_error;
    if (m_stroking) {
        preview_positions = LevelComposerService::line_placements({
            .start = m_stroke_start,
            .end = m_stroke_end,
            .snap = entry->placement.snap,
            .origin = panel.grid_origin(),
            .spacing = entry->placement.spacing,
            .include_end = true
        }, &placement_error);
    } else if (has_cursor_position) {
        preview_positions.push_back(snapped_world);
    }

    if (!placement_error.empty()) {
        panel.report_tool_status(placement_error, true);
    }

    if (entry->kind == LevelPaletteEntryKind::SpriteRegion) {
        const std::string path = resolve_reference_path(entry->sprite_region.texture);
        if (!path.empty()) {
            if (Texture* texture = AssetManager::get().get_texture(renderer, path)) {
                for (const glm::vec2 position : preview_positions) {
                    draw_sprite_ghost(
                        *entry, *texture, position, camera, image_min, image_size
                    );
                }
            }
        }
    } else if (preview_prefab && !preview_positions.empty()) {
        const std::string path = resolve_reference_path(entry->prefab.prefab);
        if (!path.empty()) {
            preview_prefab(
                path,
                preview_positions,
                entry->placement.rotation_degrees,
                entry->placement.depth_z
            );
        }
    }

    if (m_stroking && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (placement_error.empty() && !preview_positions.empty() &&
            scene == m_stroke_scene && command_history == m_stroke_history) {
            EntityManager& entities = scene->get_entity_manager();
            EntityID target_parent = panel.target_parent();
            if (required_root != 0 &&
                !is_same_or_descendant(entities, target_parent, required_root)) {
                target_parent = required_root;
            }

            const bool root_invalid = required_root != 0 &&
                (!entities.is_alive(required_root) ||
                 !entities.has_component<TransformComponent>(required_root) ||
                 (state_store && state_store->is_locked(required_root)));
            if (root_invalid) {
                panel.report_tool_status(
                    "Placement canceled: the Prefab Stage root is unavailable or locked.",
                    true
                );
            } else {
                auto group = std::make_unique<EditorCommandGroup>(
                    "Place " + std::to_string(preview_positions.size()) + " Level Stamp" +
                    (preview_positions.size() == 1 ? "" : "s")
                );
                for (const glm::vec2 position : preview_positions) {
                    auto command = make_place_command(
                        *scene, *entry, target_parent, position, renderer,
                        selection, state_store
                    );
                    if (!command) {
                        group.reset();
                        break;
                    }
                    group->add_command(std::move(command));
                }

                if (!group) {
                    panel.report_tool_status(
                        "Placement failed: verify the asset and target parent.", true
                    );
                } else if (!command_history->execute_command(std::move(group))) {
                    const std::string detail = command_history->get_last_error();
                    panel.report_tool_status(
                        detail.empty() ? "Placement transaction failed." : detail, true
                    );
                } else {
                    panel.report_tool_status(
                        "Placed " + std::to_string(preview_positions.size()) +
                        (preview_positions.size() == 1 ? " stamp." : " stamps."),
                        false
                    );
                }
            }
        }
        cancel();
    }
}

} // namespace Engine
