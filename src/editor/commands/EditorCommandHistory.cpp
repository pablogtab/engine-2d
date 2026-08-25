#include "EditorCommandHistory.hpp"
#include "../../core/Log.hpp"

#include <algorithm>
#include <utility>

namespace {

class AppliedCommandSequence final : public Engine::IEditorCommand {
public:
    AppliedCommandSequence(
        std::string description,
        std::vector<std::unique_ptr<Engine::IEditorCommand>> commands
    )
        : m_commands(std::move(commands)),
          m_description(std::move(description)) {}

    bool execute() override { return false; }

    bool undo() override {
        if (!m_applied) return false;
        for (std::size_t index = m_commands.size(); index > 0; --index) {
            const std::size_t command_index = index - 1;
            if (m_commands[command_index]->undo()) continue;
            for (std::size_t restore = command_index + 1; restore < m_commands.size(); ++restore) {
                static_cast<void>(m_commands[restore]->redo());
            }
            return false;
        }
        m_applied = false;
        return true;
    }

    bool redo() override {
        if (m_applied) return false;
        std::size_t applied = 0;
        for (; applied < m_commands.size(); ++applied) {
            if (m_commands[applied]->redo()) continue;
            while (applied > 0) {
                --applied;
                static_cast<void>(m_commands[applied]->undo());
            }
            return false;
        }
        m_applied = true;
        return true;
    }

    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    std::vector<std::unique_ptr<Engine::IEditorCommand>> m_commands;
    std::string m_description;
    bool m_applied{true};
};

} // namespace

namespace Engine {

bool EditorCommandHistory::execute_command(std::unique_ptr<IEditorCommand> command) {
    if (!command) return false;

    std::string desc = command->get_description();
    if (!command->execute()) {
        ENGINE_LOG_WARN("[COMMAND] Execution failed, not added to history: '{}'", desc);
        return false;
    }

    push_executed_command(std::move(command));
    ENGINE_LOG_INFO("[COMMAND] Executed: '{}'", desc);
    return true;
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

bool EditorCommandHistory::squash_commands_after_revision(
    uint64_t revision_before,
    std::string description
) {
    const auto first = std::find_if(
        m_undo_stack.begin(),
        m_undo_stack.end(),
        [revision_before](const HistoryEntry& entry) {
            return entry.before_revision == revision_before &&
                entry.after_revision > revision_before;
        }
    );
    if (first == m_undo_stack.end()) return false;
    const std::size_t first_index = static_cast<std::size_t>(
        std::distance(m_undo_stack.begin(), first)
    );
    for (std::size_t index = first_index; index < m_undo_stack.size(); ++index) {
        if (!m_undo_stack[index].command ||
            m_undo_stack[index].after_revision <= m_undo_stack[index].before_revision) {
            return false;
        }
    }

    const uint64_t before_revision = first->before_revision;
    const uint64_t after_revision = m_undo_stack.back().after_revision;
    std::vector<std::unique_ptr<IEditorCommand>> commands;
    commands.reserve(m_undo_stack.size() - first_index);
    for (size_t index = first_index; index < m_undo_stack.size(); ++index) {
        commands.push_back(std::move(m_undo_stack[index].command));
    }
    auto group = std::make_unique<AppliedCommandSequence>(
        std::move(description), std::move(commands)
    );

    m_undo_stack.erase(m_undo_stack.begin() + static_cast<std::ptrdiff_t>(first_index),
                       m_undo_stack.end());
    m_undo_stack.push_back(HistoryEntry{
        .command = std::move(group),
        .before_revision = before_revision,
        .after_revision = after_revision
    });
    return true;
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

void EditorCommandHistory::mark_recovered() {
    m_saved_revision.reset();
    ENGINE_LOG_INFO("[COMMAND] History marked dirty after recovery");
}

} // namespace Engine
