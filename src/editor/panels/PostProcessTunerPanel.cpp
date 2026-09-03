#include "PostProcessTunerPanel.hpp"

#include "../EditorTheme.hpp"
#include "../commands/AssetFileCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"
#include "../../renderer/postprocess/PostProcessPipeline.hpp"
#include "../../scene/Scene.hpp"
#include "../../scene/SceneManager.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <imgui.h>
#include <string_view>
#include <utility>

namespace Engine {

namespace {

float channel_to_float(uint8_t channel) {
    return static_cast<float>(channel) / 255.0f;
}

uint8_t float_to_channel(float channel) {
    return static_cast<uint8_t>(std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct PresetChoice {
    const char* label;
    PostProcessProfile (*create)();
};

constexpr std::array<PresetChoice, 6> presets{{
    {"Default", &PostProcessProfile::PresetDefault},
    {"Cinematic", &PostProcessProfile::PresetCinematic},
    {"Retro CRT", &PostProcessProfile::PresetRetroCRT},
    {"Underwater", &PostProcessProfile::PresetUnderwater},
    {"Night Vision", &PostProcessProfile::PresetNightVision},
    {"Dreamy Bloom", &PostProcessProfile::PresetDreamyBloom}
}};

PostProcessProfile preset_with_profile_identity(
    const PostProcessProfile& current,
    PostProcessProfile preset
) {
    preset.name = current.name;
    preset.effect_reference_width = current.effect_reference_width;
    preset.effect_reference_height = current.effect_reference_height;
    return preset;
}

const char* matching_preset(const PostProcessProfile& profile) {
    for (const PresetChoice& choice : presets) {
        if (preset_with_profile_identity(profile, choice.create()) == profile) return choice.label;
    }
    return "Custom";
}

void reset_reference(PostProcessProfile& profile) {
    const PostProcessProfile defaults = PostProcessProfile::PresetDefault();
    profile.effect_reference_width = defaults.effect_reference_width;
    profile.effect_reference_height = defaults.effect_reference_height;
}

void reset_vignette(PostProcessProfile& profile) {
    const PostProcessProfile defaults = PostProcessProfile::PresetDefault();
    profile.vignette_enabled = defaults.vignette_enabled;
    profile.vignette_intensity = defaults.vignette_intensity;
    profile.vignette_color = defaults.vignette_color;
}

void reset_bloom(PostProcessProfile& profile) {
    const PostProcessProfile defaults = PostProcessProfile::PresetDefault();
    profile.bloom_enabled = defaults.bloom_enabled;
    profile.bloom_intensity = defaults.bloom_intensity;
    profile.bloom_radius = defaults.bloom_radius;
}

void reset_chromatic(PostProcessProfile& profile) {
    const PostProcessProfile defaults = PostProcessProfile::PresetDefault();
    profile.chromatic_enabled = defaults.chromatic_enabled;
    profile.chromatic_offset = defaults.chromatic_offset;
}

void reset_blur(PostProcessProfile& profile) {
    const PostProcessProfile defaults = PostProcessProfile::PresetDefault();
    profile.blur_enabled = defaults.blur_enabled;
    profile.blur_radius = defaults.blur_radius;
}

void reset_color_grading(PostProcessProfile& profile) {
    const PostProcessProfile defaults = PostProcessProfile::PresetDefault();
    profile.color_grading_enabled = defaults.color_grading_enabled;
    profile.color_tint = defaults.color_tint;
    profile.shadow_tint = defaults.shadow_tint;
}

void reset_film_and_crt(PostProcessProfile& profile) {
    const PostProcessProfile defaults = PostProcessProfile::PresetDefault();
    profile.film_grain_enabled = defaults.film_grain_enabled;
    profile.film_grain_intensity = defaults.film_grain_intensity;
    profile.crt_enabled = defaults.crt_enabled;
    profile.crt_scanline_intensity = defaults.crt_scanline_intensity;
    profile.crt_scanline_spacing = defaults.crt_scanline_spacing;
    profile.crt_flicker = defaults.crt_flicker;
}

void reset_button(const char* id, PostProcessProfile& profile, void (*reset)(PostProcessProfile&)) {
    const auto& metrics = EditorTheme::metrics();
    const float button_width = metrics.toolbar_height - 6.0f * metrics.scale;
    ImGui::SameLine(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - button_width
    ));
    if (EditorUi::toolbar_button(id, EditorIcon::History, "Reset this section")) {
        reset(profile);
    }
}

template <typename DrawValue>
bool property_row(const char* id, const char* label, DrawValue&& draw_value) {
    bool changed = false;
    const auto& metrics = EditorTheme::metrics();
    ImGui::PushID(id);
    if (ImGui::BeginTable(
            "##property", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn(
            "Label", ImGuiTableColumnFlags_WidthFixed, 112.0f * metrics.scale
        );
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        changed = draw_value();
        ImGui::EndTable();
    }
    ImGui::PopID();
    return changed;
}

} // namespace

PostProcessTunerPanel::PostProcessTunerPanel(
    SceneManager* scene_manager,
    std::shared_ptr<EditorCommandHistory> command_history
)
    : m_scene_manager(scene_manager),
      m_command_history(std::move(command_history)) {}

bool PostProcessTunerPanel::owns_draft_history_focus() const {
    return is_open && m_has_keyboard_focus && m_draft_profile.has_value();
}

bool PostProcessTunerPanel::can_undo_draft() const {
    return !m_draft_undo.empty() || m_edit_before.has_value();
}

bool PostProcessTunerPanel::can_redo_draft() const {
    return !m_draft_redo.empty();
}

void PostProcessTunerPanel::record_draft_edit(const PostProcessProfile& before) {
    if (!m_draft_profile || before == *m_draft_profile) return;
    if (m_draft_undo.empty() || m_draft_undo.back() != before) {
        m_draft_undo.push_back(before);
        constexpr std::size_t maximum_history = 64;
        if (m_draft_undo.size() > maximum_history) m_draft_undo.erase(m_draft_undo.begin());
    }
    m_draft_redo.clear();
    m_is_dirty = !m_saved_profile || *m_draft_profile != *m_saved_profile;
}

void PostProcessTunerPanel::commit_draft_gesture() {
    if (!m_edit_before || !m_draft_profile || ImGui::IsAnyItemActive()) return;
    const PostProcessProfile before = *m_edit_before;
    m_edit_before.reset();
    record_draft_edit(before);
}

void PostProcessTunerPanel::undo_from_editor_action() {
    if (!m_draft_profile) return;
    if (m_edit_before) {
        const PostProcessProfile before = *m_edit_before;
        m_edit_before.reset();
        record_draft_edit(before);
    }
    if (m_draft_undo.empty()) return;
    m_draft_redo.push_back(*m_draft_profile);
    *m_draft_profile = std::move(m_draft_undo.back());
    m_draft_undo.pop_back();
    m_is_dirty = !m_saved_profile || *m_draft_profile != *m_saved_profile;
    apply_preview();
}

void PostProcessTunerPanel::redo_from_editor_action() {
    if (!m_draft_profile || m_draft_redo.empty()) return;
    m_edit_before.reset();
    m_draft_undo.push_back(*m_draft_profile);
    *m_draft_profile = std::move(m_draft_redo.back());
    m_draft_redo.pop_back();
    m_is_dirty = !m_saved_profile || *m_draft_profile != *m_saved_profile;
    apply_preview();
}

bool PostProcessTunerPanel::observed_target_is_bound() const {
    return m_observed_scene == m_bound_scene &&
        m_observed_profile == m_bound_profile &&
        m_observed_pipeline == m_bound_pipeline &&
        m_observed_handle == m_bound_handle;
}

bool PostProcessTunerPanel::observed_target_is_deferred() const {
    return m_observed_scene == m_deferred_scene &&
        m_observed_profile == m_deferred_profile &&
        m_observed_handle == m_deferred_handle;
}

void PostProcessTunerPanel::bind_observed_target(AssetDatabase& database) {
    m_bound_scene = m_observed_scene;
    m_bound_profile = m_observed_profile;
    m_bound_pipeline = m_observed_pipeline;
    m_bound_handle = m_observed_handle;
    m_bound_filepath = m_bound_handle.is_valid() ? database.resolve(m_bound_handle) : "";
    m_pending_target_change = false;
    m_deferred_scene = nullptr;
    m_deferred_profile = nullptr;
    m_deferred_handle = {};
    m_edit_before.reset();
    m_draft_undo.clear();
    m_draft_redo.clear();
    m_error.clear();
    m_preview_draft = true;

    if (m_bound_profile) {
        m_saved_profile = *m_bound_profile;
        m_draft_profile = *m_bound_profile;
    } else {
        m_saved_profile.reset();
        m_draft_profile.reset();
    }
    m_is_dirty = false;
    apply_preview();
}

void PostProcessTunerPanel::observe_target(
    Scene* scene,
    PostProcessProfile* profile,
    PostProcessPipeline* pipeline,
    AssetHandle handle,
    AssetDatabase& database
) {
    m_observed_scene = scene;
    m_observed_profile = profile;
    m_observed_pipeline = pipeline;
    m_observed_handle = handle;

    if (!m_draft_profile) {
        bind_observed_target(database);
        return;
    }
    if (observed_target_is_bound()) {
        m_pending_target_change = false;
        m_deferred_scene = nullptr;
        m_deferred_profile = nullptr;
        m_deferred_handle = {};
        return;
    }
    if (!m_is_dirty) {
        bind_observed_target(database);
        return;
    }
    if (!observed_target_is_deferred()) {
        m_deferred_scene = nullptr;
        m_deferred_profile = nullptr;
        m_deferred_handle = {};
        m_pending_target_change = true;
    }
}

void PostProcessTunerPanel::apply_preview() {
    if (!observed_target_is_bound() || !m_bound_pipeline ||
        !m_saved_profile || !m_draft_profile) return;
    const PostProcessProfile& preview = m_preview_draft ? *m_draft_profile : *m_saved_profile;
    preview.apply_to_pipeline(*m_bound_pipeline);
}

void PostProcessTunerPanel::apply_runtime_profile(
    AssetHandle handle,
    const PostProcessProfile& profile
) {
    if (!m_scene_manager || m_scene_manager->get_post_process_profile_handle() != handle) return;
    PostProcessProfile* runtime_profile = m_scene_manager->get_editable_post_process_profile();
    PostProcessPipeline* pipeline = m_scene_manager->get_post_process_pipeline();
    if (runtime_profile) *runtime_profile = profile;
    if (pipeline) profile.apply_to_pipeline(*pipeline);
    if (m_scene_manager->get_active_scene() == m_bound_scene && m_bound_handle == handle) {
        m_bound_profile = runtime_profile;
        m_bound_pipeline = pipeline;
        m_observed_profile = runtime_profile;
        m_observed_pipeline = pipeline;
    }
}

void PostProcessTunerPanel::accept_saved_asset(
    const std::string& filepath,
    AssetHandle handle,
    const PostProcessProfile& profile
) {
    apply_runtime_profile(handle, profile);
    if (filepath != m_bound_filepath || handle != m_bound_handle) return;

    const bool keep_local_draft = m_is_dirty && m_draft_profile && *m_draft_profile != profile;
    m_saved_profile = profile;
    if (!keep_local_draft) {
        m_draft_profile = profile;
        m_edit_before.reset();
        m_draft_undo.clear();
        m_draft_redo.clear();
    }
    m_is_dirty = m_draft_profile && *m_draft_profile != *m_saved_profile;
    apply_preview();
}

bool PostProcessTunerPanel::save_draft() {
    if (!m_draft_profile) return false;
    commit_draft_gesture();
    if (const auto validation = m_draft_profile->validation_error()) {
        m_error = *validation;
        return false;
    }
    if (!m_bound_handle.is_valid() || m_bound_filepath.empty()) {
        m_error = "The active profile is runtime-only. Assign a .postprocess asset to save it.";
        return false;
    }

    AssetDatabase& database = AssetDatabase::get();
    const AssetRecord* record = database.find(m_bound_handle);
    if (!record || record->type != AssetType::PostProcessProfile ||
        record->status != AssetStatus::Ready) {
        m_error = "The selected post-process asset is not ready.";
        return false;
    }

    const std::string before_text = FileSystem::read_text(m_bound_filepath);
    PostProcessProfile before_profile;
    if (before_text.empty() || !before_profile.from_json(before_text)) {
        m_error = "The current asset could not be read safely.";
        return false;
    }
    const PostProcessProfile after_profile = *m_draft_profile;
    const std::string filepath = m_bound_filepath;
    const AssetHandle handle = m_bound_handle;

    bool saved = false;
    if (m_command_history) {
        saved = m_command_history->execute_command(std::make_unique<ModifyAssetCommand>(
            &database,
            filepath,
            before_text,
            after_profile.to_json(),
            "Save Post-Process Profile",
            [this, filepath, handle, before_profile]() {
                accept_saved_asset(filepath, handle, before_profile);
            },
            [this, filepath, handle, after_profile]() {
                accept_saved_asset(filepath, handle, after_profile);
            }
        ));
    } else {
        saved = after_profile.save_asset(filepath, database);
        if (saved) accept_saved_asset(filepath, handle, after_profile);
    }

    if (!saved) {
        m_error = "Could not save the post-process asset.";
        return false;
    }
    m_error.clear();
    return true;
}

void PostProcessTunerPanel::discard_draft() {
    if (!m_saved_profile || !m_draft_profile) return;
    *m_draft_profile = *m_saved_profile;
    m_edit_before.reset();
    m_draft_undo.clear();
    m_draft_redo.clear();
    m_is_dirty = false;
    m_preview_draft = true;
    m_error.clear();
    apply_preview();
}

void PostProcessTunerPanel::render_top_bar(AssetDatabase& database) {
    const auto& metrics = EditorTheme::metrics();
    const AssetRecord* record = m_bound_handle.is_valid() ? database.find(m_bound_handle) : nullptr;
    const char* target_name = m_draft_profile ? m_draft_profile->name.c_str() : "No profile";
    const float available_width = ImGui::GetContentRegionAvail().x;
    const bool wide = available_width >= 760.0f * metrics.scale;
    const float actions_width = (wide ? 410.0f : 220.0f) * metrics.scale;

    if (ImGui::BeginTable(
            "##post_process_header", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, actions_width);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        EditorUi::badge("PROFILE", EditorUi::BadgeTone::Accent);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(target_name);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s",
                record ? record->source_path.c_str() : "Runtime-only profile"
            );
        }

        ImGui::TableSetColumnIndex(1);
        const bool preview_available = observed_target_is_bound() && m_bound_pipeline && m_saved_profile;
        ImGui::BeginDisabled(!preview_available);
        bool original = !m_preview_draft;
        if (ImGui::RadioButton("A##post_original", original)) {
            m_preview_draft = false;
            apply_preview();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Original saved profile");
        ImGui::SameLine();
        if (ImGui::RadioButton("B##post_draft", m_preview_draft)) {
            m_preview_draft = true;
            apply_preview();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Current draft");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (EditorUi::toolbar_button(
                "post_process_undo", EditorIcon::Undo, "Undo post-process edit",
                false, can_undo_draft())) {
            undo_from_editor_action();
        }
        ImGui::SameLine();
        if (EditorUi::toolbar_button(
                "post_process_redo", EditorIcon::Redo, "Redo post-process edit",
                false, can_redo_draft())) {
            redo_from_editor_action();
        }
        ImGui::SameLine();

        const std::optional<std::string> validation = m_draft_profile
            ? m_draft_profile->validation_error()
            : std::optional<std::string>{};
        const char* status = !m_draft_profile
            ? "NO PROFILE"
            : (validation ? "INVALID" : (m_is_dirty ? "UNSAVED" : "SAVED"));
        const EditorUi::BadgeTone tone = !m_draft_profile
            ? EditorUi::BadgeTone::Neutral
            : (validation
                ? EditorUi::BadgeTone::Error
                : (m_is_dirty ? EditorUi::BadgeTone::Warning : EditorUi::BadgeTone::Success));
        EditorUi::badge(status, tone);
        ImGui::SameLine();

        const bool can_save = m_is_dirty && !validation && record &&
            record->type == AssetType::PostProcessProfile &&
            record->status == AssetStatus::Ready;
        if (wide) {
            if (EditorUi::action_button(
                    "save_post_process_draft", EditorIcon::Save, "Save Changes",
                    EditorUi::ButtonTone::Primary, can_save)) {
                if (!save_draft()) m_error_popup_requested = true;
            }
            ImGui::SameLine();
            if (EditorUi::action_button(
                    "discard_post_process_draft", EditorIcon::History, "Discard",
                    EditorUi::ButtonTone::Neutral, m_is_dirty)) {
                discard_draft();
            }
        } else {
            if (EditorUi::toolbar_button(
                    "save_post_process_draft", EditorIcon::Save, "Save Changes",
                    false, can_save)) {
                if (!save_draft()) m_error_popup_requested = true;
            }
            ImGui::SameLine();
            if (EditorUi::toolbar_button(
                    "discard_post_process_draft", EditorIcon::History, "Discard changes",
                    false, m_is_dirty)) {
                discard_draft();
            }
        }
        ImGui::EndTable();
    }

    if (m_draft_profile) {
        if (const auto validation = m_draft_profile->validation_error()) {
            ImGui::TextColored(EditorTheme::palette().error, "%s", validation->c_str());
        }
    }
    if (!observed_target_is_bound() && m_is_dirty) {
        ImGui::TextColored(
            EditorTheme::palette().warning,
            "The active scene changed. Save or discard this draft before switching profiles."
        );
    }
}

void PostProcessTunerPanel::render_settings(
    PostProcessProfile& profile,
    PostProcessPipeline* pipeline
) {
    ImGui::SetNextItemWidth(-1.0f);
    const char* active_preset = matching_preset(profile);
    const std::string preset_preview = std::string("Preset: ") + active_preset;
    if (ImGui::BeginCombo("##post_process_preset", preset_preview.c_str())) {
        for (const PresetChoice& choice : presets) {
            const bool selected = std::string_view(active_preset) == choice.label;
            if (ImGui::Selectable(choice.label, selected)) {
                profile = preset_with_profile_identity(profile, choice.create());
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apply preset");

    if (!ImGui::BeginTabBar("PostProcessSections", ImGuiTabBarFlags_FittingPolicyScroll)) return;

    if (ImGui::BeginTabItem("Profile")) {
        std::array<char, 128> name{};
        std::snprintf(name.data(), name.size(), "%s", profile.name.c_str());
        if (property_row("profile_name", "Name", [&]() {
                return ImGui::InputText("##value", name.data(), name.size());
            })) {
            profile.name = name.data();
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Effect reference");
        reset_button("reset_post_reference", profile, reset_reference);
        if (property_row("reference_width", "Width", [&]() {
                return ImGui::DragInt(
                    "##value", &profile.effect_reference_width, 1.0f, 1, 16384
                );
            })) {
            profile.effect_reference_width = std::clamp(profile.effect_reference_width, 1, 16384);
        }
        if (property_row("reference_height", "Height", [&]() {
                return ImGui::DragInt(
                    "##value", &profile.effect_reference_height, 1.0f, 1, 16384
                );
            })) {
            profile.effect_reference_height = std::clamp(profile.effect_reference_height, 1, 16384);
        }
        if (pipeline) {
            const PostProcessContext& context = pipeline->get_context();
            ImGui::TextDisabled(
                "Output %d x %d  |  %.2fx",
                context.output_width,
                context.output_height,
                context.pixel_scale()
            );
        }
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Vignette")) {
        ImGui::Checkbox("Enabled##vignette", &profile.vignette_enabled);
        reset_button("reset_vignette", profile, reset_vignette);
        property_row("vignette_intensity", "Intensity", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.vignette_intensity, 0.0f, 1.0f, "%.2f"
            );
        });
        float color[4] = {
            channel_to_float(profile.vignette_color.r),
            channel_to_float(profile.vignette_color.g),
            channel_to_float(profile.vignette_color.b),
            channel_to_float(profile.vignette_color.a)
        };
        if (property_row("vignette_color", "Color", [&]() {
                return ImGui::ColorEdit4("##value", color);
            })) {
            profile.vignette_color = SDL_Color{
                float_to_channel(color[0]), float_to_channel(color[1]),
                float_to_channel(color[2]), float_to_channel(color[3])
            };
        }
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Bloom")) {
        ImGui::Checkbox("Enabled##bloom", &profile.bloom_enabled);
        reset_button("reset_bloom", profile, reset_bloom);
        property_row("bloom_intensity", "Intensity", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.bloom_intensity, 0.0f, 2.0f, "%.2f"
            );
        });
        property_row("bloom_radius", "Radius", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.bloom_radius, 1.0f, 12.0f, "%.1f px"
            );
        });
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Chromatic")) {
        ImGui::Checkbox("Enabled##chromatic", &profile.chromatic_enabled);
        reset_button("reset_chromatic", profile, reset_chromatic);
        property_row("chromatic_offset", "Offset", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.chromatic_offset, 0.0f, 15.0f, "%.1f px"
            );
        });
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Blur")) {
        ImGui::Checkbox("Enabled##blur", &profile.blur_enabled);
        reset_button("reset_blur", profile, reset_blur);
        property_row("blur_radius", "Radius", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.blur_radius, 0.0f, 6.0f, "%.1f px"
            );
        });
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Color")) {
        ImGui::Checkbox("Enabled##color_grading", &profile.color_grading_enabled);
        reset_button("reset_color_grading", profile, reset_color_grading);
        float tint[4] = {
            channel_to_float(profile.color_tint.r),
            channel_to_float(profile.color_tint.g),
            channel_to_float(profile.color_tint.b),
            channel_to_float(profile.color_tint.a)
        };
        if (property_row("color_tint", "Tint", [&]() {
                return ImGui::ColorEdit4("##value", tint);
            })) {
            profile.color_tint = SDL_Color{
                float_to_channel(tint[0]), float_to_channel(tint[1]),
                float_to_channel(tint[2]), float_to_channel(tint[3])
            };
        }
        float shadow_tint[4] = {
            channel_to_float(profile.shadow_tint.r),
            channel_to_float(profile.shadow_tint.g),
            channel_to_float(profile.shadow_tint.b),
            channel_to_float(profile.shadow_tint.a)
        };
        if (property_row("shadow_tint", "Shadow Tint", [&]() {
                return ImGui::ColorEdit4("##value", shadow_tint);
            })) {
            profile.shadow_tint = SDL_Color{
                float_to_channel(shadow_tint[0]), float_to_channel(shadow_tint[1]),
                float_to_channel(shadow_tint[2]), float_to_channel(shadow_tint[3])
            };
        }
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Film / CRT")) {
        ImGui::Checkbox("Film Grain", &profile.film_grain_enabled);
        reset_button("reset_film_crt", profile, reset_film_and_crt);
        property_row("grain_intensity", "Grain Intensity", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.film_grain_intensity, 0.0f, 1.0f, "%.2f"
            );
        });
        ImGui::Separator();
        ImGui::Checkbox("CRT", &profile.crt_enabled);
        property_row("scanline_intensity", "Scanline Intensity", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.crt_scanline_intensity, 0.0f, 1.0f, "%.2f"
            );
        });
        property_row("scanline_spacing", "Scanline Spacing", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.crt_scanline_spacing, 2.0f, 8.0f, "%.1f px"
            );
        });
        property_row("crt_flicker", "Flicker", [&]() {
            return ImGui::SliderFloat(
                "##value", &profile.crt_flicker, 0.0f, 0.2f, "%.3f"
            );
        });
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}

void PostProcessTunerPanel::render_pending_change_modal(AssetDatabase& database) {
    if ((m_pending_close || m_pending_target_change) &&
        !ImGui::IsPopupOpen("Unsaved Post-Process Profile")) {
        ImGui::OpenPopup("Unsaved Post-Process Profile");
    }
    if (!ImGui::BeginPopupModal(
            "Unsaved Post-Process Profile", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("This post-process profile has unsaved changes.");
    ImGui::TextDisabled(
        "%s",
        m_pending_close
            ? "Save or discard them before closing."
            : "Save or discard them before switching to the active profile."
    );
    if (!m_error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(EditorTheme::palette().error, "%s", m_error.c_str());
    }
    ImGui::Spacing();

    const bool can_save = m_bound_handle.is_valid() && m_draft_profile &&
        !m_draft_profile->validation_error();
    if (EditorUi::action_button(
            "save_before_post_process_change", EditorIcon::Save, "Save",
            EditorUi::ButtonTone::Primary, can_save)) {
        if (save_draft()) {
            const bool close = m_pending_close;
            const bool switch_target = m_pending_target_change;
            m_pending_close = false;
            m_pending_target_change = false;
            ImGui::CloseCurrentPopup();
            if (switch_target) bind_observed_target(database);
            if (close) is_open = false;
        }
    }
    ImGui::SameLine();
    if (EditorUi::action_button(
            "discard_before_post_process_change", EditorIcon::History, "Discard",
            EditorUi::ButtonTone::Danger)) {
        const bool close = m_pending_close;
        const bool switch_target = m_pending_target_change;
        discard_draft();
        m_pending_close = false;
        m_pending_target_change = false;
        ImGui::CloseCurrentPopup();
        if (switch_target) bind_observed_target(database);
        if (close) is_open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_pending_target_change ? "Keep Editing" : "Cancel")) {
        if (m_pending_target_change) {
            m_deferred_scene = m_observed_scene;
            m_deferred_profile = m_observed_profile;
            m_deferred_handle = m_observed_handle;
        }
        m_pending_close = false;
        m_pending_target_change = false;
        m_error.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void PostProcessTunerPanel::render_error_modal() {
    if (m_error_popup_requested) {
        ImGui::OpenPopup("Post-Process Save Failed");
        m_error_popup_requested = false;
    }
    if (ImGui::BeginPopupModal(
            "Post-Process Save Failed", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_error.c_str());
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void PostProcessTunerPanel::on_gui() {
    if (!is_open) {
        m_has_keyboard_focus = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);
    bool window_open = true;
    const bool visible = ImGui::Begin(m_name.c_str(), &window_open);
    m_has_keyboard_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!window_open) {
        if (m_is_dirty) {
            m_pending_close = true;
            is_open = true;
        } else {
            is_open = false;
        }
    }

    AssetDatabase& database = AssetDatabase::get();
    Scene* scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    PostProcessPipeline* pipeline = m_scene_manager
        ? m_scene_manager->get_post_process_pipeline()
        : nullptr;
    PostProcessProfile* profile = m_scene_manager
        ? m_scene_manager->get_editable_post_process_profile()
        : nullptr;
    const AssetHandle handle = m_scene_manager
        ? m_scene_manager->get_post_process_profile_handle()
        : AssetHandle{};
    observe_target(scene, profile, pipeline, handle, database);

    if (!visible) {
        render_pending_change_modal(database);
        render_error_modal();
        ImGui::End();
        return;
    }

    render_top_bar(database);
    ImGui::Separator();
    if (!m_draft_profile) {
        EditorUi::empty_state(
            EditorIcon::Material,
            scene ? "No post-process profile" : "No active scene",
            scene
                ? "Assign a .postprocess profile to the primary Camera"
                : "Open a scene to tune post-processing"
        );
        render_pending_change_modal(database);
        render_error_modal();
        ImGui::End();
        return;
    }

    PostProcessProfile& draft = *m_draft_profile;
    const PostProcessProfile frame_before = draft;
    render_settings(draft, observed_target_is_bound() ? m_bound_pipeline : nullptr);
    if (draft != frame_before) {
        if (!m_edit_before) m_edit_before = frame_before;
        m_is_dirty = !m_saved_profile || draft != *m_saved_profile;
        if (m_preview_draft) apply_preview();
    }
    commit_draft_gesture();

    ImGui::Separator();
    ImGui::TextDisabled(
        "%s preview%s",
        m_preview_draft ? "Draft" : "Original",
        observed_target_is_bound() ? "" : " unavailable for the inactive target"
    );

    render_pending_change_modal(database);
    render_error_modal();
    ImGui::End();
}

} // namespace Engine
