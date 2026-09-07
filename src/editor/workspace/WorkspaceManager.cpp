#include "WorkspaceManager.hpp"

#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace Engine {

namespace {

constexpr int WORKSPACE_STATE_VERSION = 1;

std::size_t workspace_index(EditorWorkspaceId id) {
    return static_cast<std::size_t>(id);
}

} // namespace

WorkspaceManager::WorkspaceManager()
    : m_descriptors{{
        {EditorWorkspaceId::TwoD, "2d", "2D", {
            EditorPanelId::Hierarchy, EditorPanelId::Viewport,
            EditorPanelId::Inspector, EditorPanelId::Assets,
            EditorPanelId::Console, EditorPanelId::TextureComposer
        }},
        {EditorWorkspaceId::Animation, "animation", "Animation", {
            EditorPanelId::Assets, EditorPanelId::Animation
        }},
        {EditorWorkspaceId::Tilemap, "tilemap", "Level", {
            EditorPanelId::Hierarchy, EditorPanelId::Viewport,
            EditorPanelId::Inspector, EditorPanelId::Tilemap,
            EditorPanelId::LevelComposer, EditorPanelId::SurfaceGenerator,
            EditorPanelId::TextureComposer,
            EditorPanelId::Assets
        }},
        {EditorWorkspaceId::Vfx, "vfx", "VFX", {
            EditorPanelId::Viewport, EditorPanelId::Inspector,
            EditorPanelId::Particles, EditorPanelId::TrailPreview,
            EditorPanelId::VfxPreview,
            EditorPanelId::PostProcess,
            EditorPanelId::Assets
        }},
        {EditorWorkspaceId::Debug, "debug", "Debug", {
            EditorPanelId::Viewport, EditorPanelId::Console,
            EditorPanelId::Metrics, EditorPanelId::Diagnostics
        }}
    }} {}

const EditorWorkspaceDescriptor& WorkspaceManager::active_descriptor() const {
    return m_descriptors[workspace_index(m_active)];
}

bool WorkspaceManager::set_active(EditorWorkspaceId id) {
    if (workspace_index(id) >= m_descriptors.size() || id == m_active) return false;
    m_active = id;
    return true;
}

bool WorkspaceManager::contains(EditorPanelId panel) const {
    const auto& panels = active_descriptor().panels;
    return std::find(panels.begin(), panels.end(), panel) != panels.end();
}

bool WorkspaceManager::load(const std::string& path) {
    const std::string text = FileSystem::read_text(path);
    if (text.empty()) return true;
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || root.value("version", 0) != WORKSPACE_STATE_VERSION) {
        ENGINE_LOG_WARN("[EDITOR UI] Ignoring invalid workspace state '{}'.", path);
        return false;
    }
    const std::string key = root.value("active", std::string{"2d"});
    const auto found = std::find_if(m_descriptors.begin(), m_descriptors.end(),
        [&key](const EditorWorkspaceDescriptor& descriptor) {
            return key == descriptor.key;
        });
    if (found != m_descriptors.end()) m_active = found->id;
    return true;
}

bool WorkspaceManager::save(const std::string& path) const {
    const nlohmann::json root{
        {"version", WORKSPACE_STATE_VERSION},
        {"active", active_descriptor().key}
    };
    if (FileSystem::write_text_atomically(path, root.dump(2) + '\n')) return true;
    ENGINE_LOG_ERROR("[EDITOR UI] Failed saving workspace state '{}'.", path);
    return false;
}

} // namespace Engine
