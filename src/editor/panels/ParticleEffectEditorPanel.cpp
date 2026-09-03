#include "ParticleEffectEditorPanel.hpp"

#include "../EditorTheme.hpp"
#include "../commands/AssetFileCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"

#include <algorithm>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <utility>

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

bool ParticleEffectEditorPanel::owns_draft_history_focus() const {
    return is_open && m_has_keyboard_focus && m_current_asset.has_value();
}

bool ParticleEffectEditorPanel::can_undo_draft() const {
    return !m_draft_undo.empty() || m_edit_before.has_value();
}

bool ParticleEffectEditorPanel::can_redo_draft() const {
    return !m_draft_redo.empty();
}

void ParticleEffectEditorPanel::record_draft_edit(const ParticleEffectAsset& before) {
    if (!m_current_asset || before == *m_current_asset) return;
    if (m_draft_undo.empty() || m_draft_undo.back() != before) {
        m_draft_undo.push_back(before);
        constexpr std::size_t maximum_history = 64;
        if (m_draft_undo.size() > maximum_history) m_draft_undo.erase(m_draft_undo.begin());
    }
    m_draft_redo.clear();
    m_is_dirty = !m_saved_asset || *m_current_asset != *m_saved_asset;
}

void ParticleEffectEditorPanel::commit_draft_gesture() {
    if (!m_edit_before || !m_current_asset || ImGui::IsAnyItemActive()) return;
    const ParticleEffectAsset before = *m_edit_before;
    m_edit_before.reset();
    record_draft_edit(before);
}

void ParticleEffectEditorPanel::undo_from_editor_action() {
    if (!m_current_asset) return;
    if (m_edit_before) {
        const ParticleEffectAsset before = *m_edit_before;
        m_edit_before.reset();
        record_draft_edit(before);
    }
    if (m_draft_undo.empty()) return;
    m_draft_redo.push_back(*m_current_asset);
    *m_current_asset = std::move(m_draft_undo.back());
    m_draft_undo.pop_back();
    m_is_dirty = !m_saved_asset || *m_current_asset != *m_saved_asset;
    reset_preview(m_preview_playing);
}

void ParticleEffectEditorPanel::redo_from_editor_action() {
    if (!m_current_asset || m_draft_redo.empty()) return;
    m_edit_before.reset();
    m_draft_undo.push_back(*m_current_asset);
    *m_current_asset = std::move(m_draft_redo.back());
    m_draft_redo.pop_back();
    m_is_dirty = !m_saved_asset || *m_current_asset != *m_saved_asset;
    reset_preview(m_preview_playing);
}

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

void ParticleEffectEditorPanel::reset_preview(bool play) {
    m_preview_system.clear();
    m_preview_accumulator = 0.0f;
    m_preview_distance_accumulator = 0.0f;
    m_preview_elapsed = 0.0f;
    m_preview_motion_elapsed = 0.0f;
    m_preview_delay = m_current_asset ? m_current_asset->emission.start_delay : 0.0f;
    m_preview_burst_pending = true;
    m_preview_has_previous_emitter_position = false;
    if (m_preview_motion != PreviewMotion::FollowCursor) {
        m_preview_emitter_position = {};
    }
    m_preview_playing = play;
}

void ParticleEffectEditorPanel::load_asset(AssetHandle handle) {
    if (!handle.is_valid()) return;
    const auto& database = AssetDatabase::get();
    const AssetRecord* record = database.find(handle);
    if (!record || record->type != AssetType::ParticleEffect) return;

    std::string error;
    auto loaded = ParticleEffectAsset::load_from_file(database.resolve(handle), &error);
    m_current_handle = handle;
    m_pending_handle.reset();
    m_pending_close = false;
    m_edit_before.reset();
    m_draft_undo.clear();
    m_draft_redo.clear();
    if (loaded) {
        m_current_asset = std::move(*loaded);
        m_saved_asset = m_current_asset;
        m_current_filepath = (database.project_root() / record->source_path).string();
        m_is_dirty = false;
        m_error.clear();
        reset_preview();
    } else {
        m_current_asset.reset();
        m_saved_asset.reset();
        m_current_filepath = database.resolve(handle);
        m_is_dirty = false;
        m_error = std::move(error);
        reset_preview(false);
    }
}

void ParticleEffectEditorPanel::open_asset(AssetHandle handle) {
    if (!handle.is_valid() || handle == m_current_handle) return;
    if (m_current_asset && m_is_dirty) {
        m_pending_handle = handle;
        m_pending_close = false;
        return;
    }
    load_asset(handle);
}

bool ParticleEffectEditorPanel::save_draft() {
    if (!m_current_asset || m_current_filepath.empty()) return false;
    commit_draft_gesture();

    ParticleEffectAsset& asset = *m_current_asset;
    std::string error;
    if (!asset.validate(&error)) {
        m_error = std::move(error);
        return false;
    }

    AssetDatabase& database = AssetDatabase::get();
    bool saved = false;
    if (m_command_history) {
        const std::string before = FileSystem::read_text(m_current_filepath);
        const std::string after = asset.to_json().dump(2);
        const auto before_json = nlohmann::json::parse(before, nullptr, false);
        const auto before_asset = before_json.is_discarded()
            ? std::optional<ParticleEffectAsset>{}
            : ParticleEffectAsset::from_json(before_json);
        const ParticleEffectAsset after_asset = asset;
        const std::string saved_path = m_current_filepath;
        saved = m_command_history->execute_command(std::make_unique<ModifyAssetCommand>(
            &database,
            saved_path,
            before,
            after,
            "Save Particle Effect",
            [this, saved_path, before_asset]() {
                if (m_current_filepath != saved_path || !before_asset) return;
                m_current_asset = *before_asset;
                m_saved_asset = *before_asset;
                m_is_dirty = false;
                m_draft_undo.clear();
                m_draft_redo.clear();
                reset_preview();
            },
            [this, saved_path, after_asset]() {
                if (m_current_filepath != saved_path) return;
                m_current_asset = after_asset;
                m_saved_asset = after_asset;
                m_is_dirty = false;
                m_draft_undo.clear();
                m_draft_redo.clear();
                reset_preview();
            }
        ));
    } else {
        saved = asset.save_asset(m_current_filepath, database, &error);
        if (!saved) m_error = std::move(error);
    }

    if (!saved) return false;
    m_saved_asset = asset;
    m_is_dirty = false;
    m_edit_before.reset();
    m_draft_undo.clear();
    m_draft_redo.clear();
    m_error.clear();
    return true;
}

void ParticleEffectEditorPanel::discard_draft() {
    if (!m_current_asset || !m_saved_asset) return;
    *m_current_asset = *m_saved_asset;
    m_is_dirty = false;
    m_edit_before.reset();
    m_draft_undo.clear();
    m_draft_redo.clear();
    m_error.clear();
    reset_preview();
}

void ParticleEffectEditorPanel::render_top_bar(AssetDatabase& database) {
    const auto& metrics = EditorTheme::metrics();
    const AssetRecord* current_record = m_current_handle.is_valid()
        ? database.find(m_current_handle)
        : nullptr;
    const char* preview_name = current_record
        ? current_record->source_path.c_str()
        : "Select a .particle asset";

    if (ImGui::BeginTable(
            "##particle_editor_header", 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "History", ImGuiTableColumnFlags_WidthFixed, 70.0f * metrics.scale
        );
        ImGui::TableSetupColumn(
            "Actions", ImGuiTableColumnFlags_WidthFixed, 260.0f * metrics.scale
        );
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        EditorUi::badge("ASSET", EditorUi::BadgeTone::Accent);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##particle_asset", preview_name)) {
            for (const auto& record : database.records()) {
                if (record.type != AssetType::ParticleEffect ||
                    record.status != AssetStatus::Ready) continue;
                const bool selected = record.handle == m_current_handle;
                if (ImGui::Selectable(record.source_path.c_str(), selected)) {
                    open_asset(record.handle);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::TableSetColumnIndex(1);
        if (EditorUi::toolbar_button(
                "particle_undo", EditorIcon::Undo, "Undo particle edit",
                false, can_undo_draft())) {
            undo_from_editor_action();
        }
        ImGui::SameLine();
        if (EditorUi::toolbar_button(
                "particle_redo", EditorIcon::Redo, "Redo particle edit",
                false, can_redo_draft())) {
            redo_from_editor_action();
        }

        ImGui::TableSetColumnIndex(2);
        std::string validation_error;
        const bool valid = m_current_asset && m_current_asset->validate(&validation_error);
        const char* status = !m_current_asset
            ? "NO ASSET"
            : (!valid ? "INVALID" : (m_is_dirty ? "UNSAVED" : "SAVED"));
        const EditorUi::BadgeTone tone = !m_current_asset
            ? EditorUi::BadgeTone::Neutral
            : (!valid
                ? EditorUi::BadgeTone::Error
                : (m_is_dirty ? EditorUi::BadgeTone::Warning : EditorUi::BadgeTone::Success));
        EditorUi::badge(status, tone);
        ImGui::SameLine();
        if (EditorUi::action_button(
                "save_particle_draft", EditorIcon::Save, "Save Changes",
                EditorUi::ButtonTone::Primary, m_is_dirty && valid)) {
            if (!save_draft()) m_error_popup_requested = true;
        }
        ImGui::SameLine();
        if (EditorUi::action_button(
                "discard_particle_draft", EditorIcon::History, "Discard",
                EditorUi::ButtonTone::Neutral, m_is_dirty)) {
            discard_draft();
        }
        ImGui::EndTable();
    }
}

void ParticleEffectEditorPanel::render_pending_change_modal() {
    if ((m_pending_handle || m_pending_close) &&
        !ImGui::IsPopupOpen("Unsaved Particle Effect")) {
        ImGui::OpenPopup("Unsaved Particle Effect");
    }
    if (!ImGui::BeginPopupModal(
            "Unsaved Particle Effect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("This particle effect has unsaved changes.");
    ImGui::TextDisabled("Save or discard them before continuing.");
    if (!m_error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(EditorTheme::palette().error, "%s", m_error.c_str());
    }
    ImGui::Spacing();
    if (EditorUi::action_button(
            "save_before_particle_change", EditorIcon::Save, "Save",
            EditorUi::ButtonTone::Primary)) {
        if (save_draft()) {
            const std::optional<AssetHandle> next = m_pending_handle;
            const bool close = m_pending_close;
            m_pending_handle.reset();
            m_pending_close = false;
            ImGui::CloseCurrentPopup();
            if (next) load_asset(*next);
            if (close) is_open = false;
        }
    }
    ImGui::SameLine();
    if (EditorUi::action_button(
            "discard_before_particle_change", EditorIcon::History, "Discard",
            EditorUi::ButtonTone::Danger)) {
        const std::optional<AssetHandle> next = m_pending_handle;
        const bool close = m_pending_close;
        discard_draft();
        m_pending_handle.reset();
        m_pending_close = false;
        ImGui::CloseCurrentPopup();
        if (next) load_asset(*next);
        if (close) is_open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_pending_handle.reset();
        m_pending_close = false;
        m_error.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ParticleEffectEditorPanel::render_error_modal() {
    if (m_error_popup_requested) {
        ImGui::OpenPopup("Particle Effect Validation Failed");
        m_error_popup_requested = false;
    }
    if (ImGui::BeginPopupModal(
            "Particle Effect Validation Failed", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_error.c_str());
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ParticleEffectEditorPanel::on_gui() {
    if (!is_open) return;

    bool window_open = true;
    const bool visible = ImGui::Begin(
        "Particle Effect Editor###ParticleEffectEditor", &window_open
    );
    m_has_keyboard_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!window_open) {
        if (m_current_asset && m_is_dirty) {
            m_pending_close = true;
            is_open = true;
        } else {
            is_open = false;
        }
    }
    if (!visible) {
        render_pending_change_modal();
        render_error_modal();
        ImGui::End();
        return;
    }

    AssetDatabase& database = AssetDatabase::get();
    render_top_bar(database);
    ImGui::Separator();

    if (!m_current_asset) {
        if (!m_error.empty()) {
            EditorUi::empty_state(
                EditorIcon::Zap,
                "Could not open particle effect",
                m_error.c_str()
            );
        } else {
            EditorUi::empty_state(
                EditorIcon::Zap,
                "Select a particle effect",
                "Open a .particle asset from the Asset Browser"
            );
        }
        render_pending_change_modal();
        render_error_modal();
        ImGui::End();
        return;
    }

    ParticleEffectAsset& asset = *m_current_asset;
    const ParticleEffectAsset frame_before = asset;
    const auto& metrics = EditorTheme::metrics();
    const float workspace_height = std::max(
        260.0f * metrics.scale, ImGui::GetContentRegionAvail().y
    );
    if (ImGui::BeginTable(
            "ParticleEditorWorkspace", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn(
            "Settings", ImGuiTableColumnFlags_WidthFixed, 330.0f * metrics.scale
        );
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, workspace_height);

        ImGui::TableSetColumnIndex(0);
        render_settings(asset, database);

        ImGui::TableSetColumnIndex(1);
        render_preview_workspace(asset, database, workspace_height);
        ImGui::EndTable();
    }

    if (asset != frame_before) {
        if (!m_edit_before) m_edit_before = frame_before;
        m_is_dirty = !m_saved_asset || asset != *m_saved_asset;
    }
    commit_draft_gesture();
    render_pending_change_modal();
    render_error_modal();
    ImGui::End();
}

} // namespace Engine
