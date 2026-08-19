#include "editor/commands/EditorCommandHistory.hpp"
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

} // namespace

int main() {
    test_execute_undo_redo_and_descriptions();
    test_failed_command_is_not_added_to_history();
    test_failing_undo_retains_command_in_undo_stack();
    test_failing_redo_retains_command_in_redo_stack();
    test_new_command_invalidates_redo();
    test_bounded_history_discards_oldest_command();
    test_push_executed_and_clear();
    test_null_commands_are_ignored();
    std::cout << "EditorCommandHistoryTests passed\n";
    return 0;
}
