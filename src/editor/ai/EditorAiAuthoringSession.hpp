#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Engine {

class EditorAiAuthoringSession {
public:
    static constexpr std::chrono::minutes DEFAULT_WORK_BUDGET{10};

    enum class Status {
        Idle,
        Active,
        ValidatedAtomically,
        Verified,
        TimeBudgetReached,
        Cancelled,
        Failed
    };

    void begin(
        std::uint64_t root_request_id,
        std::string original_prompt,
        std::uint64_t revision_before,
        std::chrono::milliseconds work_budget
    );
    [[nodiscard]] bool record_applied_batch(std::size_t action_count, std::string& error);
    void record_work_time(std::chrono::milliseconds duration);
    void finish_validated_atomically();
    void finish_verified();
    void finish_time_budget();
    void finish_cancelled(std::string reason);
    void finish_failed(std::string reason);
    void reset();

    [[nodiscard]] bool active() const { return m_status == Status::Active; }
    [[nodiscard]] bool can_continue() const;
    [[nodiscard]] bool budget_exhausted() const;
    [[nodiscard]] bool final_verification_only() const;
    [[nodiscard]] std::chrono::milliseconds remaining_work_time() const;
    [[nodiscard]] std::string continuation_prompt() const;
    [[nodiscard]] std::string history_description() const;
    [[nodiscard]] std::string status_label() const;

    [[nodiscard]] Status status() const { return m_status; }
    [[nodiscard]] std::uint64_t root_request_id() const { return m_root_request_id; }
    [[nodiscard]] const std::string& original_prompt() const { return m_original_prompt; }
    [[nodiscard]] const std::string& outcome() const { return m_outcome; }
    [[nodiscard]] std::uint64_t revision_before() const { return m_revision_before; }
    [[nodiscard]] std::size_t applied_batches() const { return m_applied_batches; }
    [[nodiscard]] std::size_t total_actions() const { return m_total_actions; }
    [[nodiscard]] std::chrono::milliseconds work_time() const { return m_work_time; }
    [[nodiscard]] std::chrono::milliseconds work_budget() const { return m_work_budget; }

private:
    Status m_status{Status::Idle};
    std::uint64_t m_root_request_id{0};
    std::string m_original_prompt;
    std::string m_outcome;
    std::uint64_t m_revision_before{0};
    std::size_t m_applied_batches{0};
    std::size_t m_total_actions{0};
    std::chrono::milliseconds m_work_time{0};
    std::chrono::milliseconds m_work_budget{DEFAULT_WORK_BUDGET};
    bool m_final_verification_required{false};
};

} // namespace Engine
