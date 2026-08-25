#include "editor/ai/EditorAiAuthoringSession.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

namespace {

using Engine::EditorAiAuthoringSession;
using namespace std::chrono_literals;

void test_iterates_with_bounded_action_budget() {
    EditorAiAuthoringSession session;
    session.begin(7, "Create and align a platform", 3, 10min);
    assert(session.active());
    assert(session.remaining_work_time() == 10min);

    std::string error;
    assert(session.record_applied_batch(25, error));
    assert(session.applied_batches() == 1);
    assert(session.total_actions() == 25);
    session.record_work_time(75s);
    assert(session.remaining_work_time() == 525s);
    assert(session.can_continue());
    assert(session.continuation_prompt().find("Create and align a platform") != std::string::npos);
    assert(session.continuation_prompt().find("remaining active AI work time: 525") !=
        std::string::npos);
}

void test_verification_finishes_session() {
    EditorAiAuthoringSession session;
    session.begin(9, "Fix lighting", 0, 5min);
    std::string error;
    assert(session.record_applied_batch(2, error));
    session.finish_verified();
    assert(!session.active());
    assert(session.status() == EditorAiAuthoringSession::Status::Verified);
    assert(session.history_description() == "AI Iterative Session (1 batch, 2 actions)");
}

void test_atomic_validation_is_not_reported_as_visual_verification() {
    EditorAiAuthoringSession session;
    session.begin(10, "Attach a sprite", 0, 5min);
    std::string error;
    assert(session.record_applied_batch(2, error));
    session.finish_validated_atomically();
    assert(!session.active());
    assert(session.status() == EditorAiAuthoringSession::Status::ValidatedAtomically);
    assert(session.status_label() == "Data validated");
    assert(session.outcome().find("No visual Codex iteration") != std::string::npos);
}

void test_elapsed_work_time_requests_one_final_verification() {
    EditorAiAuthoringSession session;
    session.begin(11, "Populate a scene", 0, 2min);
    session.record_work_time(121s);
    std::string error;
    assert(session.record_applied_batch(1, error));
    assert(session.budget_exhausted());
    assert(session.final_verification_only());
    assert(session.can_continue());

    session.finish_time_budget();
    assert(!session.can_continue());
    assert(session.status() == EditorAiAuthoringSession::Status::TimeBudgetReached);
}

void test_action_count_does_not_end_a_session() {
    EditorAiAuthoringSession session;
    session.begin(13, "Large edit", 0, 10min);
    std::string error;
    for (int batch = 0; batch < 8; ++batch) {
        assert(session.record_applied_batch(25, error));
    }
    assert(session.applied_batches() == 8);
    assert(session.total_actions() == 200);
    assert(session.active());
    assert(session.can_continue());
}

} // namespace

int main() {
    test_iterates_with_bounded_action_budget();
    test_verification_finishes_session();
    test_atomic_validation_is_not_reported_as_visual_verification();
    test_elapsed_work_time_requests_one_final_verification();
    test_action_count_does_not_end_a_session();
    std::cout << "Editor AI authoring session tests passed\n";
    return 0;
}
