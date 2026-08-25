#pragma once

#include "AsyncScheduler.hpp"
#include "Result.hpp"
#include "Task.hpp"

#include <algorithm>
#include <coroutine>
#include <condition_variable>
#include <deque>
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

    ~Jobs() { stop_workers(); }

    [[nodiscard]] std::size_t worker_count() const {
        std::scoped_lock lock(m_queue_mutex);
        return m_workers.size();
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
        auto shared_work = std::make_shared<Work>(std::move(work));
        {
            std::scoped_lock lock(m_queue_mutex);
            start_workers_locked();
            if (m_stopping) return;
            m_queue.emplace_back([shared_work]() mutable { std::invoke(std::move(*shared_work)); });
        }
        m_queue_ready.notify_one();
    }

    void start_workers_locked() {
        if (!m_workers.empty() || m_stopping) return;
        const unsigned available = std::thread::hardware_concurrency();
        const std::size_t count = std::clamp<std::size_t>(available == 0 ? 2 : available, 1, 4);
        m_workers.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            m_workers.emplace_back([this] {
                while (true) {
                    std::function<void()> work;
                    {
                        std::unique_lock lock(m_queue_mutex);
                        m_queue_ready.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
                        if (m_stopping) return;
                        work = std::move(m_queue.front());
                        m_queue.pop_front();
                    }
                    work();
                }
            });
        }
    }

    void stop_workers() {
        {
            std::scoped_lock lock(m_queue_mutex);
            m_stopping = true;
            m_queue.clear();
        }
        m_queue_ready.notify_all();
        for (auto& worker : m_workers) {
            if (worker.joinable()) worker.join();
        }
    }

    AsyncScheduler* m_scheduler{nullptr};
    std::uint32_t m_scope_id{0};
    mutable std::mutex m_queue_mutex;
    std::condition_variable m_queue_ready;
    std::deque<std::function<void()>> m_queue;
    std::vector<std::thread> m_workers;
    bool m_stopping{false};
};

} // namespace EngineScript
