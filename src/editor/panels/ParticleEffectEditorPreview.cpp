#include "ParticleEffectEditorPanel.hpp"

#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <imgui.h>

namespace Engine {
namespace {

constexpr int PREVIEW_WIDTH = 480;
constexpr int PREVIEW_HEIGHT = 270;

float channel_to_float(std::uint8_t channel) {
    return static_cast<float>(channel) / 255.0f;
}

std::uint8_t float_to_channel(float channel) {
    return static_cast<std::uint8_t>(
        std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f
    );
}

Texture* resolve_particle_texture(
    SDL_Renderer* renderer,
    const ParticleEffectAsset& asset,
    AssetDatabase& database
) {
    if (!renderer || !asset.texture_handle.is_valid()) return nullptr;
    const std::string path = database.resolve(asset.texture_handle);
    return path.empty() ? nullptr : AssetManager::get().get_texture(renderer, path);
}

} // namespace

void ParticleEffectEditorPanel::render_preview_workspace(
    const ParticleEffectAsset& asset,
    AssetDatabase& database,
    float height
) {
    ImGui::BeginChild(
        "ParticlePreview", ImVec2(0.0f, height), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );

    if (EditorUi::toolbar_button(
            "particle_preview_play",
            m_preview_playing ? EditorIcon::Pause : EditorIcon::Play,
            m_preview_playing ? "Pause preview" : "Play preview",
            m_preview_playing)) {
        m_preview_playing = !m_preview_playing;
    }
    ImGui::SameLine();
    if (EditorUi::action_button(
            "particle_preview_restart", EditorIcon::History, "Restart",
            EditorUi::ButtonTone::Neutral)) {
        reset_preview();
    }
    ImGui::SameLine();
    int motion_mode = static_cast<int>(m_preview_motion);
    const char* motion_modes[] = {"Static", "Cursor", "Figure 8"};
    ImGui::SetNextItemWidth(105.0f * EditorTheme::metrics().scale);
    if (ImGui::Combo(
            "##particle_preview_motion", &motion_mode, motion_modes, 3)) {
        m_preview_motion = static_cast<PreviewMotion>(motion_mode);
        reset_preview();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Preview emitter motion");
    }

    if (m_preview_motion == PreviewMotion::Static) {
        m_preview_emitter_position = {};
    } else if (m_preview_motion == PreviewMotion::FollowCursor) {
        m_preview_emitter_position = m_preview_cursor_position;
        if (!m_preview_cursor_hovered) {
            m_preview_has_previous_emitter_position = false;
        }
    } else {
        const float phase = m_preview_motion_elapsed * 1.2f;
        m_preview_emitter_position = {
            std::sin(phase) * 130.0f,
            std::sin(phase * 2.0f) * 62.0f
        };
    }

    ImGui::SameLine();
    if (EditorUi::action_button(
            "particle_preview_burst", EditorIcon::Zap, "Burst",
            EditorUi::ButtonTone::Neutral, asset.emission.burst_count > 0)) {
        Texture* texture = resolve_particle_texture(m_renderer, asset, database);
        static_cast<void>(m_preview_system.emit(
            asset, m_preview_emitter_position, 0.0f,
            asset.emission.burst_count, texture
        ));
    }
    ImGui::SameLine();
    if (EditorUi::toolbar_button(
            "particle_preview_clear", EditorIcon::Clear, "Clear preview",
            false, m_preview_system.get_active_count() > 0)) {
        m_preview_system.clear();
    }
    ImGui::SameLine();
    float background[3] = {
        channel_to_float(m_preview_background.r),
        channel_to_float(m_preview_background.g),
        channel_to_float(m_preview_background.b)
    };
    if (ImGui::ColorEdit3(
            "##particle_preview_background",
            background,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        m_preview_background = {
            float_to_channel(background[0]),
            float_to_channel(background[1]),
            float_to_channel(background[2]),
            255
        };
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview background color");

    const std::string active_label =
        std::to_string(m_preview_system.get_active_count()) + " ACTIVE";
    const float badge_width = ImGui::CalcTextSize(active_label.c_str()).x +
        10.0f * EditorTheme::metrics().scale;
    ImGui::SameLine(std::max(
        ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - badge_width
    ));
    EditorUi::badge(
        active_label.c_str(),
        m_preview_system.get_active_count() > 0
            ? EditorUi::BadgeTone::Success
            : EditorUi::BadgeTone::Neutral
    );
    ImGui::Separator();

    if (!m_renderer) {
        EditorUi::empty_state(
            EditorIcon::Zap,
            "Preview unavailable",
            "The particle renderer is not initialized"
        );
        ImGui::EndChild();
        return;
    }

    if (!m_preview_target) {
        m_preview_target = SDL_CreateTexture(
            m_renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET,
            PREVIEW_WIDTH,
            PREVIEW_HEIGHT
        );
        if (m_preview_target) {
            SDL_SetTextureBlendMode(m_preview_target, SDL_BLENDMODE_BLEND);
        }
    }
    if (!m_preview_renderer) {
        m_preview_renderer = std::make_unique<Renderer2D>(m_renderer);
        m_preview_system.init_default_textures(m_renderer);
    }
    if (!m_preview_target || !m_preview_renderer) {
        EditorUi::empty_state(
            EditorIcon::Zap,
            "Preview unavailable",
            "Could not allocate the render target"
        );
        ImGui::EndChild();
        return;
    }

    Texture* texture = resolve_particle_texture(m_renderer, asset, database);
    const float delta_time = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    if (m_preview_playing) {
        m_preview_elapsed += delta_time;
        m_preview_motion_elapsed += delta_time;
        if (m_preview_motion == PreviewMotion::FigureEight) {
            const float phase = m_preview_motion_elapsed * 1.2f;
            m_preview_emitter_position = {
                std::sin(phase) * 130.0f,
                std::sin(phase * 2.0f) * 62.0f
            };
        }
        const bool delay_complete = m_preview_elapsed >= m_preview_delay;
        const float active_elapsed = std::max(0.0f, m_preview_elapsed - m_preview_delay);
        const bool duration_complete = asset.emission.duration > 0.0f &&
            active_elapsed >= asset.emission.duration;
        const bool emitter_available =
            m_preview_motion != PreviewMotion::FollowCursor || m_preview_cursor_hovered;
        if (duration_complete && asset.emission.loop) {
            m_preview_elapsed = 0.0f;
            m_preview_delay = asset.emission.start_delay;
            m_preview_accumulator = 0.0f;
            m_preview_distance_accumulator = 0.0f;
            m_preview_burst_pending = true;
            m_preview_has_previous_emitter_position = false;
        } else if (!duration_complete && delay_complete && emitter_available) {
            if (m_preview_burst_pending) {
                m_preview_burst_pending = false;
                if (asset.emission.burst_count > 0) {
                    static_cast<void>(m_preview_system.emit(
                        asset, m_preview_emitter_position, 0.0f,
                        asset.emission.burst_count, texture
                    ));
                }
            }
            if (asset.emission.continuous_rate > 0.0f) {
                m_preview_accumulator += delta_time * asset.emission.continuous_rate;
                const int count = static_cast<int>(m_preview_accumulator);
                if (count > 0) {
                    m_preview_accumulator -= static_cast<float>(count);
                    static_cast<void>(m_preview_system.emit(
                        asset, m_preview_emitter_position, 0.0f, count, texture
                    ));
                }
            }

            if (!m_preview_has_previous_emitter_position) {
                m_preview_previous_emitter_position = m_preview_emitter_position;
                m_preview_has_previous_emitter_position = true;
            }
            const glm::vec2 movement =
                m_preview_emitter_position - m_preview_previous_emitter_position;
            const float travelled = glm::length(movement);
            if (asset.emission.distance_rate > 0.0f && travelled > 0.0001f) {
                const float previous_fraction = m_preview_distance_accumulator;
                const float generated = previous_fraction +
                    travelled * asset.emission.distance_rate;
                const int requested = static_cast<int>(generated);
                m_preview_distance_accumulator = generated -
                    static_cast<float>(requested);
                constexpr int maximum_distance_emissions = 512;
                const int count = std::min(requested, maximum_distance_emissions);
                if (count > 0) {
                    const glm::vec2 direction = movement / travelled;
                    const float spacing = 1.0f / asset.emission.distance_rate;
                    const float first_distance = (1.0f - previous_fraction) * spacing;
                    for (int index = 0; index < count; ++index) {
                        const float along = std::min(
                            first_distance + static_cast<float>(index) * spacing,
                            travelled
                        );
                        static_cast<void>(m_preview_system.emit(
                            asset,
                            m_preview_previous_emitter_position + direction * along,
                            0.0f,
                            1,
                            texture
                        ));
                    }
                }
            }
            m_preview_previous_emitter_position = m_preview_emitter_position;
        } else if (!emitter_available) {
            m_preview_has_previous_emitter_position = false;
        }
        m_preview_system.update(delta_time);
    }

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
            m_renderer,
            m_preview_background.r,
            m_preview_background.g,
            m_preview_background.b,
            m_preview_background.a
        ));
        static_cast<void>(SDL_RenderClear(m_renderer));

        m_preview_camera.set_viewport_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        m_preview_camera.set_reference_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        m_preview_renderer->set_custom_render_target(m_preview_target);
        m_preview_renderer->set_scene_clear_color(m_preview_background);
        m_preview_renderer->begin_scene(m_preview_camera);
        m_preview_system.render(*m_preview_renderer);
        m_preview_renderer->end_scene();
        m_preview_renderer->set_custom_render_target(previous_target);
    }

    static_cast<void>(SDL_SetRenderTarget(m_renderer, previous_target));
    static_cast<void>(SDL_SetRenderScale(
        m_renderer, previous_scale_x, previous_scale_y
    ));
    if (had_viewport) {
        static_cast<void>(SDL_SetRenderViewport(m_renderer, &previous_viewport));
    } else {
        static_cast<void>(SDL_SetRenderViewport(m_renderer, nullptr));
    }
    static_cast<void>(SDL_SetRenderDrawColor(
        m_renderer, previous_r, previous_g, previous_b, previous_a
    ));

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
    m_preview_cursor_hovered = ImGui::IsItemHovered();
    if (m_preview_cursor_hovered &&
        m_preview_motion == PreviewMotion::FollowCursor) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const glm::vec2 preview_pixel{
            (mouse.x - image_min.x) * static_cast<float>(PREVIEW_WIDTH) /
                std::max(display_size.x, 1.0f),
            (mouse.y - image_min.y) * static_cast<float>(PREVIEW_HEIGHT) /
                std::max(display_size.y, 1.0f)
        };
        m_preview_cursor_position = m_preview_camera.screen_to_world(preview_pixel);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Move the cursor to drive the preview emitter");
        }
    }
    const ImVec2 image_max(image_min.x + display_size.x, image_min.y + display_size.y);
    const ImVec2 center(
        (image_min.x + image_max.x) * 0.5f,
        (image_min.y + image_max.y) * 0.5f
    );
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 guide_color = ImGui::ColorConvertFloat4ToU32(
        ImVec4(
            EditorTheme::palette().text_muted.x,
            EditorTheme::palette().text_muted.y,
            EditorTheme::palette().text_muted.z,
            0.18f
        )
    );
    draw_list->AddLine(
        ImVec2(image_min.x, center.y), ImVec2(image_max.x, center.y), guide_color
    );
    draw_list->AddLine(
        ImVec2(center.x, image_min.y), ImVec2(center.x, image_max.y), guide_color
    );
    const glm::vec2 emitter_pixel = m_preview_camera.world_to_screen(
        glm::vec3(m_preview_emitter_position, 0.0f)
    );
    const ImVec2 emitter_screen{
        image_min.x + emitter_pixel.x * display_size.x /
            static_cast<float>(PREVIEW_WIDTH),
        image_min.y + emitter_pixel.y * display_size.y /
            static_cast<float>(PREVIEW_HEIGHT)
    };
    draw_list->AddCircle(
        emitter_screen,
        4.0f * EditorTheme::metrics().scale,
        ImGui::ColorConvertFloat4ToU32(EditorTheme::palette().accent_hovered),
        12,
        1.5f * EditorTheme::metrics().scale
    );
    draw_list->AddRect(
        image_min,
        image_max,
        ImGui::ColorConvertFloat4ToU32(EditorTheme::palette().border),
        EditorTheme::metrics().small_rounding
    );

    ImGui::EndChild();
}

} // namespace Engine
