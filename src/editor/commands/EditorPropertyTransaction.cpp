#include "EditorPropertyTransaction.hpp"

namespace Engine {

EditorValueCommand::EditorValueCommand(
    Operation apply_before,
    Operation apply_after,
    std::string description
) : m_apply_before(std::move(apply_before)),
    m_apply_after(std::move(apply_after)),
    m_description(std::move(description)) {}

bool EditorValueCommand::execute() {
    if (m_apply_after) m_apply_after();
    return true;
}

bool EditorValueCommand::undo() {
    if (m_apply_before) m_apply_before();
    return true;
}

EditorPropertyTransaction::Key EditorPropertyTransaction::make_key(
    std::uint32_t owner,
    std::string_view property
) {
    // FNV-1a keeps keys deterministic without tying the transaction layer to ImGui.
    std::uint64_t hash = 14695981039346656037ull;
    for (const char character : property) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ull;
    }
    hash ^= owner;
    hash *= 1099511628211ull;
    return hash;
}

bool EditorPropertyTransaction::commit(Key key, EditorCommandHistory& history) {
    auto found = m_pending.find(key);
    if (found == m_pending.end()) return false;

    std::unique_ptr<IEditorCommand> command = found->second.create_command();
    m_pending.erase(found);
    if (!command) return false;

    history.push_executed_command(std::move(command));
    return true;
}

bool EditorPropertyTransaction::cancel(Key key) {
    auto found = m_pending.find(key);
    if (found == m_pending.end()) return false;

    if (found->second.restore) found->second.restore();
    m_pending.erase(found);
    return true;
}

void EditorPropertyTransaction::cancel_all() {
    for (auto& [key, pending] : m_pending) {
        (void)key;
        if (pending.restore) pending.restore();
    }
    m_pending.clear();
}

void EditorPropertyTransaction::discard_all() {
    m_pending.clear();
}

} // namespace Engine
