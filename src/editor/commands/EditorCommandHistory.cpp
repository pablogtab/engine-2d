#include "EditorCommandHistory.hpp"
#include "../../core/Log.hpp"

namespace Engine {

void EditorCommandHistory::execute_command(std::unique_ptr<IEditorCommand> command) {
    if (!command) return;

    std::string desc = command->get_description();
    if (!command->execute()) {
        ENGINE_LOG_WARN("[COMMAND] Execution failed, not added to history: '{}'", desc);
        return;
    }

    push_executed_command(std::move(command));
    ENGINE_LOG_INFO("[COMMAND] Executed: '{}'", desc);
}

void EditorCommandHistory::push_executed_command(std::unique_ptr<IEditorCommand> command) {
    if (!command) return;

    // Clear redo stack on any new action
    m_redo_stack.clear();

    const bool is_authoring = (command->get_scope() == EditorCommandScope::Authoring);
    const uint64_t before_rev = m_current_revision;
    const uint64_t after_rev = is_authoring ? ++m_revision_counter : m_current_revision;
    m_current_revision = after_rev;

    m_undo_stack.push_back(HistoryEntry{
        .command = std::move(command),
        .before_revision = before_rev,
        .after_revision = after_rev
    });

    // Cap history size
    if (m_undo_stack.size() > m_max_history) {
        m_undo_stack.erase(m_undo_stack.begin());
    }
}

void EditorCommandHistory::undo() {
    if (!can_undo()) return;

    auto entry = std::move(m_undo_stack.back());
    m_undo_stack.pop_back();

    std::string desc = entry.command ? entry.command->get_description() : "";
    if (!entry.command || !entry.command->undo()) {
        ENGINE_LOG_WARN("[COMMAND] Undo failed for: '{}'", desc);
        m_undo_stack.push_back(std::move(entry));
        return;
    }

    m_current_revision = entry.before_revision;
    m_redo_stack.push_back(std::move(entry));
    ENGINE_LOG_INFO("[COMMAND] Undone: '{}' (rev -> {})", desc, m_current_revision);
}

void EditorCommandHistory::redo() {
    if (!can_redo()) return;

    auto entry = std::move(m_redo_stack.back());
    m_redo_stack.pop_back();

    std::string desc = entry.command ? entry.command->get_description() : "";
    if (!entry.command || !entry.command->redo()) {
        ENGINE_LOG_WARN("[COMMAND] Redo failed for: '{}'", desc);
        m_redo_stack.push_back(std::move(entry));
        return;
    }

    m_current_revision = entry.after_revision;
    m_undo_stack.push_back(std::move(entry));
    ENGINE_LOG_INFO("[COMMAND] Redone: '{}' (rev -> {})", desc, m_current_revision);
}

std::string EditorCommandHistory::get_undo_description() const {
    if (m_undo_stack.empty() || !m_undo_stack.back().command) return "";
    return m_undo_stack.back().command->get_description();
}

std::string EditorCommandHistory::get_redo_description() const {
    if (m_redo_stack.empty() || !m_redo_stack.back().command) return "";
    return m_redo_stack.back().command->get_description();
}

void EditorCommandHistory::clear_history_preserving_dirty_state() {
    m_undo_stack.clear();
    m_redo_stack.clear();
}

void EditorCommandHistory::reset_clean_baseline(uint64_t baseline) {
    m_undo_stack.clear();
    m_redo_stack.clear();
    m_revision_counter = baseline;
    m_current_revision = baseline;
    m_saved_revision = baseline;
}

void EditorCommandHistory::mark_saved() {
    m_saved_revision = m_current_revision;
    ENGINE_LOG_INFO("[COMMAND] History marked saved at revision {}", m_current_revision);
}

} // namespace Engine
