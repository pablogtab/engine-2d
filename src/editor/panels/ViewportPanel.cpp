#include "ViewportPanel.hpp"
#include "../../core/Log.hpp"
#include "../../core/Time.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include <imgui.h>

namespace Engine {

ViewportPanel::ViewportPanel(SDL_Renderer* renderer, SceneManager* scene_manager, int internal_width, int internal_height)
    : m_renderer(renderer), m_scene_manager(scene_manager), m_internal_width(internal_width), m_internal_height(internal_height) {
    
    if (m_renderer) {
        m_viewport_texture = SDL_CreateTexture(
            m_renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET,
            m_internal_width,
            m_internal_height
        );

        if (m_viewport_texture) {
            SDL_SetTextureScaleMode(m_viewport_texture, SDL_SCALEMODE_LINEAR);
            ENGINE_LOG_INFO("[VIEWPORT] Created Render Target Texture: {}x{}", m_internal_width, m_internal_height);
        } else {
            ENGINE_LOG_ERROR("[VIEWPORT] Failed to create Render Target Texture: {}", SDL_GetError());
        }
    }
}

ViewportPanel::~ViewportPanel() {
    if (m_viewport_texture) {
        SDL_DestroyTexture(m_viewport_texture);
        m_viewport_texture = nullptr;
    }
}

void ViewportPanel::begin_render() {
    if (m_renderer && m_viewport_texture) {
        SDL_SetRenderTarget(m_renderer, m_viewport_texture);
        SDL_SetRenderDrawColor(m_renderer, 24, 28, 36, 255);
        SDL_RenderClear(m_renderer);
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
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | 
                            ImGuiWindowFlags_NoScrollWithMouse | 
                            ImGuiWindowFlags_MenuBar;

    if (ImGui::Begin(m_name.c_str(), &is_open, flags)) {
        // --- 1. Top Viewport Control Bar ---
        if (ImGui::BeginMenuBar()) {
            bool is_play = (m_play_state == EditorPlayState::Play);
            bool is_pause = (m_play_state == EditorPlayState::Pause);
            bool is_edit = (m_play_state == EditorPlayState::Edit);

            // Play Button
            if (is_play) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
            }
            if (ImGui::Button(" ▶ Play ")) {
                m_play_state = EditorPlayState::Play;
                Time::set_time_scale(1.0f);
            }
            if (is_play) ImGui::PopStyleColor();

            ImGui::SameLine();

            // Pause Button
            if (is_pause) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.15f, 1.0f));
            }
            if (ImGui::Button(" ⏸ Pause ")) {
                m_play_state = EditorPlayState::Pause;
                Time::set_time_scale(0.0f);
            }
            if (is_pause) ImGui::PopStyleColor();

            ImGui::SameLine();

            // Stop / Edit Button
            if (is_edit) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.25f, 0.25f, 1.0f));
            }
            if (ImGui::Button(" ⏹ Stop ")) {
                m_play_state = EditorPlayState::Edit;
                Time::set_time_scale(0.0f);
            }
            if (is_edit) ImGui::PopStyleColor();

            ImGui::Separator();

            // Scene Switcher Dropdown
            if (m_scene_manager && m_scene_manager->get_scene_count() > 0) {
                int current_idx = m_scene_manager->get_active_index();
                std::string current_name = m_scene_manager->get_active_scene() ? m_scene_manager->get_active_scene()->get_name() : "None";
                
                ImGui::TextDisabled("Scene:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::BeginCombo("##scene_selector", current_name.c_str())) {
                    const auto& scenes = m_scene_manager->get_scenes();
                    for (size_t i = 0; i < scenes.size(); ++i) {
                        bool is_selected = (current_idx == static_cast<int>(i));
                        if (ImGui::Selectable(scenes[i]->get_name().c_str(), is_selected)) {
                            m_scene_manager->set_active_scene(i);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("%dx%d", m_internal_width, m_internal_height);

            ImGui::EndMenuBar();
        }

        // --- 2. Render Game Texture in Letterboxed Aspect Ratio ---
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x > 0.0f && avail.y > 0.0f && m_viewport_texture) {
            float target_aspect = static_cast<float>(m_internal_width) / static_cast<float>(m_internal_height);
            float window_aspect = avail.x / avail.y;

            float draw_w = avail.x;
            float draw_h = avail.y;

            if (window_aspect > target_aspect) {
                draw_w = avail.y * target_aspect;
                draw_h = avail.y;
            } else {
                draw_w = avail.x;
                draw_h = avail.x / target_aspect;
            }

            float offset_x = (avail.x - draw_w) * 0.5f;
            float offset_y = (avail.y - draw_h) * 0.5f;

            ImVec2 cursor_pos = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursor_pos.x + offset_x, cursor_pos.y + offset_y));

            ImGui::Image(reinterpret_cast<ImTextureID>(m_viewport_texture), ImVec2(draw_w, draw_h));

            m_is_hovered = ImGui::IsItemHovered();
            m_is_focused = ImGui::IsWindowFocused();

            // Calculate precise mouse position relative to the internal 1920x1080 game texture
            ImVec2 img_min = ImGui::GetItemRectMin();
            ImVec2 img_max = ImGui::GetItemRectMax();
            ImVec2 img_size = ImVec2(img_max.x - img_min.x, img_max.y - img_min.y);
            ImVec2 mouse_pos = ImGui::GetMousePos();

            if (img_size.x > 0.0f && img_size.y > 0.0f) {
                float norm_x = (mouse_pos.x - img_min.x) / img_size.x;
                float norm_y = (mouse_pos.y - img_min.y) / img_size.y;
                m_viewport_mouse_pos = glm::vec2(norm_x * static_cast<float>(m_internal_width), 
                                                 norm_y * static_cast<float>(m_internal_height));
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
