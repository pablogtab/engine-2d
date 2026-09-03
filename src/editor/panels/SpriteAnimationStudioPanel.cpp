#include "SpriteAnimationStudioPanel.hpp"

#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../../renderer/Texture.hpp"
#include "../EditorTheme.hpp"
#include "../commands/EntityCommands.hpp"
#include "../ui/EditorUi.hpp"

#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <string>
#include <utility>

namespace Engine {

namespace {

void reset_animation_runtime(EntityManager& em, EntityID entity) {
    SpriteAnimationSystem::reset_playback(em, entity);
}

AnimationComponent default_animation_draft() {
    AnimationComponent animation;
    animation.columns = 8;
    animation.rows = 3;
    animation.frame_width = 96.0f;
    animation.frame_height = 96.0f;
    animation.add_clip("Idle", 0, 8, 8.0f, true);
    return animation;
}

} // namespace

SpriteAnimationStudioPanel::SpriteAnimationStudioPanel(
    SceneManager* scene_manager,
    std::shared_ptr<EditorSelection> selection
)
    : m_scene_manager(scene_manager),
      m_selection(std::move(selection)) {
    is_open = false;
}

bool SpriteAnimationStudioPanel::owns_draft_history_focus() const {
    return is_open && m_has_keyboard_focus;
}

bool SpriteAnimationStudioPanel::can_undo_draft() const {
    return !m_draft_undo.empty() || m_pending_history_snapshot.has_value() ||
        m_nudge_history_snapshot.has_value();
}

bool SpriteAnimationStudioPanel::can_redo_draft() const {
    return !m_draft_redo.empty();
}

void SpriteAnimationStudioPanel::undo_from_editor_action() {
    undo_draft_edit();
}

void SpriteAnimationStudioPanel::redo_from_editor_action() {
    redo_draft_edit();
}

void SpriteAnimationStudioPanel::open_for_entity(EntityID entity) {
    if (m_target_entity != 0 && m_target_entity != entity && m_draft_dirty) {
        m_pending_target_entity = entity;
    } else if (m_target_entity != entity) {
        m_target_entity = entity;
        clear_draft();
        m_preview_entity = 0;
    }
    is_open = true;
}

void SpriteAnimationStudioPanel::ensure_draft(EntityManager& em, EntityID entity) {
    if (m_draft_manager == &em && m_draft_entity == entity) return;

    m_draft_manager = &em;
    m_draft_entity = entity;
    m_selected_custom_frame = -1;
    m_freeform_drag = FreeformDrag::None;
    m_freeform_drag_before.reset();
    m_draft_undo.clear();
    m_draft_redo.clear();
    m_pending_history_snapshot.reset();
    m_nudge_history_snapshot.reset();
    if (em.has_component<AnimationComponent>(entity)) {
        m_saved_animation = em.get_component<AnimationComponent>(entity);
        m_draft_animation = *m_saved_animation;
    } else {
        m_saved_animation.reset();
        m_draft_animation = default_animation_draft();
    }
    refresh_draft_dirty();
}

void SpriteAnimationStudioPanel::clear_draft() {
    m_draft_manager = nullptr;
    m_draft_entity = 0;
    m_saved_animation.reset();
    m_draft_animation = AnimationComponent{};
    m_draft_dirty = false;
    m_selected_custom_frame = -1;
    m_freeform_drag = FreeformDrag::None;
    m_freeform_drag_before.reset();
    m_draft_undo.clear();
    m_draft_redo.clear();
    m_pending_history_snapshot.reset();
    m_nudge_history_snapshot.reset();
    m_sheet_zoom_fit_pending = true;
}

AnimationComponent& SpriteAnimationStudioPanel::draft_animation(
    EntityManager& em,
    EntityID entity
) {
    ensure_draft(em, entity);
    return m_draft_animation;
}

void SpriteAnimationStudioPanel::refresh_draft_dirty() {
    m_draft_dirty = m_saved_animation
        ? m_draft_animation != *m_saved_animation
        : m_draft_animation != default_animation_draft();
}

void SpriteAnimationStudioPanel::record_draft_edit(const AnimationComponent& before) {
    if (before == m_draft_animation) return;
    if (m_draft_undo.empty() || m_draft_undo.back() != before) {
        m_draft_undo.push_back(before);
        constexpr std::size_t maximum_history = 64;
        if (m_draft_undo.size() > maximum_history) m_draft_undo.erase(m_draft_undo.begin());
    }
    m_draft_redo.clear();
    refresh_draft_dirty();
}

void SpriteAnimationStudioPanel::commit_pending_history() {
    if (!m_pending_history_snapshot) return;
    record_draft_edit(*m_pending_history_snapshot);
    m_pending_history_snapshot.reset();
}

void SpriteAnimationStudioPanel::commit_nudge_edit() {
    if (!m_nudge_history_snapshot) return;
    record_draft_edit(*m_nudge_history_snapshot);
    m_nudge_history_snapshot.reset();
}

void SpriteAnimationStudioPanel::undo_draft_edit() {
    commit_pending_history();
    commit_nudge_edit();
    if (m_draft_undo.empty()) return;
    const int selected_before_undo = m_selected_custom_frame;
    m_draft_redo.push_back(m_draft_animation);
    m_draft_animation = std::move(m_draft_undo.back());
    m_draft_undo.pop_back();
    m_pending_history_snapshot.reset();
    m_freeform_drag = FreeformDrag::None;
    m_freeform_drag_before.reset();
    reset_preview(m_draft_entity, m_draft_animation, m_preview_clip_index);
    if (const AnimationClip* clip = m_draft_animation.get_clip(m_preview_clip_index);
        clip && !clip->custom_frames.empty() && selected_before_undo >= 0) {
        m_selected_custom_frame = std::clamp(
            selected_before_undo,
            0,
            static_cast<int>(clip->custom_frames.size()) - 1
        );
        m_preview_frame = m_selected_custom_frame;
    }
    refresh_draft_dirty();
}

void SpriteAnimationStudioPanel::redo_draft_edit() {
    commit_pending_history();
    commit_nudge_edit();
    if (m_draft_redo.empty()) return;
    const int selected_before_redo = m_selected_custom_frame;
    m_draft_undo.push_back(m_draft_animation);
    m_draft_animation = std::move(m_draft_redo.back());
    m_draft_redo.pop_back();
    m_pending_history_snapshot.reset();
    m_freeform_drag = FreeformDrag::None;
    m_freeform_drag_before.reset();
    reset_preview(m_draft_entity, m_draft_animation, m_preview_clip_index);
    if (const AnimationClip* clip = m_draft_animation.get_clip(m_preview_clip_index);
        clip && !clip->custom_frames.empty() && selected_before_redo >= 0) {
        m_selected_custom_frame = std::clamp(
            selected_before_redo,
            0,
            static_cast<int>(clip->custom_frames.size()) - 1
        );
        m_preview_frame = m_selected_custom_frame;
    }
    refresh_draft_dirty();
}

std::optional<std::string> SpriteAnimationStudioPanel::validate_draft(
    const Texture* texture
) const {
    const AnimationComponent& animation = m_draft_animation;
    if (animation.columns <= 0 || animation.rows <= 0) {
        return "Grid rows and columns must be positive.";
    }
    if (animation.frame_width <= 0.0f || animation.frame_height <= 0.0f) {
        return "Frame width and height must be positive.";
    }
    if (animation.clips.empty()) return "Add at least one animation clip.";
    if (animation.default_clip_index < 0 ||
        animation.default_clip_index >= static_cast<int>(animation.clips.size())) {
        return "Choose a valid default clip.";
    }

    const int grid_frames = animation.columns * animation.rows;
    for (std::size_t index = 0; index < animation.clips.size(); ++index) {
        const AnimationClip& clip = animation.clips[index];
        if (clip.name.empty()) return "Clip names cannot be empty.";
        for (std::size_t other = index + 1; other < animation.clips.size(); ++other) {
            if (clip.name == animation.clips[other].name) {
                return "Clip names must be unique.";
            }
        }
        if (clip.fps <= 0.0f) return "Clip speed must be positive.";
        if (clip.custom_frames.empty()) {
            if (clip.start_frame < 0 || clip.frame_count <= 0 ||
                clip.start_frame + clip.frame_count > grid_frames) {
                return "A grid clip extends beyond the configured spritesheet grid.";
            }
            continue;
        }
        if (!texture) continue;
        const float texture_width = static_cast<float>(texture->get_width());
        const float texture_height = static_cast<float>(texture->get_height());
        for (const SDL_FRect& frame : clip.custom_frames) {
            if (frame.w <= 0.0f || frame.h <= 0.0f || frame.x < 0.0f || frame.y < 0.0f ||
                frame.x + frame.w > texture_width || frame.y + frame.h > texture_height) {
                return "A custom slice extends beyond the texture bounds.";
            }
        }
    }
    return std::nullopt;
}

void SpriteAnimationStudioPanel::save_draft(EntityManager& em, EntityID entity) {
    if ((!m_draft_dirty && m_saved_animation) || !m_command_history ||
        m_draft_manager != &em || m_draft_entity != entity) return;
    const Texture* texture = em.has_component<SpriteComponent>(entity)
        ? em.get_component<SpriteComponent>(entity).texture
        : nullptr;
    if (validate_draft(texture)) return;

    const std::optional<AnimationComponent> before = em.has_component<AnimationComponent>(entity)
        ? std::optional<AnimationComponent>(em.get_component<AnimationComponent>(entity))
        : std::nullopt;
    if (m_command_history->execute_command(
            std::make_unique<SetComponentCommand<AnimationComponent>>(
                &em,
                entity,
                before,
                m_draft_animation,
                "Save Sprite Animation",
                reset_animation_runtime
            ))) {
        m_saved_animation = m_draft_animation;
        m_draft_undo.clear();
        m_draft_redo.clear();
        m_pending_history_snapshot.reset();
        m_nudge_history_snapshot.reset();
        refresh_draft_dirty();
    }
}

void SpriteAnimationStudioPanel::discard_draft() {
    if (m_saved_animation) {
        m_draft_animation = *m_saved_animation;
    } else {
        m_draft_animation = default_animation_draft();
    }
    m_selected_custom_frame = -1;
    m_freeform_drag = FreeformDrag::None;
    m_freeform_drag_before.reset();
    m_draft_undo.clear();
    m_draft_redo.clear();
    m_pending_history_snapshot.reset();
    m_nudge_history_snapshot.reset();
    reset_preview(m_draft_entity, m_draft_animation);
    refresh_draft_dirty();
}

void SpriteAnimationStudioPanel::reset_preview(
    EntityID entity,
    const AnimationComponent& animation,
    int clip_index
) {
    m_preview_entity = entity;
    m_preview_clip_index = clip_index >= 0 ? clip_index : animation.default_clip_index;
    m_preview_clip_index = std::clamp(
        m_preview_clip_index,
        0,
        std::max(0, static_cast<int>(animation.clips.size()) - 1)
    );
    const AnimationClip* clip = animation.get_clip(m_preview_clip_index);
    const int relative_frame = clip ? clip->first_relative_frame() : 0;
    m_preview_frame = clip
        ? (clip->custom_frames.empty() ? clip->start_frame + relative_frame : relative_frame)
        : 0;
    m_preview_direction = clip ? clip->initial_direction() : 1;
    m_selected_custom_frame = clip && !clip->custom_frames.empty() ? relative_frame : -1;
    if (clip && !clip->custom_frames.empty()) m_slicer_mode_freeform = true;
    m_preview_timer = 0.0f;
    m_preview_playing = false;
    m_preview_finished = false;
}

void SpriteAnimationStudioPanel::track_edit(
    EntityManager& em,
    EntityID entity,
    const AnimationComponent& before,
    const std::string& property,
    const char* description,
    bool force_commit
) {
    static_cast<void>(em);
    static_cast<void>(entity);
    static_cast<void>(property);
    static_cast<void>(description);
    if (force_commit) {
        commit_pending_history();
        commit_nudge_edit();
        record_draft_edit(before);
        m_pending_history_snapshot.reset();
        return;
    }

    if (ImGui::IsItemActivated() && !m_pending_history_snapshot) {
        commit_nudge_edit();
        m_pending_history_snapshot = before;
    }
    if (before != m_draft_animation && !m_pending_history_snapshot) {
        m_pending_history_snapshot = before;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && m_pending_history_snapshot) {
        record_draft_edit(*m_pending_history_snapshot);
        m_pending_history_snapshot.reset();
        return;
    }
    refresh_draft_dirty();
}

void SpriteAnimationStudioPanel::on_gui() {
    if (!is_open) {
        m_has_keyboard_focus = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(1040.0f, 680.0f), ImGuiCond_FirstUseEver);
    const bool window_visible = ImGui::Begin(
        "Animation Studio###SpriteAnimationStudio", &is_open
    );
    m_has_keyboard_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!window_visible) {
        ImGui::End();
        return;
    }

    if (!m_scene_manager) {
        EditorUi::empty_state(
            EditorIcon::Animation, "Animation Studio unavailable", "No SceneManager is active"
        );
        ImGui::End();
        return;
    }

    Scene* active_scene = m_scene_manager->get_active_scene();
    if (!active_scene) {
        EditorUi::empty_state(
            EditorIcon::Animation, "No active scene", "Open a scene to edit sprite animation"
        );
        ImGui::End();
        return;
    }

    EntityManager& em = active_scene->get_entity_manager();
    if (!m_draft_dirty && m_pending_target_entity != 0 &&
        em.is_alive(m_pending_target_entity) &&
        em.has_component<SpriteComponent>(m_pending_target_entity)) {
        m_target_entity = m_pending_target_entity;
        m_pending_target_entity = 0;
        clear_draft();
        m_preview_entity = 0;
    }
    if (m_selection && !m_selection->empty()) {
        const EntityID selected = m_selection->get_primary();
        if (em.is_alive(selected) && em.has_component<SpriteComponent>(selected)) {
            if (m_target_entity == selected) {
                m_pending_target_entity = 0;
            } else if (m_target_entity != 0 && m_draft_dirty) {
                m_pending_target_entity = selected;
            } else if (m_target_entity != selected) {
                m_target_entity = selected;
                m_pending_target_entity = 0;
                clear_draft();
                m_preview_entity = 0;
            }
        }
    }

    if (m_target_entity == 0 || !em.is_alive(m_target_entity)) {
        em.for_each<AnimationComponent, SpriteComponent>(
            [&](EntityID id, AnimationComponent&, SpriteComponent&) {
                if (m_target_entity == 0) m_target_entity = id;
            }
        );
    }

    if (m_target_entity == 0 || !em.is_alive(m_target_entity) ||
        !em.has_component<SpriteComponent>(m_target_entity)) {
        EditorUi::empty_state(
            EditorIcon::Animation,
            "Select a sprite",
            "Choose an entity with a Sprite Renderer in the Scene Hierarchy"
        );
        ImGui::End();
        return;
    }

    ensure_draft(em, m_target_entity);

    if (m_preview_entity != m_target_entity) {
        reset_preview(m_target_entity, m_draft_animation);
    }

    render_top_bar(em, m_target_entity);
    ImGui::Separator();

    const auto& metrics = EditorTheme::metrics();
    const float timeline_height = 112.0f * metrics.scale;
    const float workspace_height = std::max(
        280.0f * metrics.scale,
        ImGui::GetContentRegionAvail().y - timeline_height - metrics.compact_spacing
    );
    if (ImGui::BeginTable(
            "AnimationStudioWorkspace", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn(
            "Sidebar", ImGuiTableColumnFlags_WidthFixed, 300.0f * metrics.scale
        );
        ImGui::TableSetupColumn("Spritesheet", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, workspace_height);

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("AnimationSidebar", ImVec2(0.0f, workspace_height), false);
        render_preview_viewport(em, m_target_entity);
        render_animation_settings(em, m_target_entity);
        render_clip_tabs(em, m_target_entity);
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        render_spritesheet_canvas(em, m_target_entity, workspace_height);
        ImGui::EndTable();
    }

    ImGui::Separator();
    render_timeline(em, m_target_entity);
    ImGui::End();
}

void SpriteAnimationStudioPanel::render_top_bar(EntityManager& em, EntityID entity) {
    const auto& sprite = em.get_component<SpriteComponent>(entity);
    const std::string tag = em.has_component<TagComponent>(entity)
        ? em.get_component<TagComponent>(entity).tag
        : "Entity";
    const auto& metrics = EditorTheme::metrics();
    const std::optional<std::string> validation_error = validate_draft(sprite.texture);

    if (ImGui::BeginTable(
            "##animation_target", 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Actions", ImGuiTableColumnFlags_WidthFixed, 250.0f * metrics.scale
        );
        ImGui::TableSetupColumn(
            "Zoom", ImGuiTableColumnFlags_WidthFixed, 230.0f * metrics.scale
        );
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        EditorUi::badge("TARGET", EditorUi::BadgeTone::Accent);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tag.c_str());
        ImGui::SameLine();
        if (sprite.texture) {
            ImGui::TextDisabled(
                "ID %u  |  %d x %d px",
                entity, sprite.texture->get_width(), sprite.texture->get_height()
            );
        } else {
            ImGui::TextDisabled("ID %u  |  No texture", entity);
        }

        ImGui::TableSetColumnIndex(1);
        const char* draft_status = validation_error
            ? "INVALID"
            : (!m_saved_animation ? "NEW" : (m_draft_dirty ? "UNSAVED" : "SAVED"));
        const EditorUi::BadgeTone draft_tone = validation_error
            ? EditorUi::BadgeTone::Error
            : (!m_saved_animation || m_draft_dirty
                ? EditorUi::BadgeTone::Warning
                : EditorUi::BadgeTone::Success);
        EditorUi::badge(draft_status, draft_tone);
        ImGui::SameLine();
        if (EditorUi::action_button(
                "save_animation_draft", EditorIcon::Save, "Save Changes",
                EditorUi::ButtonTone::Primary,
                (!m_saved_animation || m_draft_dirty) && !validation_error &&
                    m_command_history != nullptr)) {
            save_draft(em, entity);
        }
        ImGui::SameLine();
        if (EditorUi::action_button(
                "discard_animation_draft", EditorIcon::History, "Discard",
                EditorUi::ButtonTone::Neutral, m_draft_dirty)) {
            discard_draft();
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Sheet");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##sheet_zoom", &m_sheet_zoom, 0.05f, 8.0f, "%.2fx")) {
            m_sheet_zoom_fit_pending = false;
        }
        ImGui::EndTable();
    }

    if (validation_error) {
        ImGui::TextColored(EditorTheme::palette().error, "%s", validation_error->c_str());
    }

    if (m_pending_target_entity != 0 && m_pending_target_entity != entity) {
        ImGui::TextColored(
            EditorTheme::palette().warning,
            "Save or discard this draft before switching to entity %u.",
            m_pending_target_entity
        );
    }
}

} // namespace Engine
