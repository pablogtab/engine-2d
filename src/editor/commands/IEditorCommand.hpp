#pragma once

#include <string>

namespace Engine {

enum class EditorCommandScope {
    Authoring,
    EditorSession
};

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

    // Command scope determines whether this command mutates serialized scene content (Authoring)
    // or ephemeral editor session metadata (EditorSession) like Lock/Visibility.
    [[nodiscard]] virtual EditorCommandScope get_scope() const { return EditorCommandScope::Authoring; }
};

} // namespace Engine
