#include "editor/ai/EditorAiConversationStore.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

namespace {

std::filesystem::path make_test_directory() {
    const auto path = std::filesystem::temp_directory_path() /
        "engine2d_ai_conversation_store_tests";
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
    assert(!error);
    return path;
}

Engine::EditorAiConversationRecord make_conversation(
    std::string thread_id,
    std::string prompt,
    std::int64_t updated_at
) {
    return {
        .thread_id = std::move(thread_id),
        .session_id = "session-test",
        .title = {},
        .model = "gpt-5.6-sol",
        .tool_contract_version = 2,
        .last_input_tokens = 24000,
        .last_output_tokens = 500,
        .last_total_tokens = 24500,
        .updated_at_unix_ms = updated_at,
        .messages = {
            {"user", std::move(prompt)},
            {"assistant", "Done."}
        }
    };
}

void test_persists_active_conversation_and_messages() {
    const auto filepath = make_test_directory() / "conversations.json";
    Engine::EditorAiConversationStore store(filepath.string());
    store.record(make_conversation("thread-first", "Create a player", 100));
    auto second = make_conversation("thread-second", "Explain the renderer", 200);
    second.messages.push_back({"editor", "Applied successfully."});
    store.record(std::move(second));
    assert(store.active_thread_id() == "thread-second");

    std::string error;
    assert(store.save(error));
    assert(error.empty());

    Engine::EditorAiConversationStore loaded(filepath.string());
    assert(loaded.load(error));
    assert(error.empty());
    assert(loaded.conversations().size() == 2);
    assert(loaded.conversations().front().thread_id == "thread-second");
    assert(loaded.active_thread_id() == "thread-second");
    assert(loaded.active_conversation());
    assert(loaded.active_conversation()->messages.size() == 3);
    assert(loaded.active_conversation()->messages.back().role == "editor");
    assert(loaded.active_conversation()->messages.back().content == "Applied successfully.");
    assert(loaded.active_conversation()->title == "Explain the renderer");
    assert(loaded.active_conversation()->tool_contract_version == 2);
    assert(loaded.active_conversation()->last_input_tokens == 24000);
    assert(loaded.active_conversation()->last_output_tokens == 500);
    assert(loaded.active_conversation()->last_total_tokens == 24500);
}

void test_switches_and_clears_active_conversation_without_deleting_history() {
    const auto filepath = make_test_directory() / "selection.json";
    Engine::EditorAiConversationStore store(filepath.string());
    store.record(make_conversation("thread-first", "First", 100));
    store.record(make_conversation("thread-second", "Second", 200));

    assert(store.set_active_thread("thread-first"));
    assert(store.active_conversation()->thread_id == "thread-first");
    assert(!store.set_active_thread("missing"));
    assert(store.active_conversation()->thread_id == "thread-first");
    assert(store.set_active_thread(""));
    assert(store.active_conversation() == nullptr);
    assert(store.conversations().size() == 2);
}

void test_replaces_existing_record_and_bounds_history() {
    Engine::EditorAiConversationStore store(
        (make_test_directory() / "bounded.json").string()
    );
    for (std::size_t index = 0;
         index < Engine::EditorAiConversationStore::MAX_CONVERSATIONS + 3;
         ++index) {
        store.record(make_conversation(
            "thread-" + std::to_string(index),
            "Prompt " + std::to_string(index),
            static_cast<std::int64_t>(index)
        ));
    }
    assert(store.conversations().size() == Engine::EditorAiConversationStore::MAX_CONVERSATIONS);
    assert(store.conversations().front().thread_id == "thread-22");

    auto replacement = make_conversation("thread-22", "Updated prompt", 500);
    replacement.messages.resize(
        Engine::EditorAiConversationStore::MAX_MESSAGES_PER_CONVERSATION + 5,
        {"assistant", "Message"}
    );
    store.record(std::move(replacement));
    assert(store.conversations().size() == Engine::EditorAiConversationStore::MAX_CONVERSATIONS);
    assert(store.conversations().front().title == "Updated prompt");
    assert(
        store.conversations().front().messages.size() ==
        Engine::EditorAiConversationStore::MAX_MESSAGES_PER_CONVERSATION
    );
}

void test_rejects_invalid_store_without_partial_state() {
    const auto filepath = make_test_directory() / "invalid.json";
    {
        std::ofstream file(filepath);
        assert(file.is_open());
        file << "not json";
    }
    Engine::EditorAiConversationStore store(filepath.string());
    std::string error;
    assert(!store.load(error));
    assert(!error.empty());
    assert(store.conversations().empty());
    assert(store.active_thread_id().empty());
}

void test_ignores_malformed_conversation_entries() {
    const auto filepath = make_test_directory() / "malformed-entry.json";
    {
        std::ofstream file(filepath);
        assert(file.is_open());
        file << R"({
  "version": 1,
  "active_thread_id": 42,
  "conversations": [
    {"thread_id": 99, "messages": "invalid"},
    {"thread_id": "valid", "session_id": 17, "messages": [
      {"role": "user", "content": "Hello"},
      {"role": 2, "content": false}
    ]}
  ]
})";
    }
    Engine::EditorAiConversationStore store(filepath.string());
    std::string error;
    assert(store.load(error));
    assert(error.empty());
    assert(store.conversations().size() == 1);
    assert(store.conversations().front().thread_id == "valid");
    assert(store.conversations().front().session_id == "valid");
    assert(store.conversations().front().messages.size() == 1);
    assert(store.active_thread_id().empty());
}

void test_renames_and_deletes_saved_conversations() {
    const auto filepath = make_test_directory() / "mutations.json";
    Engine::EditorAiConversationStore store(filepath.string());
    store.record(make_conversation("thread-first", "First", 100));
    store.record(make_conversation("thread-second", "Second", 200));

    assert(!store.rename("missing", "Renamed"));
    assert(!store.rename("thread-first", "  \n\t "));
    assert(store.rename("thread-first", "  Renamed conversation  \n"));
    assert(store.find("thread-first"));
    assert(store.find("thread-first")->title == "Renamed conversation");

    assert(!store.erase("missing"));
    assert(store.erase("thread-first"));
    assert(store.find("thread-first") == nullptr);
    assert(store.active_thread_id() == "thread-second");
    assert(store.erase("thread-second"));
    assert(store.active_thread_id().empty());
    assert(store.conversations().empty());
}

void test_loads_legacy_conversations_as_contract_zero() {
    const auto filepath = make_test_directory() / "legacy-contract.json";
    {
        std::ofstream file(filepath);
        assert(file.is_open());
        file << R"({
  "version": 1,
  "active_thread_id": "legacy",
  "conversations": [{
    "thread_id": "legacy",
    "model": "gpt-5.6-sol",
    "messages": [{"role": "user", "content": "Continue"}]
  }]
})";
    }
    Engine::EditorAiConversationStore store(filepath.string());
    std::string error;
    assert(store.load(error));
    assert(store.active_conversation());
    assert(store.active_conversation()->tool_contract_version == 0);
}

} // namespace

int main() {
    test_persists_active_conversation_and_messages();
    test_switches_and_clears_active_conversation_without_deleting_history();
    test_replaces_existing_record_and_bounds_history();
    test_rejects_invalid_store_without_partial_state();
    test_ignores_malformed_conversation_entries();
    test_renames_and_deletes_saved_conversations();
    test_loads_legacy_conversations_as_contract_zero();
    std::cout << "EditorAiConversationStoreTests passed\n";
    return 0;
}
