#include "EditorAiAuthoringSession.hpp"

#include <algorithm>

namespace Engine {

void EditorAiAuthoringSession::begin(
    std::uint64_t root_request_id,
    std::string original_prompt,
    std::uint64_t revision_before,
    std::chrono::milliseconds work_budget
) {
    m_status = Status::Active;
    m_root_request_id = root_request_id;
    m_original_prompt = std::move(original_prompt);
    m_outcome.clear();
    m_revision_before = revision_before;
    m_applied_batches = 0;
    m_total_actions = 0;
    m_work_time = std::chrono::milliseconds{0};
    m_work_budget = std::max(std::chrono::milliseconds{1}, work_budget);
    m_final_verification_required = false;
}

bool EditorAiAuthoringSession::record_applied_batch(
    std::size_t action_count,
    std::string& error
) {
    error.clear();
    if (!active()) {
        error = "The iterative authoring session is not active";
        return false;
    }
    if (action_count == 0) {
        error = "An applied authoring batch cannot be empty";
        return false;
    }
    ++m_applied_batches;
    m_total_actions += action_count;
    if (budget_exhausted()) m_final_verification_required = true;
    return true;
}

void EditorAiAuthoringSession::record_work_time(std::chrono::milliseconds duration) {
    if (!active() || duration.count() <= 0) return;
    m_work_time += duration;
}

void EditorAiAuthoringSession::finish_validated_atomically() {
    if (!active()) return;
    m_status = Status::ValidatedAtomically;
    m_outcome =
        "The editor validated the applied data atomically. No visual Codex iteration was run.";
}

void EditorAiAuthoringSession::finish_verified() {
    if (!active()) return;
    m_status = Status::Verified;
    m_outcome = "The applied result was inspected and no further changes were requested.";
}

void EditorAiAuthoringSession::finish_time_budget() {
    if (!active()) return;
    m_status = Status::TimeBudgetReached;
    m_outcome = "The session completed its final inspection after using the configured AI work time.";
    m_final_verification_required = false;
}

void EditorAiAuthoringSession::finish_cancelled(std::string reason) {
    if (!active()) return;
    m_status = Status::Cancelled;
    m_outcome = std::move(reason);
}

void EditorAiAuthoringSession::finish_failed(std::string reason) {
    if (!active()) return;
    m_status = Status::Failed;
    m_outcome = std::move(reason);
}

void EditorAiAuthoringSession::reset() {
    *this = EditorAiAuthoringSession{};
}

bool EditorAiAuthoringSession::can_continue() const {
    return active() && (!budget_exhausted() || m_final_verification_required);
}

bool EditorAiAuthoringSession::budget_exhausted() const {
    return m_work_time >= m_work_budget;
}

bool EditorAiAuthoringSession::final_verification_only() const {
    return active() && budget_exhausted() && m_final_verification_required;
}

std::chrono::milliseconds EditorAiAuthoringSession::remaining_work_time() const {
    return budget_exhausted()
        ? std::chrono::milliseconds{0}
        : m_work_budget - m_work_time;
}

std::string EditorAiAuthoringSession::continuation_prompt() const {
    std::string prompt =
        "Continue the same editor authoring task after the previous batch was applied. "
        "Original goal: " + m_original_prompt + "\n"
        "Inspect the actual current scene with read-only editor tools. Use capture_viewport "
        "when the result has a visual or spatial aspect. Do not repeat already applied actions. "
        "If the result satisfies the original goal, queue no authoring actions and briefly state "
        "that verification is complete. If it needs correction, queue one focused correction "
        "batch. Applied batches: " + std::to_string(m_applied_batches) +
        "; actions applied: " + std::to_string(m_total_actions) +
        "; remaining active AI work time: " +
        std::to_string(remaining_work_time().count() / 1000) + " seconds.";
    if (final_verification_only()) {
        prompt +=
            " The configured work time has been used. Perform one final read-only inspection "
            "and report the current result without making or proposing more changes.";
    }
    return prompt;
}

std::string EditorAiAuthoringSession::history_description() const {
    return "AI Iterative Session (" + std::to_string(m_applied_batches) +
        (m_applied_batches == 1 ? " batch, " : " batches, ") +
        std::to_string(m_total_actions) +
        (m_total_actions == 1 ? " action)" : " actions)");
}

std::string EditorAiAuthoringSession::status_label() const {
    switch (m_status) {
        case Status::Idle: return "Idle";
        case Status::Active: return "Inspecting";
        case Status::ValidatedAtomically: return "Data validated";
        case Status::Verified: return "Verified";
        case Status::TimeBudgetReached: return "Work time reached";
        case Status::Cancelled: return "Cancelled";
        case Status::Failed: return "Failed";
    }
    return "Unknown";
}

} // namespace Engine
