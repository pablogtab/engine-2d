#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

class EditorCommandHistory;
class EditorEntityStateStore;
class EditorSelection;
class Scene;

struct EditorSceneSession {
    std::shared_ptr<EditorCommandHistory> command_history;
    std::shared_ptr<EditorEntityStateStore> entity_state_store;
    std::shared_ptr<EditorSelection> selection;
    std::string file_path;
    std::string initial_snapshot_json;
    std::string autosave_identity;
    bool recovery_source_conflicted{false};
    bool external_source_conflicted{false};
    bool source_write_time_initialized{false};
    std::filesystem::file_time_type observed_source_write_time{};
    std::uint64_t persisted_entity_state_revision{0};
};

class EditorSessionController {
public:
    using SessionFactory = std::function<EditorSceneSession()>;
    using SessionMap = std::unordered_map<const Scene*, EditorSceneSession>;

    EditorSceneSession& ensure(const Scene* scene, SessionFactory factory);
    [[nodiscard]] EditorSceneSession* find(const Scene* scene);
    [[nodiscard]] const EditorSceneSession* find(const Scene* scene) const;
    void erase(const Scene* scene);
    void prune(const std::function<bool(const Scene*)>& keep);

    [[nodiscard]] SessionMap& sessions() { return m_sessions; }
    [[nodiscard]] const SessionMap& sessions() const { return m_sessions; }

    [[nodiscard]] bool is_tab_open(const Scene* scene) const;
    void open_tab(const Scene* scene);
    void close_tab(const Scene* scene);
    void clear_tabs() { m_open_tabs.clear(); }
    [[nodiscard]] bool tabs_empty() const { return m_open_tabs.empty(); }
    [[nodiscard]] std::size_t tab_count() const { return m_open_tabs.size(); }
    [[nodiscard]] const std::vector<const Scene*>& open_tabs() const { return m_open_tabs; }

private:
    SessionMap m_sessions;
    std::vector<const Scene*> m_open_tabs;
};

} // namespace Engine
