#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/EditorCommandGroup.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

namespace {

using namespace Engine;

class SetIntegerCommand final : public IEditorCommand {
public:
    SetIntegerCommand(int& target, int before, int after, std::string description)
        : m_target(target), m_before(before), m_after(after), m_description(std::move(description)) {}

    bool execute() override { m_target = m_after; return true; }
    bool undo() override { m_target = m_before; return true; }
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    int& m_target;
    int m_before;
    int m_after;
    std::string m_description;
};

class FailingCommand final : public IEditorCommand {
public:
    bool execute() override { return false; }
    bool undo() override { return false; }
    [[nodiscard]] std::string get_description() const override { return "Failing Command"; }
};

class FailingUndoCommand final : public IEditorCommand {
public:
    bool execute() override { return true; }
    bool undo() override { return false; }
    [[nodiscard]] std::string get_description() const override { return "Failing Undo Command"; }
};

class FailingRedoCommand final : public IEditorCommand {
public:
    bool execute() override { return true; }
    bool undo() override { return true; }
    bool redo() override { return false; }
    [[nodiscard]] std::string get_description() const override { return "Failing Redo Command"; }
};

void test_execute_undo_redo_and_descriptions() {
    int value = 1;
    EditorCommandHistory history;
    history.execute_command(std::make_unique<SetIntegerCommand>(value, 1, 7, "Set Value"));

    assert(value == 7);
    assert(history.can_undo());
    assert(!history.can_redo());
    assert(history.get_undo_description() == "Set Value");

    history.undo();
    assert(value == 1);
    assert(history.get_redo_description() == "Set Value");

    history.redo();
    assert(value == 7);
    assert(history.get_undo_count() == 1);
}

void test_failed_command_is_not_added_to_history() {
    EditorCommandHistory history;
    history.execute_command(std::make_unique<FailingCommand>());

    assert(!history.can_undo());
    assert(history.get_undo_count() == 0);
    assert(!history.can_redo());
}

void test_failing_undo_retains_command_in_undo_stack() {
    EditorCommandHistory history;
    history.execute_command(std::make_unique<FailingUndoCommand>());
    assert(history.can_undo());
    assert(history.get_undo_count() == 1);

    // Undo fails
    history.undo();
    // Command must still be on undo stack and NOT moved to redo stack
    assert(history.can_undo());
    assert(history.get_undo_count() == 1);
    assert(!history.can_redo());
    assert(history.get_redo_count() == 0);
}

void test_failing_redo_retains_command_in_redo_stack() {
    EditorCommandHistory history;
    history.execute_command(std::make_unique<FailingRedoCommand>());
    assert(history.can_undo());

    // Undo succeeds, moves to redo stack
    history.undo();
    assert(!history.can_undo());
    assert(history.can_redo());
    assert(history.get_redo_count() == 1);

    // Redo fails
    history.redo();
    // Command must still be on redo stack and NOT lost
    assert(!history.can_undo());
    assert(history.can_redo());
    assert(history.get_redo_count() == 1);
}

void test_command_group_is_one_reversible_history_entry() {
    int value = 0;
    auto group = std::make_unique<EditorCommandGroup>("AI Session (2 actions)");
    group->add_command(std::make_unique<SetIntegerCommand>(value, 0, 1, "First"));
    group->add_command(std::make_unique<SetIntegerCommand>(value, 1, 2, "Second"));

    EditorCommandHistory history;
    assert(history.execute_command(std::move(group)));
    assert(value == 2);
    assert(history.get_undo_count() == 1);
    assert(history.get_undo_description() == "AI Session (2 actions)");

    history.undo();
    assert(value == 0);
    history.redo();
    assert(value == 2);
}

void test_command_group_rolls_back_failed_execution() {
    int value = 0;
    auto group = std::make_unique<EditorCommandGroup>("Failing AI Session");
    group->add_command(std::make_unique<SetIntegerCommand>(value, 0, 1, "First"));
    group->add_command(std::make_unique<FailingCommand>());

    EditorCommandHistory history;
    assert(!history.execute_command(std::move(group)));
    assert(value == 0);
    assert(!history.can_undo());
}

void test_command_group_restores_applied_state_when_undo_fails() {
    int value = 0;
    auto group = std::make_unique<EditorCommandGroup>("Failing Group Undo");
    group->add_command(std::make_unique<FailingUndoCommand>());
    group->add_command(std::make_unique<SetIntegerCommand>(value, 0, 1, "Second"));

    EditorCommandHistory history;
    assert(history.execute_command(std::move(group)));
    assert(value == 1);
    history.undo();
    assert(value == 1);
    assert(history.get_undo_description() == "Failing Group Undo");
    assert(!history.can_redo());
}

void test_command_group_rolls_back_partial_redo() {
    int value = 0;
    auto group = std::make_unique<EditorCommandGroup>("Failing Group Redo");
    group->add_command(std::make_unique<SetIntegerCommand>(value, 0, 1, "First"));
    group->add_command(std::make_unique<FailingRedoCommand>());

    EditorCommandHistory history;
    assert(history.execute_command(std::move(group)));
    history.undo();
    assert(value == 0);
    history.redo();
    assert(value == 0);
    assert(!history.can_undo());
    assert(history.get_redo_description() == "Failing Group Redo");
}

void test_new_command_invalidates_redo() {
    int value = 0;
    EditorCommandHistory history;
    history.execute_command(std::make_unique<SetIntegerCommand>(value, 0, 1, "First"));
    history.undo();
    assert(history.can_redo());

    history.execute_command(std::make_unique<SetIntegerCommand>(value, 0, 2, "Second"));
    assert(value == 2);
    assert(!history.can_redo());
    assert(history.get_undo_description() == "Second");
}

void test_bounded_history_discards_oldest_command() {
    int value = 0;
    EditorCommandHistory history(2);
    history.execute_command(std::make_unique<SetIntegerCommand>(value, 0, 1, "One"));
    history.execute_command(std::make_unique<SetIntegerCommand>(value, 1, 2, "Two"));
    history.execute_command(std::make_unique<SetIntegerCommand>(value, 2, 3, "Three"));

    assert(history.get_undo_count() == 2);
    history.undo();
    history.undo();
    assert(value == 1);
    history.undo();
    assert(value == 1);
}

void test_push_executed_and_clear() {
    int value = 9;
    EditorCommandHistory history;
    history.push_executed_command(std::make_unique<SetIntegerCommand>(value, 4, 9, "Drag Value"));
    assert(value == 9);

    history.undo();
    assert(value == 4);
    history.redo();
    assert(value == 9);

    history.clear();
    assert(!history.can_undo());
    assert(!history.can_redo());
    assert(history.get_undo_description().empty());
    assert(history.get_redo_description().empty());
}

void test_null_commands_are_ignored() {
    EditorCommandHistory history;
    history.execute_command(nullptr);
    history.push_executed_command(nullptr);
    assert(history.get_undo_count() == 0);
    assert(history.get_redo_count() == 0);
}

void test_dirty_state_execute_and_mark_saved() {
    int val = 0;
    EditorCommandHistory history;
    assert(!history.is_dirty());
    assert(history.get_current_revision() == 0);

    history.execute_command(std::make_unique<SetIntegerCommand>(val, 0, 10, "Set 10"));
    assert(history.is_dirty());
    assert(history.get_current_revision() == 1);

    history.mark_saved();
    assert(!history.is_dirty());
    assert(history.get_saved_revision() == 1);
}

void test_dirty_state_undo_and_redo_revisions() {
    int val = 0;
    EditorCommandHistory history;
    // Initial state: rev 0, saved 0 (clean)
    assert(!history.is_dirty());

    // Cmd 1 -> rev 1
    history.execute_command(std::make_unique<SetIntegerCommand>(val, 0, 1, "Set 1"));
    assert(history.is_dirty());

    // Save at rev 1
    history.mark_saved();
    assert(!history.is_dirty());

    // Cmd 2 -> rev 2
    history.execute_command(std::make_unique<SetIntegerCommand>(val, 1, 2, "Set 2"));
    assert(history.is_dirty());

    // Undo Cmd 2 -> back to rev 1 (saved revision point!)
    history.undo();
    assert(history.get_current_revision() == 1);
    assert(!history.is_dirty());

    // Undo Cmd 1 -> back to rev 0 (dirty, because saved was rev 1!)
    history.undo();
    assert(history.get_current_revision() == 0);
    assert(history.is_dirty());

    // Redo Cmd 1 -> back to rev 1 (clean again!)
    history.redo();
    assert(history.get_current_revision() == 1);
    assert(!history.is_dirty());

    // Redo Cmd 2 -> forward to rev 2 (dirty!)
    history.redo();
    assert(history.get_current_revision() == 2);
    assert(history.is_dirty());
}

void test_dirty_state_branching_history() {
    int val = 0;
    EditorCommandHistory history;
    history.execute_command(std::make_unique<SetIntegerCommand>(val, 0, 1, "Cmd 1"));
    history.execute_command(std::make_unique<SetIntegerCommand>(val, 1, 2, "Cmd 2"));
    history.mark_saved(); // Saved at rev 2
    assert(!history.is_dirty());

    // Undo Cmd 2 (rev 1), then execute Cmd 3 (rev 3) -> branching history
    history.undo();
    assert(history.is_dirty());
    history.execute_command(std::make_unique<SetIntegerCommand>(val, 1, 3, "Cmd 3"));
    assert(history.get_current_revision() == 3);
    assert(history.is_dirty()); // rev 3 != saved rev 2

    // Undo Cmd 3 -> rev 1 (still dirty, since saved was 2 and 2 is no longer reachable)
    history.undo();
    assert(history.get_current_revision() == 1);
    assert(history.is_dirty());
}

void test_clear_preserving_dirty_state_vs_reset_baseline() {
    int val = 0;
    EditorCommandHistory history;
    history.execute_command(std::make_unique<SetIntegerCommand>(val, 0, 5, "Cmd 5"));
    assert(history.is_dirty());

    // clear_history_preserving_dirty_state clears undo stack but retains dirty flag
    history.clear_history_preserving_dirty_state();
    assert(history.get_undo_count() == 0);
    assert(history.is_dirty());

    // reset_clean_baseline sets clean state for a freshly loaded/created scene
    history.reset_clean_baseline(0);
    assert(history.get_undo_count() == 0);
    assert(!history.is_dirty());
}

void test_editor_session_commands_do_not_modify_revisions() {
    class SessionCommand final : public IEditorCommand {
    public:
        SessionCommand(bool& target) : m_target(target) {}
        bool execute() override { m_target = true; return true; }
        bool undo() override { m_target = false; return true; }
        [[nodiscard]] std::string get_description() const override { return "Session Command"; }
        [[nodiscard]] EditorCommandScope get_scope() const override { return EditorCommandScope::EditorSession; }
    private:
        bool& m_target;
    };

    bool flag = false;
    EditorCommandHistory history;
    history.reset_clean_baseline(0);
    assert(!history.is_dirty());
    assert(history.get_current_revision() == 0);

    // Execute EditorSession command
    history.execute_command(std::make_unique<SessionCommand>(flag));
    assert(flag == true);
    assert(history.get_undo_count() == 1);
    assert(history.get_current_revision() == 0);
    assert(!history.is_dirty()); // Does NOT dirty the scene

    // Undo EditorSession command
    assert(history.can_undo());
    history.undo();
    assert(flag == false);
    assert(history.get_current_revision() == 0);
    assert(!history.is_dirty());

    // Redo EditorSession command
    assert(history.can_redo());
    history.redo();
    assert(flag == true);
    assert(history.get_current_revision() == 0);
    assert(!history.is_dirty());

    // Now mix with an Authoring command
    int val = 0;
    history.execute_command(std::make_unique<SetIntegerCommand>(val, 0, 10, "Authoring Cmd"));
    assert(history.get_current_revision() == 1);
    assert(history.is_dirty());

    // Execute another Session command
    history.execute_command(std::make_unique<SessionCommand>(flag));
    assert(history.get_current_revision() == 1); // Stays at 1
    assert(history.is_dirty());

    // Undo session cmd
    history.undo();
    assert(history.get_current_revision() == 1); // Stays at 1
    assert(history.is_dirty());

    // Undo authoring cmd
    history.undo();
    assert(history.get_current_revision() == 0);
    assert(!history.is_dirty()); // Back to clean baseline!
}

} // namespace

int main() {
    test_execute_undo_redo_and_descriptions();
    test_failed_command_is_not_added_to_history();
    test_failing_undo_retains_command_in_undo_stack();
    test_failing_redo_retains_command_in_redo_stack();
    test_command_group_is_one_reversible_history_entry();
    test_command_group_rolls_back_failed_execution();
    test_command_group_restores_applied_state_when_undo_fails();
    test_command_group_rolls_back_partial_redo();
    test_new_command_invalidates_redo();
    test_bounded_history_discards_oldest_command();
    test_push_executed_and_clear();
    test_null_commands_are_ignored();
    test_dirty_state_execute_and_mark_saved();
    test_dirty_state_undo_and_redo_revisions();
    test_dirty_state_branching_history();
    test_clear_preserving_dirty_state_vs_reset_baseline();
    test_editor_session_commands_do_not_modify_revisions();
    std::cout << "EditorCommandHistoryTests passed\n";
    return 0;
}
