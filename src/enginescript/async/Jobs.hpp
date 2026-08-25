#pragma once

#include "AsyncScheduler.hpp"
#include "Result.hpp"
#include "Task.hpp"

#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace EngineScript {

class Jobs {
public:
    Jobs() = default;
    Jobs(AsyncScheduler* scheduler, std::uint32_t scope_id)
        : m_scheduler(scheduler), m_scope_id(scope_id) {}

    Jobs(const Jobs&) = delete;
    Jobs& operator=(const Jobs&) = delete;

    ~Jobs() {
        for (auto& worker : m_workers) {
            if (worker.joinable()) worker.join();
        }
    }

    template <typename Work>
    auto run(Work work) -> Task<Result<std::invoke_result_t<Work>, std::string>> {
        using Value = std::invoke_result_t<Work>;
        static_assert(!std::is_void_v<Value>, "jobs.run requires a value-producing closure");
        co_return co_await WorkerAwaiter<Work, Value>{this, std::move(work)};
    }

private:
    template <typename Work, typename Value>
    struct WorkerAwaiter {
        struct State {
            Result<Value, std::string> result = Result<Value, std::string>::failure("job was cancelled");
            std::coroutine_handle<> continuation;
            std::weak_ptr<detail::CoroutineControlBlock> control;
        };

        Jobs* jobs{nullptr};
        Work work;
        std::shared_ptr<State> state = std::make_shared<State>();

        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> continuation) {
            static_assert(std::is_base_of_v<detail::TaskPromiseBase, Promise>);
            state->continuation = continuation;
            state->control = continuation.promise().control;
            jobs->launch([jobs = jobs, work = std::move(work), state = state]() mutable {
                try {
                    state->result = Result<Value, std::string>::success(std::invoke(std::move(work)));
                } catch (const std::exception& error) {
                    state->result = Result<Value, std::string>::failure(error.what());
                } catch (...) {
                    state->result = Result<Value, std::string>::failure("worker failed with an unknown exception");
                }
                if (!jobs->m_scheduler) return;
                jobs->m_scheduler->post_next_frame([state]() {
                    const auto control = state->control.lock();
                    if (!control || control->cancelled) return;
                    if (state->continuation && !state->continuation.done()) state->continuation.resume();
                }, jobs->m_scope_id);
            });
        }

        Result<Value, std::string> await_resume() { return std::move(state->result); }
    };

    template <typename Work>
    void launch(Work work) {
        std::scoped_lock lock(m_workers_mutex);
        m_workers.emplace_back(std::move(work));
    }

    AsyncScheduler* m_scheduler{nullptr};
    std::uint32_t m_scope_id{0};
    std::mutex m_workers_mutex;
    std::vector<std::thread> m_workers;
};

} // namespace EngineScript
