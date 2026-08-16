#pragma once

#include <string>

namespace Engine {

class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;

    // Executes the command (applied first time)
    virtual void execute() = 0;

    // Reverses the command (Undo)
    virtual void undo() = 0;

    // Re-applies the command (Redo) - defaults to execute()
    virtual void redo() { execute(); }

    // Human-readable description for UI tooltips (e.g. "Move Player_Knight")
    [[nodiscard]] virtual std::string get_description() const = 0;
};

} // namespace Engine
