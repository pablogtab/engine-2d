#include "EditorCommandGroup.hpp"

namespace Engine {

EditorCommandGroup::EditorCommandGroup(std::string description)
    : m_description(std::move(description)) {}

void EditorCommandGroup::add_command(std::unique_ptr<IEditorCommand> command) {
    if (command && m_state == State::Fresh) m_commands.push_back(std::move(command));
}

bool EditorCommandGroup::execute() {
    m_error.clear();
    if (m_state != State::Fresh) {
        m_error = "Command group is not fresh";
        return false;
    }
    if (m_commands.empty()) {
        m_error = "Command group is empty";
        return false;
    }

    std::size_t applied = 0;
    for (; applied < m_commands.size(); ++applied) {
        if (m_commands[applied]->execute()) continue;
        m_error = "Action " + std::to_string(applied + 1) + "/" +
            std::to_string(m_commands.size()) + " ('" +
            m_commands[applied]->get_description() + "') failed";
        const std::string detail = m_commands[applied]->get_error();
        if (!detail.empty()) m_error += ": " + detail;
        while (applied > 0) {
            --applied;
            static_cast<void>(m_commands[applied]->undo());
        }
        return false;
    }
    m_state = State::Applied;
    return true;
}

bool EditorCommandGroup::undo() {
    if (m_state != State::Applied || m_commands.empty()) return false;

    for (std::size_t index = m_commands.size(); index > 0; --index) {
        const std::size_t command_index = index - 1;
        if (m_commands[command_index]->undo()) continue;

        // Restore the applied state if one child refuses to undo. History must
        // never observe a half-undone transaction.
        for (std::size_t restore = command_index + 1; restore < m_commands.size(); ++restore) {
            static_cast<void>(m_commands[restore]->redo());
        }
        return false;
    }
    m_state = State::Undone;
    return true;
}

bool EditorCommandGroup::redo() {
    m_error.clear();
    if (m_state != State::Undone || m_commands.empty()) return false;

    std::size_t applied = 0;
    for (; applied < m_commands.size(); ++applied) {
        if (m_commands[applied]->redo()) continue;
        m_error = "Action " + std::to_string(applied + 1) + "/" +
            std::to_string(m_commands.size()) + " ('" +
            m_commands[applied]->get_description() + "') failed during redo";
        const std::string detail = m_commands[applied]->get_error();
        if (!detail.empty()) m_error += ": " + detail;
        while (applied > 0) {
            --applied;
            static_cast<void>(m_commands[applied]->undo());
        }
        return false;
    }
    m_state = State::Applied;
    return true;
}

} // namespace Engine
