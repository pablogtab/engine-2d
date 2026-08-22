#pragma once

#include "EditorAiProvider.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

struct EditorAiConversationRecord {
    std::string thread_id;
    std::string session_id;
    std::string title;
    std::string model;
    std::uint32_t tool_contract_version{0};
    std::size_t last_input_tokens{0};
    std::size_t last_output_tokens{0};
    std::size_t last_total_tokens{0};
    std::int64_t updated_at_unix_ms{0};
    std::vector<EditorAiChatMessage> messages;
};

class EditorAiConversationStore {
public:
    static constexpr std::size_t MAX_CONVERSATIONS = 20;
    static constexpr std::size_t MAX_MESSAGES_PER_CONVERSATION = 80;

    explicit EditorAiConversationStore(std::string filepath);

    [[nodiscard]] bool load(std::string& error);
    [[nodiscard]] bool save(std::string& error) const;

    void record(EditorAiConversationRecord conversation);
    [[nodiscard]] bool rename(const std::string& thread_id, std::string title);
    [[nodiscard]] bool erase(const std::string& thread_id);
    [[nodiscard]] bool set_active_thread(std::string thread_id);
    [[nodiscard]] const EditorAiConversationRecord* active_conversation() const;
    [[nodiscard]] const EditorAiConversationRecord* find(const std::string& thread_id) const;
    [[nodiscard]] const std::vector<EditorAiConversationRecord>& conversations() const {
        return m_conversations;
    }
    [[nodiscard]] const std::string& active_thread_id() const { return m_active_thread_id; }
    [[nodiscard]] const std::string& filepath() const { return m_filepath; }

private:
    std::string m_filepath;
    std::string m_active_thread_id;
    std::vector<EditorAiConversationRecord> m_conversations;
};

[[nodiscard]] std::string make_editor_ai_conversation_title(
    const std::vector<EditorAiChatMessage>& messages
);

} // namespace Engine
