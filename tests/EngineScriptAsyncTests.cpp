#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>
#include "enginescript/async/Task.hpp"
#include "enginescript/async/AsyncScheduler.hpp"
#include "enginescript/async/Jobs.hpp"
#include "enginescript/async/TaskScope.hpp"
#include "enginescript/async/TypedEventBus.hpp"
#include "enginescript/module/GameModule.hpp"

using namespace EngineScript;

Task<int> async_add(int a, int b) {
    co_return a + b;
}

Task<void> delayed_action(AsyncScheduler& scheduler, bool& flag, uint32_t scope_id) {
    co_await scheduler.delay(0.5f, scope_id);
    flag = true;
    co_return;
}

struct EnemyDefeated {
    int score{0};
};

Task<int> next_score(Events& events) {
    const EnemyDefeated event = co_await events.next<EnemyDefeated>();
    co_return event.score;
}

int main() {
    std::cout << "[TEST] Running EngineScriptAsyncTests...\n";

    // Test 1: Direct Task<T> execution
    auto t1 = async_add(10, 20);
    assert(t1.is_done());
    assert(t1.get_result() == 30);

    const Result<int, std::string> successful_result = Result<int, std::string>::success(42);
    assert(successful_result.isOk());
    assert(successful_result.value() == 42);
    const Result<int, std::string> failed_result = Result<int, std::string>::failure("not found");
    assert(failed_result.isError());
    assert(failed_result.error() == "not found");

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
    assert(t3.state() == TaskState::Cancelled);

    // Test 4: Destroying a suspended task invalidates the scheduler entry.
    bool discarded_flag = false;
    {
        auto discarded = delayed_action(scheduler, discarded_flag, 303);
        assert(scheduler.pending_tasks_count() == 1);
    }
    assert(scheduler.pending_tasks_count() == 0);
    scheduler.step(1.0f, 1.0f);
    assert(!discarded_flag);

    bool reset_flag = false;
    auto reset_task = delayed_action(scheduler, reset_flag, 404);
    scheduler.reset();
    assert(reset_task.state() == TaskState::Cancelled);
    scheduler.step(1.0f, 1.0f);
    assert(!reset_flag);

    // Type identity, rather than a string key, selects an event channel.
    TypedEventBus event_bus;
    Events events(&event_bus, 505);
    auto event_task = next_score(events);
    assert(!event_task.is_done());
    assert(events.emit(EnemyDefeated{77}));
    assert(event_task.is_done());
    assert(event_task.get_result() == 77);

    // Worker failures are structured Results and continuations resume through
    // the main scheduler rather than on the worker thread.
    Jobs jobs(&scheduler, 606);
    auto job = jobs.run([] { return 21 * 2; });
    for (int attempt = 0; attempt < 200 && !job.is_done(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        scheduler.step(0.0f, 0.0f);
    }
    assert(job.is_done());
    assert(job.get_result().isOk());
    assert(job.get_result().value() == 42);

    auto failed_job = jobs.run([]() -> int { throw std::runtime_error("worker failure"); });
    for (int attempt = 0; attempt < 200 && !failed_job.is_done(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        scheduler.step(0.0f, 0.0f);
    }
    assert(failed_job.is_done());
    assert(failed_job.get_result().isError());
    assert(failed_job.get_result().error() == "worker failure");

    bool scoped_flag = false;
    auto scoped_task = delayed_action(scheduler, scoped_flag, 707);
    {
        TaskScope scope(&scheduler, 707);
        assert(!scope.isCancelled());
    }
    scheduler.step(1.0f, 1.0f);
    assert(scoped_task.state() == TaskState::Cancelled);
    assert(!scoped_flag);

    std::vector<int> bounded{1, 2};
    assert(checkedIndex(bounded, 1) == 2);
    bool range_error = false;
    try { static_cast<void>(checkedIndex(bounded, 2)); }
    catch (const ScriptRuntimeError&) { range_error = true; }
    assert(range_error);

    std::cout << "[PASS] EngineScriptAsyncTests passed!\n";
    return 0;
}
