#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>

namespace Engine {

class ThreadPool {
public:
    explicit ThreadPool(size_t thread_count = 0);
    ~ThreadPool();

    // Re-initialize thread count dynamically (1 to hardware_concurrency)
    void set_thread_count(size_t thread_count);
    size_t get_thread_count() const { return m_workers.size(); }

    // Enqueue a single void/return task
    template <typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            if (m_stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            m_tasks.emplace([task]() { (*task)(); });
        }
        m_cv.notify_one();
        return res;
    }

    // Parallel loop helper: divides range [start, end) into chunks across worker threads
    void parallel_for(size_t start, size_t end, const std::function<void(size_t index)>& job);

private:
    void stop_workers();
    void create_workers(size_t count);

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;

    std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop{false};
};

} // namespace Engine
