#pragma once

#include "EditorCommandHistory.hpp"
#include "IEditorCommand.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Engine {

class EditorValueCommand final : public IEditorCommand {
public:
    using Operation = std::function<void()>;

    EditorValueCommand(Operation apply_before, Operation apply_after, std::string description);

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    Operation m_apply_before;
    Operation m_apply_after;
    std::string m_description;
};

// Tracks live editor previews independently from ImGui. A transaction captures
// the authored value once, permits any number of preview mutations, and emits
// one already-executed command when committed.
class EditorPropertyTransaction {
public:
    using Key = std::uint64_t;

    [[nodiscard]] static Key make_key(std::uint32_t owner, std::string_view property);

    template<
        typename State,
        typename Capture,
        typename Apply,
        typename Equal = std::equal_to<State>,
        typename Finalize = std::function<void()>
    >
    bool begin(
        Key key,
        State before,
        Capture capture,
        Apply apply,
        std::string description,
        Equal equal = Equal{},
        Finalize finalize = [] {}
    ) {
        if (m_pending.contains(key)) return false;

        auto restore = [apply, before]() mutable {
            apply(before);
        };
        auto create_command = [
            before = std::move(before),
            capture = std::move(capture),
            apply = std::move(apply),
            description = std::move(description),
            equal = std::move(equal),
            finalize = std::move(finalize)
        ]() mutable -> std::unique_ptr<IEditorCommand> {
            std::optional<State> current = capture();
            if (!current || equal(before, *current)) return nullptr;

            State after = std::move(*current);
            finalize();
            return std::make_unique<EditorValueCommand>(
                [apply, before]() mutable { apply(before); },
                [apply, after]() mutable { apply(after); },
                description
            );
        };

        m_pending.emplace(key, Pending{std::move(restore), std::move(create_command)});
        return true;
    }

    // The edited value is already live, so commit pushes without executing it again.
    bool commit(Key key, EditorCommandHistory& history);
    bool cancel(Key key);
    void cancel_all();
    void discard_all();

    [[nodiscard]] bool contains(Key key) const { return m_pending.contains(key); }
    [[nodiscard]] std::size_t pending_count() const { return m_pending.size(); }

private:
    struct Pending {
        std::function<void()> restore;
        std::function<std::unique_ptr<IEditorCommand>()> create_command;
    };

    std::unordered_map<Key, Pending> m_pending;
};

} // namespace Engine
