#include <cassert>
#include <iostream>
#include "enginescript/async/Task.hpp"
#include "enginescript/async/AsyncScheduler.hpp"

using namespace EngineScript;

Task<int> async_add(int a, int b) {
    co_return a + b;
}

Task<void> delayed_action(AsyncScheduler& scheduler, bool& flag, uint32_t scope_id) {
    co_await scheduler.delay(0.5f, scope_id);
    flag = true;
    co_return;
}

int main() {
    std::cout << "[TEST] Running EngineScriptAsyncTests...\n";

    // Test 1: Direct Task<T> execution
    auto t1 = async_add(10, 20);
    assert(t1.is_done());
    assert(t1.get_result() == 30);

    // Test 2: Scheduler delay and time stepping
    AsyncScheduler scheduler;
    bool triggered = false;
    uint32_t entity_scope = 101;

    auto t2 = delayed_action(scheduler, triggered, entity_scope);
    assert(scheduler.pending_tasks_count() == 1);
    assert(!triggered);

    // Advance time by 0.2s -> should not trigger yet
    scheduler.step(0.2f, 0.2f);
    assert(!triggered);
    assert(scheduler.pending_tasks_count() == 1);

    // Advance time by 0.4s (total 0.6s >= 0.5s) -> triggers!
    scheduler.step(0.4f, 0.4f);
    assert(triggered);
    assert(scheduler.pending_tasks_count() == 0);

    // Test 3: Scope cancellation on entity destruction
    bool cancelled_flag = false;
    uint32_t doomed_entity = 202;
    auto t3 = delayed_action(scheduler, cancelled_flag, doomed_entity);
    assert(scheduler.pending_tasks_count() == 1);

    // Cancel entity scope
    scheduler.cancel_scope(doomed_entity);

    // Advance time past delay
    scheduler.step(1.0f, 1.0f);
    assert(!cancelled_flag); // Cancelled task did not run!

    std::cout << "[PASS] EngineScriptAsyncTests passed!\n";
    return 0;
}
