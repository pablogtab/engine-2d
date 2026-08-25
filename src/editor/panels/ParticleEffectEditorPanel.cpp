#include "ParticleEffectEditorPanel.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/AssetFileCommands.hpp"
#include "../commands/EditorPropertyTransaction.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/FileSystem.hpp"
#include <imgui.h>
#include <algorithm>

namespace Engine {

ParticleEffectEditorPanel::ParticleEffectEditorPanel(
    SDL_Renderer* renderer,
    std::shared_ptr<EditorCommandHistory> command_history
) : m_command_history(std::move(command_history)), m_renderer(renderer) {
    if (m_renderer) {
        m_preview_renderer = std::make_unique<Renderer2D>(m_renderer);
        m_preview_system.init_default_textures(m_renderer);
    }
}

ParticleEffectEditorPanel::~ParticleEffectEditorPanel() { destroy_preview(); }

void ParticleEffectEditorPanel::destroy_preview() {
    m_preview_renderer.reset();
    if (m_preview_target) SDL_DestroyTexture(m_preview_target);
    m_preview_target = nullptr;
}

void ParticleEffectEditorPanel::on_render_resources_invalidated() {
    destroy_preview();
    m_preview_system.clear();
    m_preview_system.invalidate_render_resources();
}

void ParticleEffectEditorPanel::commit_draft_gesture() {
    if (!m_edit_before || !m_current_asset || !m_command_history || ImGui::IsAnyItemActive()) return;
    const ParticleEffectAsset before = *m_edit_before;
    const ParticleEffectAsset after = *m_current_asset;
    m_edit_before.reset();
    if (before == after) return;
    m_command_history->push_executed_command(std::make_unique<EditorValueCommand>(
        [this, before]() {
            m_current_asset = before;
            m_is_dirty = !m_saved_asset || before != *m_saved_asset;
            m_preview_system.clear();
        },
        [this, after]() {
            m_current_asset = after;
            m_is_dirty = !m_saved_asset || after != *m_saved_asset;
            m_preview_system.clear();
        },
        "Edit Particle Effect"
    ));
}

void ParticleEffectEditorPanel::render_preview(
    const ParticleEffectAsset& asset,
    AssetDatabase& database
) {
    if (!m_renderer) {
        ImGui::TextDisabled("Particle preview renderer is unavailable.");
        return;
    }
    constexpr int width = 480;
    constexpr int height = 270;
    if (!m_preview_target) {
        m_preview_target = SDL_CreateTexture(
            m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height
        );
        if (m_preview_target) SDL_SetTextureBlendMode(m_preview_target, SDL_BLENDMODE_BLEND);
    }
    if (!m_preview_renderer) {
        m_preview_renderer = std::make_unique<Renderer2D>(m_renderer);
        m_preview_system.init_default_textures(m_renderer);
    }
    if (!m_preview_target || !m_preview_renderer) {
        ImGui::TextDisabled("Could not allocate the particle preview target.");
        return;
    }

    Texture* texture = nullptr;
    if (asset.texture_handle.is_valid()) {
        const std::string path = database.resolve(asset.texture_handle);
        if (!path.empty()) texture = AssetManager::get().get_texture(m_renderer, path);
    }
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    if (m_preview_playing) {
        m_preview_elapsed += dt;
        const bool delay_complete = m_preview_elapsed >= m_preview_delay;
        const float active_elapsed = std::max(0.0f, m_preview_elapsed - m_preview_delay);
        const bool duration_complete = asset.emission.duration > 0.0f &&
            active_elapsed >= asset.emission.duration;
        if (duration_complete && asset.emission.loop) {
            m_preview_elapsed = 0.0f;
            m_preview_delay = asset.emission.start_delay;
            m_preview_accumulator = 0.0f;
        }
        if (delay_complete && !duration_complete && asset.emission.continuous_rate > 0.0f) {
            m_preview_accumulator += dt * asset.emission.continuous_rate;
            const int count = static_cast<int>(m_preview_accumulator);
            if (count > 0) {
                m_preview_accumulator -= static_cast<float>(count);
                static_cast<void>(m_preview_system.emit(asset, {}, 0.0f, count, texture));
            }
        }
        m_preview_system.update(dt);
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(m_renderer);
    m_preview_camera.set_viewport_size(width, height);
    m_preview_camera.set_reference_size(width, height);
    m_preview_renderer->set_custom_render_target(m_preview_target);
    m_preview_renderer->set_scene_clear_color({17, 20, 27, 255});
    m_preview_renderer->begin_scene(m_preview_camera);
    m_preview_system.render(*m_preview_renderer);
    m_preview_renderer->end_scene();
    m_preview_renderer->set_custom_render_target(previous_target);
    SDL_SetRenderTarget(m_renderer, previous_target);

    const float available = std::max(160.0f, ImGui::GetContentRegionAvail().x);
    const float display_width = std::min(available, static_cast<float>(width));
    ImGui::Image(m_preview_target, {display_width, display_width * height / width});
}

void ParticleEffectEditorPanel::open_asset(AssetHandle handle) {
    if (!handle.is_valid()) return;
    const auto& db = AssetDatabase::get();
    const AssetRecord* record = db.find(handle);
    if (!record || record->type != AssetType::ParticleEffect) return;

    std::string error;
    auto loaded = ParticleEffectAsset::load_from_file(db.resolve(handle), &error);
    if (loaded) {
        m_current_handle = handle;
        m_current_asset = std::move(*loaded);
        m_saved_asset = m_current_asset;
        m_current_filepath = (db.project_root() / record->source_path).string();
        m_is_dirty = false;
        m_edit_before.reset();
        m_preview_system.clear();
        m_preview_accumulator = 0.0f;
        m_preview_elapsed = 0.0f;
        m_preview_delay = m_current_asset->emission.start_delay;
        m_error.clear();
    } else {
        m_current_handle = handle;
        m_current_asset.reset();
        m_saved_asset.reset();
        m_current_filepath = db.resolve(handle);
        m_is_dirty = false;
        m_error = std::move(error);
    }
}

void ParticleEffectEditorPanel::on_gui() {
    if (!is_open) return;

    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    AssetDatabase& db = AssetDatabase::get();

    // Asset selection header
    const AssetRecord* current_rec = m_current_handle.is_valid() ? db.find(m_current_handle) : nullptr;
    const char* preview_name = current_rec ? current_rec->source_path.c_str() : "Select a .particle asset...";

    if (ImGui::BeginCombo("Asset", preview_name)) {
        for (const auto& rec : db.records()) {
            if (rec.type != AssetType::ParticleEffect || rec.status != AssetStatus::Ready) continue;
            const bool selected = rec.handle == m_current_handle;
            if (ImGui::Selectable(rec.source_path.c_str(), selected)) {
                open_asset(rec.handle);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (!m_current_asset) {
        ImGui::Spacing();
        if (!m_error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Could not open particle effect:");
            ImGui::TextWrapped("%s", m_error.c_str());
        } else {
            ImGui::TextDisabled("Select or create a Particle Effect in the Asset Browser to edit.");
        }
        ImGui::End();
        return;
    }

    auto& asset = *m_current_asset;
    const ParticleEffectAsset frame_before = asset;

    ImGui::SameLine();
    if (m_is_dirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[Unsaved]");
        ImGui::SameLine();
    }

    if (ImGui::Button("Save Asset", ImVec2(100, 0))) {
        std::string error;
        if (!asset.validate(&error)) {
            m_error = std::move(error);
            ImGui::OpenPopup("Particle Effect Validation Failed");
        } else if (m_command_history) {
            const std::string before = FileSystem::read_text(m_current_filepath);
            const std::string after = asset.to_json().dump(2);
            const auto before_json = nlohmann::json::parse(before, nullptr, false);
            const auto before_asset = before_json.is_discarded()
                ? std::optional<ParticleEffectAsset>{}
                : ParticleEffectAsset::from_json(before_json);
            const ParticleEffectAsset after_asset = asset;
            const std::string saved_path = m_current_filepath;
            if (m_command_history->execute_command(std::make_unique<ModifyAssetCommand>(
                    &db,
                    m_current_filepath,
                    before,
                    after,
                    "Save Particle Effect",
                    [this, saved_path, before_asset]() {
                        if (m_current_filepath != saved_path || !before_asset) return;
                        m_current_asset = *before_asset;
                        m_saved_asset = *before_asset;
                        m_is_dirty = false;
                        m_preview_system.clear();
                    },
                    [this, saved_path, after_asset]() {
                        if (m_current_filepath != saved_path) return;
                        m_current_asset = after_asset;
                        m_saved_asset = after_asset;
                        m_is_dirty = false;
                        m_preview_system.clear();
                    }
                ))) {
                m_is_dirty = false;
            }
        } else if (asset.save_asset(m_current_filepath, db, &error)) {
            m_saved_asset = asset;
            m_is_dirty = false;
        } else {
            m_error = std::move(error);
            ImGui::OpenPopup("Particle Effect Validation Failed");
        }
    }

    if (ImGui::BeginPopupModal("Particle Effect Validation Failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_error.c_str());
        if (ImGui::Button("OK", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // Split layout: Controls on Left, Live Preview Status on Right
    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
        char name_buf[128];
        std::snprintf(name_buf, sizeof(name_buf), "%s", asset.name.c_str());
        if (ImGui::InputText("Name", name_buf, sizeof(name_buf))) {
            asset.name = name_buf;
            m_is_dirty = true;
        }

        const AssetRecord* tex_rec = asset.texture_handle.is_valid() ? db.find(asset.texture_handle) : nullptr;
        if (ImGui::BeginCombo("Texture", tex_rec ? tex_rec->source_path.c_str() : "Default (Soft Circle)")) {
            if (ImGui::Selectable("Default (Soft Circle)", !asset.texture_handle.is_valid())) {
                asset.texture_handle = {};
                m_is_dirty = true;
            }
            for (const auto& rec : db.records()) {
                if (rec.type != AssetType::Texture || rec.status != AssetStatus::Ready) continue;
                const bool selected = rec.handle == asset.texture_handle;
                if (ImGui::Selectable(rec.source_path.c_str(), selected)) {
                    asset.texture_handle = rec.handle;
                    m_is_dirty = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        float start_col[4] = {
            asset.appearance.start_color.r / 255.0f,
            asset.appearance.start_color.g / 255.0f,
            asset.appearance.start_color.b / 255.0f,
            asset.appearance.start_color.a / 255.0f
        };
        if (ImGui::ColorEdit4("Start Color", start_col)) {
            asset.appearance.start_color = {
                static_cast<uint8_t>(start_col[0] * 255.0f),
                static_cast<uint8_t>(start_col[1] * 255.0f),
                static_cast<uint8_t>(start_col[2] * 255.0f),
                static_cast<uint8_t>(start_col[3] * 255.0f)
            };
            m_is_dirty = true;
        }

        float end_col[4] = {
            asset.appearance.end_color.r / 255.0f,
            asset.appearance.end_color.g / 255.0f,
            asset.appearance.end_color.b / 255.0f,
            asset.appearance.end_color.a / 255.0f
        };
        if (ImGui::ColorEdit4("End Color", end_col)) {
            asset.appearance.end_color = {
                static_cast<uint8_t>(end_col[0] * 255.0f),
                static_cast<uint8_t>(end_col[1] * 255.0f),
                static_cast<uint8_t>(end_col[2] * 255.0f),
                static_cast<uint8_t>(end_col[3] * 255.0f)
            };
            m_is_dirty = true;
        }

        if (ImGui::DragFloat("Start Size", &asset.appearance.start_size, 0.2f, 0.1f, 200.0f)) m_is_dirty = true;
        if (ImGui::DragFloat("End Size", &asset.appearance.end_size, 0.2f, 0.0f, 200.0f)) m_is_dirty = true;
        if (ImGui::DragFloat("Size Variation", &asset.appearance.size_variation, 0.1f, 0.0f, 100.0f)) m_is_dirty = true;
    }

    if (ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen)) {
        float vel[2] = {asset.motion.initial_velocity.x, asset.motion.initial_velocity.y};
        if (ImGui::DragFloat2("Initial Velocity", vel, 1.0f, -2000.0f, 2000.0f)) {
            asset.motion.initial_velocity = {vel[0], vel[1]};
            m_is_dirty = true;
        }

        float vel_var[2] = {asset.motion.velocity_variation.x, asset.motion.velocity_variation.y};
        if (ImGui::DragFloat2("Velocity Variation", vel_var, 1.0f, 0.0f, 2000.0f)) {
            asset.motion.velocity_variation = {vel_var[0], vel_var[1]};
            m_is_dirty = true;
        }

        float accel[2] = {asset.motion.acceleration.x, asset.motion.acceleration.y};
        if (ImGui::DragFloat2("Acceleration", accel, 1.0f, -2000.0f, 2000.0f)) {
            asset.motion.acceleration = {accel[0], accel[1]};
            m_is_dirty = true;
        }

        float grav[2] = {asset.motion.gravity.x, asset.motion.gravity.y};
        if (ImGui::DragFloat2("Gravity", grav, 1.0f, -2000.0f, 2000.0f)) {
            asset.motion.gravity = {grav[0], grav[1]};
            m_is_dirty = true;
        }

        if (ImGui::DragFloat("Initial Rotation", &asset.motion.initial_rotation, 1.0f, -360.0f, 360.0f)) m_is_dirty = true;
        if (ImGui::DragFloat("Rotation Variation", &asset.motion.rotation_variation, 1.0f, 0.0f, 360.0f)) m_is_dirty = true;
        if (ImGui::DragFloat("Rotation Speed", &asset.motion.rotation_speed, 1.0f, -720.0f, 720.0f)) m_is_dirty = true;
        if (ImGui::DragFloat("Rotation Speed Variation", &asset.motion.rotation_speed_variation, 1.0f, 0.0f, 720.0f)) m_is_dirty = true;
    }

    if (ImGui::CollapsingHeader("Lifetime", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat("Duration", &asset.lifetime.duration, 0.05f, 0.05f, 60.0f, "%.2f s")) m_is_dirty = true;
        if (ImGui::DragFloat("Variation", &asset.lifetime.variation, 0.05f, 0.0f, 30.0f, "%.2f s")) m_is_dirty = true;
    }

    if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Enable World Collision", &asset.collision.enabled)) m_is_dirty = true;
        if (asset.collision.enabled) {
            if (ImGui::SliderFloat("Bounciness", &asset.collision.bounciness, 0.0f, 1.0f)) m_is_dirty = true;
            int collision_mask = static_cast<int>(asset.collision.mask_bits);
            if (ImGui::InputInt("Collision Mask", &collision_mask)) {
                asset.collision.mask_bits = static_cast<std::uint16_t>(
                    std::clamp(collision_mask, 0, 65535)
                );
                m_is_dirty = true;
            }
        }
    }

    if (ImGui::CollapsingHeader("Emission Defaults", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat("Continuous Rate", &asset.emission.continuous_rate, 0.5f, 0.0f, 500.0f, "%.1f /s")) m_is_dirty = true;
        if (ImGui::DragInt("Burst Count", &asset.emission.burst_count, 1.0f, 0, 1000)) m_is_dirty = true;
        if (ImGui::DragFloat("Emission Duration", &asset.emission.duration, 0.1f, 0.0f, 60.0f, "%.1f s (0 = infinite)")) m_is_dirty = true;
        if (ImGui::DragFloat("Start Delay", &asset.emission.start_delay, 0.05f, 0.0f, 10.0f, "%.2f s")) m_is_dirty = true;
        if (ImGui::Checkbox("Loop", &asset.emission.loop)) m_is_dirty = true;
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        int layer = static_cast<int>(asset.rendering.render_layer);
        if (ImGui::SliderInt("Render Layer", &layer, 0, 31)) {
            asset.rendering.render_layer = static_cast<RenderLayer>(layer);
            m_is_dirty = true;
        }
        if (ImGui::DragFloat("Z Layer", &asset.rendering.z_layer, 0.01f, -1.0f, 1.0f, "%.3f")) m_is_dirty = true;
        int blend_mode = static_cast<int>(asset.rendering.blend_mode);
        const char* blend_modes[] = {"Alpha", "Additive"};
        if (ImGui::Combo("Blend Mode", &blend_mode, blend_modes, 2)) {
            asset.rendering.blend_mode = static_cast<ParticleBlendMode>(blend_mode);
            m_is_dirty = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    render_preview(asset, db);

    // Preview Controls
    ImGui::Text("Preview & Diagnostics:");
    if (ImGui::Button(m_preview_playing ? "Pause" : "Play", ImVec2(80, 0))) {
        m_preview_playing = !m_preview_playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart", ImVec2(80, 0))) {
        m_preview_system.clear();
        m_preview_accumulator = 0.0f;
        m_preview_elapsed = 0.0f;
        m_preview_delay = asset.emission.start_delay;
        m_preview_playing = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Burst 20", ImVec2(80, 0))) {
        Texture* texture = nullptr;
        if (asset.texture_handle.is_valid()) {
            const std::string path = db.resolve(asset.texture_handle);
            if (!path.empty()) texture = AssetManager::get().get_texture(m_renderer, path);
        }
        static_cast<void>(m_preview_system.emit(asset, {}, 0.0f, 20, texture));
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(80, 0))) {
        m_preview_system.clear();
    }
    ImGui::SameLine();
    ImGui::Text("Active Particles: %zu / %zu", m_preview_system.get_active_count(), m_preview_system.get_capacity());

    if (asset != frame_before) {
        if (!m_edit_before) m_edit_before = frame_before;
        m_is_dirty = true;
    }
    commit_draft_gesture();

    ImGui::End();
}

} // namespace Engine
