#pragma once

#include <string>

namespace Engine {

class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;

    // Executes the command (applied first time). Returns true on success.
    virtual bool execute() = 0;

    // Reverses the command (Undo). Returns true on success.
    virtual bool undo() = 0;

    // Re-applies the command (Redo) - defaults to execute(). Returns true on success.
    virtual bool redo() { return execute(); }

    // Human-readable description for UI tooltips (e.g. "Move Player_Knight")
    [[nodiscard]] virtual std::string get_description() const = 0;
};

} // namespace Engine
