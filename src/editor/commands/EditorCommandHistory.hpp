#pragma once

#include "IEditorCommand.hpp"
#include <vector>
#include <memory>
#include <string>
#include <optional>
#include <cstdint>

namespace Engine {

struct HistoryEntry {
    std::unique_ptr<IEditorCommand> command;
    uint64_t before_revision{0};
    uint64_t after_revision{0};
};

class EditorCommandHistory {
public:
    explicit EditorCommandHistory(size_t max_history = 100)
        : m_max_history(max_history) {}

    // Executes a new command and pushes it to the undo stack (clears redo stack)
    bool execute_command(std::unique_ptr<IEditorCommand> command);

    // Pushes a command that was already executed interactively (e.g. Gizmo drag release)
    void push_executed_command(std::unique_ptr<IEditorCommand> command);

    // Collapses every authoring command added after revision_before into one already-applied
    // transaction without executing it again. Used by multi-pass authoring sessions.
    [[nodiscard]] bool squash_commands_after_revision(
        uint64_t revision_before,
        std::string description
    );

    // Reverses the top command
    void undo();

    // Re-applies the top undone command
    void redo();

    [[nodiscard]] bool can_undo() const { return !m_undo_stack.empty(); }
    [[nodiscard]] bool can_redo() const { return !m_redo_stack.empty(); }

    [[nodiscard]] std::string get_undo_description() const;
    [[nodiscard]] std::string get_redo_description() const;

    // Clears undo/redo stacks while preserving current dirty state and revisions
    void clear_history_preserving_dirty_state();

    // Backward-compatible alias for clearing history preserving dirty state
    void clear() { clear_history_preserving_dirty_state(); }

    // Resets history and revision counters to a clean baseline (used when creating or loading a scene)
    void reset_clean_baseline(uint64_t baseline = 0);

    // Marks the current revision as saved (clean point)
    void mark_saved();

    // A restored recovery snapshot must be saved or discarded explicitly.
    void mark_recovered();

    // Returns true if current revision differs from the saved revision
    [[nodiscard]] bool is_dirty() const {
        return !m_saved_revision.has_value() || m_current_revision != *m_saved_revision;
    }

    [[nodiscard]] uint64_t get_current_revision() const { return m_current_revision; }
    [[nodiscard]] std::optional<uint64_t> get_saved_revision() const { return m_saved_revision; }
    [[nodiscard]] size_t get_undo_count() const { return m_undo_stack.size(); }
    [[nodiscard]] size_t get_redo_count() const { return m_redo_stack.size(); }

private:
    size_t m_max_history{100};
    uint64_t m_revision_counter{0};
    uint64_t m_current_revision{0};
    std::optional<uint64_t> m_saved_revision{0};

    std::vector<HistoryEntry> m_undo_stack;
    std::vector<HistoryEntry> m_redo_stack;
};

} // namespace Engine
