#include "EditorEntityStateCommands.hpp"
#include "../../core/Log.hpp"

namespace Engine {

namespace {

bool get_field_value(const EditorEntityStateStore& store, EntityID id, EditorEntityStateField field) {
    if (field == EditorEntityStateField::Locked) {
        return store.is_locked(id);
    }
    return store.is_visible(id);
}

void set_field_value(EditorEntityStateStore& store, EntityID id, EditorEntityStateField field, bool value) {
    if (field == EditorEntityStateField::Locked) {
        store.set_locked(id, value);
    } else {
        store.set_visible(id, value);
    }
}

std::string default_description(EditorEntityStateField field, bool new_val, size_t count) {
    std::string action;
    if (field == EditorEntityStateField::Locked) {
        action = new_val ? "Lock" : "Unlock";
    } else {
        action = new_val ? "Show" : "Hide";
    }

    if (count == 1) {
        return action + (field == EditorEntityStateField::VisibleInEditor ? " in Scene View" : " Entity");
    }
    return action + " (" + std::to_string(count) + ") Entities" + (field == EditorEntityStateField::VisibleInEditor ? " in Scene View" : "");
}

} // namespace

SetEditorEntityStateCommand::SetEditorEntityStateCommand(
    const EntityManager* em,
    std::shared_ptr<EditorEntityStateStore> store,
    EditorEntityStateField field,
    std::vector<Entry> entries,
    std::string description
) : m_em(em),
    m_store(std::move(store)),
    m_field(field),
    m_entries(std::move(entries)),
    m_description(std::move(description)) {
    if (!m_em || !m_store || m_entries.empty()) {
        m_valid = false;
        return;
    }

    for (const auto& entry : m_entries) {
        if (!m_em->is_alive(entry.id)) {
            m_valid = false;
            return;
        }
    }

    if (m_description.empty()) {
        m_description = default_description(m_field, m_entries.front().after_value, m_entries.size());
    }
    m_valid = true;
}

SetEditorEntityStateCommand::SetEditorEntityStateCommand(
    const EntityManager* em,
    std::shared_ptr<EditorEntityStateStore> store,
    EditorEntityStateField field,
    EntityID entity,
    bool new_value,
    std::string description
) : m_em(em),
    m_store(std::move(store)),
    m_field(field),
    m_description(std::move(description)) {
    if (!m_em || !m_store || !m_em->is_alive(entity)) {
        m_valid = false;
        return;
    }

    const bool current_val = get_field_value(*m_store, entity, m_field);
    m_entries.push_back(Entry{
        .id = entity,
        .before_value = current_val,
        .after_value = new_value
    });

    if (m_description.empty()) {
        m_description = default_description(m_field, new_value, 1);
    }
    m_valid = true;
}

SetEditorEntityStateCommand::SetEditorEntityStateCommand(
    const EntityManager* em,
    std::shared_ptr<EditorEntityStateStore> store,
    EditorEntityStateField field,
    const std::vector<EntityID>& entities,
    bool new_value,
    std::string description
) : m_em(em),
    m_store(std::move(store)),
    m_field(field),
    m_description(std::move(description)) {
    if (!m_em || !m_store || entities.empty()) {
        m_valid = false;
        return;
    }

    for (EntityID id : entities) {
        if (!m_em->is_alive(id)) {
            m_valid = false;
            return;
        }
        const bool current_val = get_field_value(*m_store, id, m_field);
        m_entries.push_back(Entry{
            .id = id,
            .before_value = current_val,
            .after_value = new_value
        });
    }

    if (m_description.empty()) {
        m_description = default_description(m_field, new_value, m_entries.size());
    }
    m_valid = true;
}

bool SetEditorEntityStateCommand::execute() {
    if (!m_valid || !m_em || !m_store) return false;

    // Check all entities are alive before mutating
    for (const auto& entry : m_entries) {
        if (!m_em->is_alive(entry.id)) {
            ENGINE_LOG_ERROR("[COMMAND] SetEditorEntityStateCommand failed: entity #{} is not alive", entry.id);
            return false;
        }
    }

    for (const auto& entry : m_entries) {
        set_field_value(*m_store, entry.id, m_field, entry.after_value);
    }
    return true;
}

bool SetEditorEntityStateCommand::undo() {
    if (!m_valid || !m_em || !m_store) return false;

    // Check all entities are alive before mutating
    for (const auto& entry : m_entries) {
        if (!m_em->is_alive(entry.id)) {
            ENGINE_LOG_ERROR("[COMMAND] SetEditorEntityStateCommand undo failed: entity #{} is not alive", entry.id);
            return false;
        }
    }

    for (const auto& entry : m_entries) {
        set_field_value(*m_store, entry.id, m_field, entry.before_value);
    }
    return true;
}

} // namespace Engine
