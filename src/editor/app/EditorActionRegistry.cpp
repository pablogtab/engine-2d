#include "EditorActionRegistry.hpp"

#include <algorithm>
#include <cctype>

namespace Engine {

namespace {

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

} // namespace

bool EditorActionRegistry::register_action(EditorAction action) {
    if (action.id.empty() || action.label.empty() || !action.execute || m_indices.contains(action.id)) {
        return false;
    }
    m_indices.emplace(action.id, m_actions.size());
    m_actions.push_back(std::move(action));
    return true;
}

const EditorAction* EditorActionRegistry::find(std::string_view id) const {
    const auto found = m_indices.find(std::string(id));
    return found == m_indices.end() ? nullptr : &m_actions[found->second];
}

bool EditorActionRegistry::execute(std::string_view id) const {
    const EditorAction* action = find(id);
    if (!action || (action->enabled && !action->enabled())) return false;
    action->execute();
    return true;
}

bool EditorActionRegistry::is_enabled(std::string_view id) const {
    const EditorAction* action = find(id);
    return action && (!action->enabled || action->enabled());
}

bool EditorActionRegistry::is_checked(std::string_view id) const {
    const EditorAction* action = find(id);
    return action && action->checked && action->checked();
}

std::vector<const EditorAction*> EditorActionRegistry::search(std::string_view query) const {
    const std::string needle = lowercase(query);
    std::vector<const EditorAction*> results;
    for (const EditorAction& action : m_actions) {
        if (needle.empty()) {
            results.push_back(&action);
            continue;
        }
        const std::string haystack = lowercase(action.label + " " + action.category + " " + action.id);
        if (haystack.find(needle) != std::string::npos) results.push_back(&action);
    }
    return results;
}

} // namespace Engine
