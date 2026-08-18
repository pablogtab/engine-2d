#include "editor/commands/EditorPropertyTransaction.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

struct FakeSpriteState {
    const void* texture{nullptr};
    std::uint8_t opacity{255};
    bool visible{true};

    bool operator==(const FakeSpriteState&) const = default;
};

void test_commit_creates_one_reversible_command() {
    Engine::EditorCommandHistory history;
    Engine::EditorPropertyTransaction transactions;
    int value = 10;
    const auto key = Engine::EditorPropertyTransaction::make_key(1, "Value");

    assert(transactions.begin<int>(
        key,
        value,
        [&]() -> std::optional<int> { return value; },
        [&](int state) { value = state; },
        "Edit Value"
    ));

    value = 20;
    value = 30;
    assert(transactions.commit(key, history));
    assert(history.get_undo_count() == 1);
    assert(value == 30);

    history.undo();
    assert(value == 10);
    history.redo();
    assert(value == 30);
}

void test_unchanged_transaction_is_not_recorded() {
    Engine::EditorCommandHistory history;
    Engine::EditorPropertyTransaction transactions;
    int value = 42;
    const auto key = Engine::EditorPropertyTransaction::make_key(2, "Unchanged");

    assert(transactions.begin<int>(
        key,
        value,
        [&]() -> std::optional<int> { return value; },
        [&](int state) { value = state; },
        "Edit Unchanged Value"
    ));
    assert(!transactions.commit(key, history));
    assert(history.get_undo_count() == 0);
    assert(transactions.pending_count() == 0);
}

void test_cancel_restores_initial_value() {
    Engine::EditorPropertyTransaction transactions;
    int value = 5;
    const auto key = Engine::EditorPropertyTransaction::make_key(3, "Cancel");

    assert(transactions.begin<int>(
        key,
        value,
        [&]() -> std::optional<int> { return value; },
        [&](int state) { value = state; },
        "Edit Cancelled Value"
    ));
    value = 25;
    value = 50;
    value = 99;
    assert(transactions.cancel(key));
    assert(value == 5);
    assert(!transactions.contains(key));
}

void test_text_commit_round_trips() {
    Engine::EditorCommandHistory history;
    Engine::EditorPropertyTransaction transactions;
    std::string name = "Player";
    const auto key = Engine::EditorPropertyTransaction::make_key(3, "Entity.Name");

    assert(transactions.begin<std::string>(
        key,
        name,
        [&]() -> std::optional<std::string> { return name; },
        [&](const std::string& state) { name = state; },
        "Rename Entity"
    ));
    name = "Player_Knight";
    assert(transactions.commit(key, history));
    history.undo();
    assert(name == "Player");
    history.redo();
    assert(name == "Player_Knight");
}

void test_toggle_commit_round_trips() {
    Engine::EditorCommandHistory history;
    Engine::EditorPropertyTransaction transactions;
    bool visible = true;
    const auto key = Engine::EditorPropertyTransaction::make_key(3, "Sprite.Visible");

    assert(transactions.begin<bool>(
        key,
        visible,
        [&]() -> std::optional<bool> { return visible; },
        [&](bool state) { visible = state; },
        "Toggle Sprite Visibility"
    ));
    visible = false;
    assert(transactions.commit(key, history));
    history.undo();
    assert(visible);
    history.redo();
    assert(!visible);
}

void test_duplicate_begin_preserves_original_snapshot() {
    Engine::EditorCommandHistory history;
    Engine::EditorPropertyTransaction transactions;
    int value = 7;
    const auto key = Engine::EditorPropertyTransaction::make_key(4, "Duplicate");

    auto capture = [&]() -> std::optional<int> { return value; };
    auto apply = [&](int state) { value = state; };
    assert(transactions.begin<int>(key, value, capture, apply, "Edit Value"));
    value = 8;
    assert(!transactions.begin<int>(key, value, capture, apply, "Wrong Snapshot"));
    value = 9;
    assert(transactions.commit(key, history));

    history.undo();
    assert(value == 7);
}

void test_texture_assignment_round_trips() {
    Engine::EditorCommandHistory history;
    Engine::EditorPropertyTransaction transactions;
    int first_texture = 1;
    int second_texture = 2;
    FakeSpriteState sprite{&first_texture, 255, true};
    const auto key = Engine::EditorPropertyTransaction::make_key(5, "Sprite.Texture");

    assert(transactions.begin<FakeSpriteState>(
        key,
        sprite,
        [&]() -> std::optional<FakeSpriteState> { return sprite; },
        [&](const FakeSpriteState& state) { sprite = state; },
        "Change Sprite Texture"
    ));
    sprite.texture = &second_texture;
    sprite.opacity = 128;
    assert(transactions.commit(key, history));

    history.undo();
    assert(sprite.texture == &first_texture);
    assert(sprite.opacity == 255);
    history.redo();
    assert(sprite.texture == &second_texture);
    assert(sprite.opacity == 128);
}

void test_missing_target_drops_transaction_safely() {
    Engine::EditorCommandHistory history;
    Engine::EditorPropertyTransaction transactions;
    int value = 1;
    bool target_exists = true;
    const auto key = Engine::EditorPropertyTransaction::make_key(6, "DeletedTarget");

    assert(transactions.begin<int>(
        key,
        value,
        [&]() -> std::optional<int> {
            if (!target_exists) return std::nullopt;
            return value;
        },
        [&](int state) {
            if (target_exists) value = state;
        },
        "Edit Deleted Target"
    ));
    value = 2;
    target_exists = false;
    assert(!transactions.commit(key, history));
    assert(history.get_undo_count() == 0);
}

void test_finalize_runs_once_only_for_changed_commit() {
    Engine::EditorCommandHistory history;
    Engine::EditorPropertyTransaction transactions;
    int value = 10;
    int finalize_count = 0;
    int apply_count = 0;
    const auto key = Engine::EditorPropertyTransaction::make_key(7, "Finalize");

    assert(transactions.begin<int>(
        key,
        value,
        [&]() -> std::optional<int> { return value; },
        [&](int state) {
            value = state;
            ++apply_count;
        },
        "Edit Finalized Value",
        std::equal_to<int>{},
        [&]() { ++finalize_count; }
    ));
    value = 20;
    value = 30;
    assert(transactions.commit(key, history));
    assert(finalize_count == 1);

    history.undo();
    assert(apply_count == 1);
    history.redo();
    assert(apply_count == 2);
    assert(finalize_count == 1);

    assert(transactions.begin<int>(
        key,
        value,
        [&]() -> std::optional<int> { return value; },
        [&](int state) {
            value = state;
            ++apply_count;
        },
        "Edit Unchanged Finalized Value",
        std::equal_to<int>{},
        [&]() { ++finalize_count; }
    ));
    assert(!transactions.commit(key, history));
    assert(finalize_count == 1);
    assert(apply_count == 2);

    assert(transactions.begin<int>(
        key,
        value,
        [&]() -> std::optional<int> { return value; },
        [&](int state) {
            value = state;
            ++apply_count;
        },
        "Cancel Finalized Value",
        std::equal_to<int>{},
        [&]() { ++finalize_count; }
    ));
    value = 50;
    assert(transactions.cancel(key));
    assert(value == 30);
    assert(apply_count == 3);
    assert(finalize_count == 1);
}

} // namespace

int main() {
    test_commit_creates_one_reversible_command();
    test_unchanged_transaction_is_not_recorded();
    test_cancel_restores_initial_value();
    test_text_commit_round_trips();
    test_toggle_commit_round_trips();
    test_duplicate_begin_preserves_original_snapshot();
    test_texture_assignment_round_trips();
    test_missing_target_drops_transaction_safely();
    test_finalize_runs_once_only_for_changed_commit();
    std::cout << "EditorPropertyTransactionTests passed\n";
    return 0;
}
