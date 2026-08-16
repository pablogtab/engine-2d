#include "EditorCommandHistory.hpp"
#include "../../core/Log.hpp"

namespace Engine {

void EditorCommandHistory::execute_command(std::unique_ptr<IEditorCommand> command) {
    if (!command) return;

    std::string desc = command->get_description();
    command->execute();

    push_executed_command(std::move(command));
    ENGINE_LOG_INFO("[COMMAND] Executed: '{}'", desc);
}

void EditorCommandHistory::push_executed_command(std::unique_ptr<IEditorCommand> command) {
    if (!command) return;

    // Clear redo stack on any new action
    m_redo_stack.clear();

    m_undo_stack.push_back(std::move(command));

    // Cap history size
    if (m_undo_stack.size() > m_max_history) {
        m_undo_stack.erase(m_undo_stack.begin());
    }
}

void EditorCommandHistory::undo() {
    if (!can_undo()) return;

    auto cmd = std::move(m_undo_stack.back());
    m_undo_stack.pop_back();

    std::string desc = cmd->get_description();
    cmd->undo();

    m_redo_stack.push_back(std::move(cmd));
    ENGINE_LOG_INFO("[COMMAND] Undone: '{}'", desc);
}

void EditorCommandHistory::redo() {
    if (!can_redo()) return;

    auto cmd = std::move(m_redo_stack.back());
    m_redo_stack.pop_back();

    std::string desc = cmd->get_description();
    cmd->redo();

    m_undo_stack.push_back(std::move(cmd));
    ENGINE_LOG_INFO("[COMMAND] Redone: '{}'", desc);
}

std::string EditorCommandHistory::get_undo_description() const {
    if (m_undo_stack.empty()) return "";
    return m_undo_stack.back()->get_description();
}

std::string EditorCommandHistory::get_redo_description() const {
    if (m_redo_stack.empty()) return "";
    return m_redo_stack.back()->get_description();
}

void EditorCommandHistory::clear() {
    m_undo_stack.clear();
    m_redo_stack.clear();
}

} // namespace Engine
