#include "EditorAiConversationStore.hpp"

#include "../../core/FileSystem.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <utility>

namespace Engine {
namespace {

constexpr int STORE_VERSION = 1;
constexpr std::size_t MAX_TITLE_CHARACTERS = 64;

std::string normalize_title(std::string title) {
    std::replace(title.begin(), title.end(), '\n', ' ');
    std::replace(title.begin(), title.end(), '\r', ' ');
    const auto first = title.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = title.find_last_not_of(" \t");
    title = title.substr(first, last - first + 1);
    if (title.size() > MAX_TITLE_CHARACTERS) {
        std::size_t end = MAX_TITLE_CHARACTERS - 3;
        while (end > 0 && (static_cast<unsigned char>(title[end]) & 0xc0) == 0x80) --end;
        title.resize(end);
        title += "...";
    }
    return title;
}

std::string string_field(const nlohmann::json& value, const char* key) {
    const auto found = value.find(key);
    return found != value.end() && found->is_string() ? found->get<std::string>() : "";
}

std::size_t size_field(const nlohmann::json& value, const char* key) {
    const auto found = value.find(key);
    if (found == value.end()) return 0;
    if (found->is_number_unsigned()) return found->get<std::size_t>();
    if (!found->is_number_integer()) return 0;
    const std::int64_t parsed = found->get<std::int64_t>();
    return parsed >= 0 ? static_cast<std::size_t>(parsed) : 0;
}

nlohmann::json serialize_message(const EditorAiChatMessage& message) {
    return {{"role", message.role}, {"content", message.content}};
}

nlohmann::json serialize_conversation(const EditorAiConversationRecord& conversation) {
    nlohmann::json messages = nlohmann::json::array();
    for (const EditorAiChatMessage& message : conversation.messages) {
        messages.push_back(serialize_message(message));
    }
    return {
        {"thread_id", conversation.thread_id},
        {"session_id", conversation.session_id},
        {"title", conversation.title},
        {"model", conversation.model},
        {"tool_contract_version", conversation.tool_contract_version},
        {"last_input_tokens", conversation.last_input_tokens},
        {"last_output_tokens", conversation.last_output_tokens},
        {"last_total_tokens", conversation.last_total_tokens},
        {"updated_at_unix_ms", conversation.updated_at_unix_ms},
        {"messages", std::move(messages)}
    };
}

std::vector<EditorAiChatMessage> parse_messages(const nlohmann::json& value) {
    std::vector<EditorAiChatMessage> messages;
    if (!value.is_array()) return messages;
    for (const auto& item : value) {
        if (!item.is_object()) continue;
        const std::string role = string_field(item, "role");
        const std::string content = string_field(item, "content");
        if ((role != "user" && role != "assistant" && role != "editor") ||
            content.empty()) {
            continue;
        }
        messages.push_back({role, content});
    }
    if (messages.size() > EditorAiConversationStore::MAX_MESSAGES_PER_CONVERSATION) {
        messages.erase(
            messages.begin(),
            messages.end() - EditorAiConversationStore::MAX_MESSAGES_PER_CONVERSATION
        );
    }
    return messages;
}

} // namespace

EditorAiConversationStore::EditorAiConversationStore(std::string filepath)
    : m_filepath(std::move(filepath)) {}

bool EditorAiConversationStore::load(std::string& error) {
    error.clear();
    m_active_thread_id.clear();
    m_conversations.clear();
    if (!FileSystem::exists(m_filepath)) return true;

    const std::string contents = FileSystem::read_text(m_filepath);
    const nlohmann::json root = nlohmann::json::parse(contents, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "Conversation history is not valid JSON";
        return false;
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer() ||
        version->get<int>() != STORE_VERSION) {
        error = "Conversation history has an unsupported version";
        return false;
    }

    const nlohmann::json conversations = root.value("conversations", nlohmann::json::array());
    if (!conversations.is_array()) {
        error = "Conversation history does not contain a valid conversation list";
        return false;
    }
    for (const auto& item : conversations) {
        if (!item.is_object()) continue;
        EditorAiConversationRecord conversation{
            .thread_id = string_field(item, "thread_id"),
            .session_id = string_field(item, "session_id"),
            .title = string_field(item, "title"),
            .model = string_field(item, "model"),
            .tool_contract_version = item.contains("tool_contract_version") &&
                    item.at("tool_contract_version").is_number_integer() &&
                    item.at("tool_contract_version").get<std::int64_t>() >= 0
                ? item.at("tool_contract_version").get<std::uint32_t>()
                : 0,
            .last_input_tokens = size_field(item, "last_input_tokens"),
            .last_output_tokens = size_field(item, "last_output_tokens"),
            .last_total_tokens = size_field(item, "last_total_tokens"),
            .updated_at_unix_ms = item.contains("updated_at_unix_ms") &&
                    item.at("updated_at_unix_ms").is_number_integer()
                ? item.at("updated_at_unix_ms").get<std::int64_t>()
                : 0,
            .messages = parse_messages(
                item.contains("messages") ? item.at("messages") : nlohmann::json::array()
            )
        };
        if (conversation.thread_id.empty()) continue;
        if (conversation.session_id.empty()) conversation.session_id = conversation.thread_id;
        if (conversation.title.empty()) {
            conversation.title = make_editor_ai_conversation_title(conversation.messages);
        }
        const bool duplicate = std::any_of(
            m_conversations.begin(),
            m_conversations.end(),
            [&](const EditorAiConversationRecord& existing) {
                return existing.thread_id == conversation.thread_id;
            }
        );
        if (!duplicate) m_conversations.push_back(std::move(conversation));
        if (m_conversations.size() == MAX_CONVERSATIONS) break;
    }

    const std::string active_thread_id = string_field(root, "active_thread_id");
    if (!active_thread_id.empty() && find(active_thread_id)) {
        m_active_thread_id = active_thread_id;
    }
    return true;
}

bool EditorAiConversationStore::save(std::string& error) const {
    error.clear();
    nlohmann::json conversations = nlohmann::json::array();
    for (const EditorAiConversationRecord& conversation : m_conversations) {
        conversations.push_back(serialize_conversation(conversation));
    }
    const nlohmann::json root{
        {"version", STORE_VERSION},
        {"active_thread_id", m_active_thread_id},
        {"conversations", std::move(conversations)}
    };
    if (FileSystem::write_text_atomically(m_filepath, root.dump(2) + '\n')) return true;
    error = "Could not save conversation history";
    return false;
}

void EditorAiConversationStore::record(EditorAiConversationRecord conversation) {
    if (conversation.thread_id.empty()) return;
    if (conversation.session_id.empty()) conversation.session_id = conversation.thread_id;
    if (conversation.title.empty()) {
        conversation.title = make_editor_ai_conversation_title(conversation.messages);
    }
    if (conversation.messages.size() > MAX_MESSAGES_PER_CONVERSATION) {
        conversation.messages.erase(
            conversation.messages.begin(),
            conversation.messages.end() - MAX_MESSAGES_PER_CONVERSATION
        );
    }
    std::erase_if(m_conversations, [&](const EditorAiConversationRecord& existing) {
        return existing.thread_id == conversation.thread_id;
    });
    m_conversations.insert(m_conversations.begin(), std::move(conversation));
    if (m_conversations.size() > MAX_CONVERSATIONS) {
        m_conversations.resize(MAX_CONVERSATIONS);
    }
    m_active_thread_id = m_conversations.front().thread_id;
}

bool EditorAiConversationStore::rename(const std::string& thread_id, std::string title) {
    EditorAiConversationRecord* conversation = nullptr;
    for (EditorAiConversationRecord& candidate : m_conversations) {
        if (candidate.thread_id == thread_id) {
            conversation = &candidate;
            break;
        }
    }
    title = normalize_title(std::move(title));
    if (!conversation || title.empty()) return false;
    conversation->title = std::move(title);
    return true;
}

bool EditorAiConversationStore::erase(const std::string& thread_id) {
    const auto previous_size = m_conversations.size();
    std::erase_if(m_conversations, [&](const EditorAiConversationRecord& conversation) {
        return conversation.thread_id == thread_id;
    });
    if (m_conversations.size() == previous_size) return false;
    if (m_active_thread_id == thread_id) m_active_thread_id.clear();
    return true;
}

bool EditorAiConversationStore::set_active_thread(std::string thread_id) {
    if (!thread_id.empty() && !find(thread_id)) return false;
    m_active_thread_id = std::move(thread_id);
    return true;
}

const EditorAiConversationRecord* EditorAiConversationStore::active_conversation() const {
    return find(m_active_thread_id);
}

const EditorAiConversationRecord* EditorAiConversationStore::find(
    const std::string& thread_id
) const {
    const auto found = std::find_if(
        m_conversations.begin(),
        m_conversations.end(),
        [&](const EditorAiConversationRecord& conversation) {
            return conversation.thread_id == thread_id;
        }
    );
    return found == m_conversations.end() ? nullptr : &*found;
}

std::string make_editor_ai_conversation_title(
    const std::vector<EditorAiChatMessage>& messages
) {
    const auto first_user = std::find_if(
        messages.begin(),
        messages.end(),
        [](const EditorAiChatMessage& message) { return message.role == "user"; }
    );
    if (first_user == messages.end()) return "Untitled conversation";

    return normalize_title(first_user->content);
}

} // namespace Engine
