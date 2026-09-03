#include "TrailPreviewPanel.hpp"

#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"
#include "../../ecs/RenderSystem.hpp"
#include "../../ecs/components/AnimationComponents.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../renderer/Renderer2D.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../../renderer/material/MaterialAsset.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <string>

namespace Engine {
namespace {

constexpr int PREVIEW_WIDTH = 640;
constexpr int PREVIEW_HEIGHT = 360;
constexpr float STRAIGHT_START_X = -270.0f;
constexpr float STRAIGHT_END_X = 270.0f;
constexpr float STRAIGHT_SPEED = 160.0f;

float color_channel(std::uint8_t channel) {
    return static_cast<float>(channel) / 255.0f;
}

std::uint8_t color_byte(float channel) {
    return static_cast<std::uint8_t>(
        std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f
    );
}

void resolve_sprite_resources(SDL_Renderer* renderer, SpriteComponent& sprite) {
    const auto& database = AssetDatabase::get();
    if (!sprite.texture && renderer && sprite.texture_handle.is_valid()) {
        const std::string path = database.resolve(sprite.texture_handle);
        if (!path.empty()) {
            sprite.texture = AssetManager::get().get_texture(renderer, path);
        }
    }
    if (!sprite.material && sprite.material_handle.is_valid()) {
        sprite.material = MaterialAsset::load(sprite.material_handle, database);
    }
}

} // namespace

TrailPreviewPanel::TrailPreviewPanel(
    SDL_Renderer* renderer,
    std::shared_ptr<IMaterialBackend> material_backend
) : m_renderer(renderer), m_material_backend(std::move(material_backend)) {
    m_preview_camera.set_reference_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    m_preview_camera.set_viewport_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
}

TrailPreviewPanel::~TrailPreviewPanel() {
    destroy_render_resources();
}

bool TrailPreviewPanel::targets(
    const EntityManager* entities,
    EntityID entity
) const {
    return entities && entities == m_source_entities && entity == m_source_root &&
        target_is_valid();
}

bool TrailPreviewPanel::target_is_valid() const {
    return m_source_entities && m_source_entities->is_alive(m_source_root) &&
        m_source_entities->entity_generation(m_source_root) == m_source_generation &&
        m_source_entities->has_component<TrailRenderer2DComponent>(m_source_root);
}

std::vector<EntityID> TrailPreviewPanel::collect_visual_sources() const {
    std::vector<EntityID> result;
    if (!target_is_valid()) return result;

    std::function<void(EntityID)> visit = [&](EntityID entity) {
        if (!m_source_entities->is_alive(entity)) return;
        if (entity == m_source_root ||
            m_source_entities->has_component<SpriteComponent>(entity)) {
            result.push_back(entity);
        }
        if (!m_source_entities->has_component<HierarchyComponent>(entity)) return;
        for (EntityID child :
             m_source_entities->get_component<HierarchyComponent>(entity).children) {
            visit(child);
        }
    };
    visit(m_source_root);
    return result;
}

void TrailPreviewPanel::open_for_entity(
    EntityManager* entities,
    EntityID entity
) {
    if (!entities || !entities->is_alive(entity) ||
        !entities->has_component<TrailRenderer2DComponent>(entity)) return;

    const bool changed = entities != m_source_entities || entity != m_source_root ||
        entities->entity_generation(entity) != m_source_generation;
    m_source_entities = entities;
    m_source_root = entity;
    m_source_generation = entities->entity_generation(entity);
    is_open = true;
    m_focus_requested = true;
    if (changed) rebuild_preview();
    restart_preview();
}

void TrailPreviewPanel::clear_target() {
    m_source_entities = nullptr;
    m_source_root = 0;
    m_source_generation = 0;
    m_preview_entities.clear();
    m_preview_root = 0;
    m_visual_clones.clear();
    m_trail_system.clear();
}

void TrailPreviewPanel::rebuild_preview() {
    m_preview_entities.clear();
    m_preview_root = 0;
    m_visual_clones.clear();
    m_trail_system.clear();
    if (!target_is_valid()) return;

    for (EntityID source : collect_visual_sources()) {
        const EntityID preview = m_preview_entities.create_entity();
        m_visual_clones.push_back({
            source,
            m_source_entities->entity_generation(source),
            preview
        });
        if (source == m_source_root) m_preview_root = preview;

        if (m_source_entities->has_component<TransformComponent>(source)) {
            m_preview_entities.add_component<TransformComponent>(
                preview, m_source_entities->get_component<TransformComponent>(source)
            );
        } else {
            m_preview_entities.add_component<TransformComponent>(preview, {});
        }
        if (m_source_entities->has_component<TagComponent>(source)) {
            m_preview_entities.add_component<TagComponent>(
                preview, m_source_entities->get_component<TagComponent>(source)
            );
        }
        if (m_source_entities->has_component<SpriteComponent>(source)) {
            auto sprite = m_source_entities->get_component<SpriteComponent>(source);
            resolve_sprite_resources(m_renderer, sprite);
            m_preview_entities.add_component<SpriteComponent>(preview, std::move(sprite));
        }
        if (m_source_entities->has_component<AnimationComponent>(source)) {
            m_preview_entities.add_component<AnimationComponent>(
                preview, m_source_entities->get_component<AnimationComponent>(source)
            );
        }
    }

    if (m_preview_root != 0) {
        auto trail = m_source_entities->get_component<TrailRenderer2DComponent>(m_source_root);
        if (!trail.material && trail.material_handle.is_valid()) {
            trail.material = MaterialAsset::load(trail.material_handle, AssetDatabase::get());
        }
        m_preview_entities.add_component<TrailRenderer2DComponent>(
            m_preview_root, std::move(trail)
        );
    }
    sync_preview();
}

void TrailPreviewPanel::sync_preview() {
    if (!target_is_valid()) {
        clear_target();
        return;
    }

    const std::vector<EntityID> sources = collect_visual_sources();
    const bool structure_changed = sources.size() != m_visual_clones.size() ||
        !std::equal(
            sources.begin(), sources.end(), m_visual_clones.begin(),
            [](EntityID source, const VisualClone& clone) {
                return source == clone.source;
            }
        );
    if (structure_changed) {
        rebuild_preview();
        return;
    }

    glm::vec3 source_origin{0.0f};
    if (m_source_entities->has_component<TransformComponent>(m_source_root)) {
        source_origin = m_source_entities
            ->get_component<TransformComponent>(m_source_root).world_position;
    }

    for (const VisualClone& clone : m_visual_clones) {
        if (!m_source_entities->is_alive(clone.source) ||
            m_source_entities->entity_generation(clone.source) != clone.source_generation ||
            !m_preview_entities.is_alive(clone.preview)) {
            rebuild_preview();
            return;
        }

        auto& target_transform =
            m_preview_entities.get_component<TransformComponent>(clone.preview);
        if (m_source_entities->has_component<TransformComponent>(clone.source)) {
            const auto& source_transform =
                m_source_entities->get_component<TransformComponent>(clone.source);
            target_transform = source_transform;
            const glm::vec3 relative = source_transform.world_position - source_origin;
            const float preview_rotation = m_orient_to_path ? m_asset_rotation : 0.0f;
            const float radians = glm::radians(preview_rotation);
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            const glm::vec2 rotated_relative{
                relative.x * cosine - relative.y * sine,
                relative.x * sine + relative.y * cosine
            };
            target_transform.position = {
                rotated_relative.x + m_emitter_position.x,
                rotated_relative.y + m_emitter_position.y,
                relative.z
            };
            target_transform.world_position = target_transform.position;
            target_transform.scale = source_transform.world_scale;
            target_transform.world_scale = source_transform.world_scale;
            // World motion is Y-up, while sprite rotation is applied after the
            // renderer maps it to Y-down screen coordinates.
            target_transform.rotation = source_transform.world_rotation - preview_rotation;
            target_transform.world_rotation = target_transform.rotation;
            target_transform.pivot = source_transform.world_pivot;
            target_transform.world_pivot = source_transform.world_pivot;
            target_transform.skew = source_transform.world_skew;
            target_transform.world_skew = source_transform.world_skew;
            target_transform.visible = source_transform.world_visible;
            target_transform.world_visible = source_transform.world_visible;
        } else {
            target_transform.position = glm::vec3(m_emitter_position, 0.0f);
            target_transform.world_position = target_transform.position;
        }

        if (m_source_entities->has_component<SpriteComponent>(clone.source) &&
            m_preview_entities.has_component<SpriteComponent>(clone.preview)) {
            auto sprite = m_source_entities->get_component<SpriteComponent>(clone.source);
            resolve_sprite_resources(m_renderer, sprite);
            auto& target_sprite =
                m_preview_entities.get_component<SpriteComponent>(clone.preview);
            const SDL_FRect animated_rect = target_sprite.src_rect;
            const bool animated_crop = target_sprite.use_src_rect;
            target_sprite = std::move(sprite);
            if (m_preview_entities.has_component<AnimationComponent>(clone.preview)) {
                target_sprite.src_rect = animated_rect;
                target_sprite.use_src_rect = animated_crop;
            }
            target_sprite.visible = target_sprite.visible && m_show_sprite;
        }

        const bool source_has_animation =
            m_source_entities->has_component<AnimationComponent>(clone.source);
        const bool preview_has_animation =
            m_preview_entities.has_component<AnimationComponent>(clone.preview);
        if (source_has_animation != preview_has_animation) {
            rebuild_preview();
            return;
        }
        if (source_has_animation) {
            const auto& source_animation =
                m_source_entities->get_component<AnimationComponent>(clone.source);
            auto& target_animation =
                m_preview_entities.get_component<AnimationComponent>(clone.preview);
            if (!(target_animation == source_animation)) {
                target_animation = source_animation;
                SpriteAnimationSystem::reset_playback(m_preview_entities, clone.preview);
            }
        }
    }

    if (m_preview_root != 0 &&
        m_preview_entities.has_component<TrailRenderer2DComponent>(m_preview_root)) {
        auto trail = m_source_entities->get_component<TrailRenderer2DComponent>(m_source_root);
        if (!trail.material && trail.material_handle.is_valid()) {
            trail.material = MaterialAsset::load(trail.material_handle, AssetDatabase::get());
        }
        trail.enabled = trail.enabled && m_show_trail;
        m_preview_entities.get_component<TrailRenderer2DComponent>(m_preview_root) =
            std::move(trail);
    }
}

void TrailPreviewPanel::restart_preview() {
    m_elapsed = 0.0f;
    m_emitter_position = m_motion_mode == MotionMode::Straight
        ? glm::vec2{STRAIGHT_START_X, 0.0f}
        : glm::vec2{};
    m_previous_emitter_position = m_emitter_position;
    m_asset_rotation = 0.0f;
    m_trail_system.clear();
    for (const VisualClone& clone : m_visual_clones) {
        SpriteAnimationSystem::reset_playback(m_preview_entities, clone.preview);
    }
    sync_preview();
}

void TrailPreviewPanel::destroy_render_resources() {
    m_preview_renderer.reset();
    if (m_preview_target) SDL_DestroyTexture(m_preview_target);
    m_preview_target = nullptr;
}

void TrailPreviewPanel::on_render_resources_invalidated() {
    destroy_render_resources();
    m_trail_system.clear();
}

void TrailPreviewPanel::on_update(float delta_time) {
    if (!target_is_valid()) return;
    const float dt = m_playing ? std::clamp(delta_time, 0.0f, 0.1f) : 0.0f;
    if (m_playing) {
        m_previous_emitter_position = m_emitter_position;
        m_elapsed += dt * m_motion_speed;
        if (m_motion_mode == MotionMode::Straight) {
            const float span = STRAIGHT_END_X - STRAIGHT_START_X;
            m_emitter_position = {
                STRAIGHT_START_X + std::fmod(m_elapsed * STRAIGHT_SPEED, span),
                0.0f
            };
            if (m_emitter_position.x < m_previous_emitter_position.x) {
                // The asset re-enters from the left without joining both
                // sides of the preview with one artificial segment.
                m_trail_system.clear();
            }
        } else if (m_motion_mode == MotionMode::FollowCursor) {
            if (m_cursor_hovered) m_emitter_position = m_cursor_position;
        } else {
            const float phase = m_elapsed * 1.2f;
            m_emitter_position = {
                std::sin(phase) * 145.0f,
                std::sin(phase * 2.0f) * 68.0f
            };
        }
        const glm::vec2 movement = m_emitter_position - m_previous_emitter_position;
        if (m_orient_to_path && glm::dot(movement, movement) > 0.0001f) {
            m_asset_rotation = glm::degrees(std::atan2(movement.y, movement.x));
        }
    }
    sync_preview();
    SpriteAnimationSystem::update(m_preview_entities, dt);
    m_trail_system.update(m_preview_entities, dt);
}

void TrailPreviewPanel::render_toolbar() {
    if (EditorUi::toolbar_button(
            "trail_preview_play",
            m_playing ? EditorIcon::Pause : EditorIcon::Play,
            m_playing ? "Pause preview" : "Play preview",
            m_playing)) {
        m_playing = !m_playing;
    }
    ImGui::SameLine();
    if (EditorUi::action_button(
            "trail_preview_restart", EditorIcon::History, "Restart",
            EditorUi::ButtonTone::Neutral)) {
        restart_preview();
    }
    ImGui::SameLine();
    int motion = static_cast<int>(m_motion_mode);
    const char* paths[] = {"Straight", "Cursor", "Figure 8"};
    ImGui::SetNextItemWidth(110.0f * EditorTheme::metrics().scale);
    if (ImGui::Combo("##trail_preview_motion", &motion, paths, 3)) {
        m_motion_mode = static_cast<MotionMode>(motion);
        restart_preview();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview motion path");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(88.0f * EditorTheme::metrics().scale);
    ImGui::SliderFloat("##trail_preview_speed", &m_motion_speed, 0.1f, 3.0f, "%.1fx");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Motion speed");

    ImGui::SameLine();
    if (ImGui::Checkbox("Sprite", &m_show_sprite)) sync_preview();
    ImGui::SameLine();
    if (ImGui::Checkbox("Trail", &m_show_trail)) sync_preview();
    ImGui::SameLine();
    if (ImGui::Checkbox("Orient", &m_orient_to_path)) sync_preview();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Rotate the complete visual hierarchy to face its path");
    }

    ImGui::SameLine();
    float background[3] = {
        color_channel(m_background.r),
        color_channel(m_background.g),
        color_channel(m_background.b)
    };
    if (ImGui::ColorEdit3(
            "##trail_preview_background", background,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        m_background = {
            color_byte(background[0]), color_byte(background[1]),
            color_byte(background[2]), 255
        };
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview background color");

    ImGui::SameLine();
    if (EditorUi::action_button(
            "trail_preview_fit", EditorIcon::Crosshair, "Fit",
            EditorUi::ButtonTone::Neutral)) {
        m_zoom = 1.0f;
        m_camera_position = {};
    }

    if (m_preview_root != 0) {
        const std::string points =
            std::to_string(m_trail_system.point_count(m_preview_root)) + " POINTS";
        const float badge_width = ImGui::CalcTextSize(points.c_str()).x +
            10.0f * EditorTheme::metrics().scale;
        ImGui::SameLine(std::max(
            ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - badge_width
        ));
        EditorUi::badge(
            points.c_str(),
            m_trail_system.point_count(m_preview_root) > 1
                ? EditorUi::BadgeTone::Success
                : EditorUi::BadgeTone::Neutral
        );
    }
    ImGui::Separator();
}

void TrailPreviewPanel::render_to_target() {
    if (!m_renderer) return;
    if (!m_preview_target) {
        m_preview_target = SDL_CreateTexture(
            m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
            PREVIEW_WIDTH, PREVIEW_HEIGHT
        );
        if (m_preview_target) {
            static_cast<void>(SDL_SetTextureBlendMode(m_preview_target, SDL_BLENDMODE_BLEND));
            static_cast<void>(SDL_SetTextureScaleMode(m_preview_target, SDL_SCALEMODE_LINEAR));
        }
    }
    if (!m_preview_renderer) {
        m_preview_renderer = std::make_unique<Renderer2D>(m_renderer);
        m_preview_renderer->set_material_backend(m_material_backend);
    }
    if (!m_preview_target || !m_preview_renderer) return;

    SDL_Texture* previous_target = SDL_GetRenderTarget(m_renderer);
    SDL_Rect previous_viewport{};
    const bool had_viewport = SDL_GetRenderViewport(m_renderer, &previous_viewport);
    float previous_scale_x = 1.0f;
    float previous_scale_y = 1.0f;
    static_cast<void>(SDL_GetRenderScale(
        m_renderer, &previous_scale_x, &previous_scale_y
    ));
    std::uint8_t previous_r = 0;
    std::uint8_t previous_g = 0;
    std::uint8_t previous_b = 0;
    std::uint8_t previous_a = 0;
    static_cast<void>(SDL_GetRenderDrawColor(
        m_renderer, &previous_r, &previous_g, &previous_b, &previous_a
    ));

    if (SDL_SetRenderTarget(m_renderer, m_preview_target)) {
        static_cast<void>(SDL_SetRenderScale(m_renderer, 1.0f, 1.0f));
        static_cast<void>(SDL_SetRenderViewport(m_renderer, nullptr));
        static_cast<void>(SDL_SetRenderDrawColor(
            m_renderer, m_background.r, m_background.g,
            m_background.b, m_background.a
        ));
        static_cast<void>(SDL_RenderClear(m_renderer));

        m_preview_camera.set_position(m_camera_position);
        m_preview_camera.set_zoom(m_zoom);
        m_preview_camera.set_viewport_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        m_preview_camera.set_reference_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        m_preview_renderer->set_custom_render_target(m_preview_target);
        m_preview_renderer->set_scene_clear_color(m_background);
        m_preview_renderer->begin_scene(m_preview_camera);
        if (m_show_sprite) RenderSystem::render(m_preview_entities, *m_preview_renderer);
        if (m_show_trail) m_trail_system.render(*m_preview_renderer);
        m_preview_renderer->end_scene();
        m_preview_renderer->set_custom_render_target(previous_target);
    }

    static_cast<void>(SDL_SetRenderTarget(m_renderer, previous_target));
    static_cast<void>(SDL_SetRenderScale(
        m_renderer, previous_scale_x, previous_scale_y
    ));
    static_cast<void>(SDL_SetRenderViewport(
        m_renderer, had_viewport ? &previous_viewport : nullptr
    ));
    static_cast<void>(SDL_SetRenderDrawColor(
        m_renderer, previous_r, previous_g, previous_b, previous_a
    ));
}

void TrailPreviewPanel::render_canvas() {
    render_to_target();
    if (!m_preview_target) {
        EditorUi::empty_state(
            EditorIcon::Zap, "Preview unavailable",
            "Could not allocate the trail render target"
        );
        return;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(1.0f, available.x);
    available.y = std::max(1.0f, available.y);
    const float aspect = static_cast<float>(PREVIEW_WIDTH) /
        static_cast<float>(PREVIEW_HEIGHT);
    ImVec2 display_size(available.x, available.x / aspect);
    if (display_size.y > available.y) {
        display_size.y = available.y;
        display_size.x = display_size.y * aspect;
    }
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() + std::max(0.0f, (available.x - display_size.x) * 0.5f)
    );
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + std::max(0.0f, (available.y - display_size.y) * 0.5f)
    );
    const ImVec2 image_min = ImGui::GetCursorScreenPos();
    ImGui::Image(m_preview_target, display_size);
    m_cursor_hovered = ImGui::IsItemHovered();

    if (m_cursor_hovered) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f && m_motion_mode != MotionMode::FollowCursor) {
            m_zoom = std::clamp(m_zoom * std::pow(1.12f, io.MouseWheel), 0.2f, 5.0f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            (ImGui::IsKeyDown(ImGuiKey_Space) &&
             ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
            const ImVec2 drag = io.MouseDelta;
            m_camera_position.x -= drag.x * static_cast<float>(PREVIEW_WIDTH) /
                std::max(display_size.x * m_zoom, 1.0f);
            m_camera_position.y += drag.y * static_cast<float>(PREVIEW_HEIGHT) /
                std::max(display_size.y * m_zoom, 1.0f);
        }
        if (m_motion_mode == MotionMode::FollowCursor) {
            const ImVec2 mouse = io.MousePos;
            const glm::vec2 preview_pixel{
                (mouse.x - image_min.x) * static_cast<float>(PREVIEW_WIDTH) /
                    std::max(display_size.x, 1.0f),
                (mouse.y - image_min.y) * static_cast<float>(PREVIEW_HEIGHT) /
                    std::max(display_size.y, 1.0f)
            };
            m_cursor_position = m_preview_camera.screen_to_world(preview_pixel);
        }
    }

    const ImVec2 image_max{
        image_min.x + display_size.x,
        image_min.y + display_size.y
    };
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRect(
        image_min, image_max,
        ImGui::ColorConvertFloat4ToU32(EditorTheme::palette().border),
        EditorTheme::metrics().small_rounding
    );
}

void TrailPreviewPanel::on_gui() {
    if (m_focus_requested) {
        ImGui::SetNextWindowFocus();
        m_focus_requested = false;
    }
    if (!ImGui::Begin(
            m_name.c_str(), &is_open,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }

    if (!target_is_valid()) {
        if (m_source_entities) clear_target();
        EditorUi::empty_state(
            EditorIcon::Zap,
            "Select a Trail Renderer 2D",
            "Open the preview from its Inspector component"
        );
        ImGui::End();
        return;
    }

    render_toolbar();
    render_canvas();
    ImGui::End();
}

} // namespace Engine
