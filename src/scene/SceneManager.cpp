#include "SceneManager.hpp"
#include "SceneSerializer.hpp"
#include "../core/Log.hpp"
#include "../core/Time.hpp"
#include "../ecs/TransformSystem.hpp"
#include <algorithm>
#include <fstream>

namespace Engine {

namespace {
    const char* SCENE_STATE_PRIMARY = "build/.scene_state";
    const char* SCENE_STATE_FALLBACK = ".scene_state";
}

void SceneManager::save_state() {
    if (m_active_index < 0 || static_cast<size_t>(m_active_index) >= m_scenes.size()) return;

    std::ofstream out(SCENE_STATE_PRIMARY);
    if (!out.is_open()) {
        out.open(SCENE_STATE_FALLBACK);
    }
    if (out.is_open()) {
        out << m_scenes[m_active_index]->get_name() << "\n";
    }
}

void SceneManager::restore_state() {
    std::ifstream in(SCENE_STATE_PRIMARY);
    if (!in.is_open()) {
        in.open(SCENE_STATE_FALLBACK);
    }
    if (in.is_open()) {
        std::string name;
        if (std::getline(in, name) && !name.empty()) {
            set_active_scene(name);
        }
    }
}

void SceneManager::add_scene(std::shared_ptr<Scene> scene) {
    if (!scene) return;
    
    // Check if scene with same name already exists: replace it instead of duplicating!
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i] && m_scenes[i]->get_name() == scene->get_name()) {
            if (m_active_index == static_cast<int>(i)) {
                m_scenes[i]->on_detach();
            }
            m_scenes[i] = scene;
            if (m_active_index == static_cast<int>(i)) {
                m_scenes[i]->set_is_editing(is_editing());
                m_scenes[i]->on_attach();
                apply_active_view();
            }
            return;
        }
    }

    m_scenes.push_back(scene);
    
    // Automatically activate first registered scene
    if (m_active_index < 0) {
        set_active_scene(0);
    }
}

void SceneManager::set_active_scene(size_t index) {
    if (index >= m_scenes.size()) return;
    if (m_active_index == static_cast<int>(index)) return; // Prevent redundant double attach!

    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_detach();
    }

    m_active_index = static_cast<int>(index);
    m_scenes[m_active_index]->set_is_editing(is_editing());
    m_scenes[m_active_index]->on_attach();
    apply_active_view();

    save_state();

    if (m_on_authoring_state_invalidated) m_on_authoring_state_invalidated();

    ENGINE_LOG_INFO("Switched to Scene [{}/{}]: '{}'", m_active_index + 1, m_scenes.size(), m_scenes[m_active_index]->get_name());
}

void SceneManager::set_active_scene(const std::string& name) {
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i]->get_name() == name) {
            set_active_scene(i);
            return;
        }
    }
}

void SceneManager::next_scene() {
    if (m_scenes.empty()) return;
    size_t next_idx = (m_active_index + 1) % m_scenes.size();
    set_active_scene(next_idx);
}

void SceneManager::previous_scene() {
    if (m_scenes.empty()) return;
    size_t prev_idx = (m_active_index - 1 + m_scenes.size()) % m_scenes.size();
    set_active_scene(prev_idx);
}

void SceneManager::start_play() {
    if (m_play_state == ScenePlayState::Pause) {
        resume_play();
        return;
    }

    Scene* active_scene = get_active_scene();
    if (!active_scene) return;

    // 1. Take in-memory snapshot of authoring scene state
    SceneSerializer serializer(active_scene, m_renderer);
    m_snapshot_json = serializer.serialize_to_string();

    // 2. Attach runtime physics simulation
    active_scene->on_detach();
    active_scene->set_is_editing(false);
    active_scene->on_attach();
    const auto& game = m_game_view;
    active_scene->on_viewport_resize(RenderViewKind::Game, game.width, game.height, game.dpi_scale);

    m_play_state = ScenePlayState::Play;
    Time::set_time_scale(1.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Started Play simulation (Memory snapshot taken, switched to Gameplay Camera).");
}

void SceneManager::pause_play() {
    if (m_play_state != ScenePlayState::Play) return;

    m_play_state = ScenePlayState::Pause;
    Time::set_time_scale(0.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Simulation Paused.");
}

void SceneManager::resume_play() {
    if (m_play_state != ScenePlayState::Pause) return;

    m_play_state = ScenePlayState::Play;
    Time::set_time_scale(1.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Simulation Resumed.");
}

void SceneManager::stop_play() {
    if (m_play_state == ScenePlayState::Edit) return;

    Scene* active_scene = get_active_scene();
    if (active_scene) {
        active_scene->on_detach();
        active_scene->set_is_editing(true);

        // Restore pristine authoring state from memory snapshot
        if (!m_snapshot_json.empty()) {
            SceneSerializer serializer(active_scene, m_renderer);
            serializer.deserialize_from_string(m_snapshot_json);
            if (m_on_authoring_state_invalidated) m_on_authoring_state_invalidated();
        }

        TransformSystem::update(active_scene->get_entity_manager());
        const auto& editor = m_editor_view;
        active_scene->on_viewport_resize(RenderViewKind::Editor, editor.width, editor.height, editor.dpi_scale);
    }

    m_play_state = ScenePlayState::Edit;
    Time::set_time_scale(0.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Stopped Simulation: Authoring snapshot restored, switched back to Editor Camera.");
}

void SceneManager::update(float delta_time) {
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_update(delta_time);
    }
}

void SceneManager::render(Renderer2D& renderer) {
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_render(renderer);
    }
}

void SceneManager::resize_view(RenderViewKind view, int width, int height, float dpi_scale) {
    ViewportMetrics& metrics = view == RenderViewKind::Editor ? m_editor_view : m_game_view;
    metrics.width = std::max(width, 1);
    metrics.height = std::max(height, 1);
    metrics.dpi_scale = dpi_scale;

    const RenderViewKind active_view = is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
    if (view == active_view && m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_viewport_resize(view, metrics.width, metrics.height, metrics.dpi_scale);
    }
}

void SceneManager::apply_active_view() {
    if (m_active_index < 0 || static_cast<size_t>(m_active_index) >= m_scenes.size()) return;
    const RenderViewKind view = is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
    const ViewportMetrics& metrics = view == RenderViewKind::Editor ? m_editor_view : m_game_view;
    m_scenes[m_active_index]->on_viewport_resize(view, metrics.width, metrics.height, metrics.dpi_scale);
}

Scene* SceneManager::get_active_scene() const {
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        return m_scenes[m_active_index].get();
    }
    return nullptr;
}

} // namespace Engine
