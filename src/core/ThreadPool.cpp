#include "ThreadPool.hpp"
#include "Log.hpp"
#include <algorithm>

namespace Engine {

ThreadPool::ThreadPool(size_t thread_count) {
    if (thread_count == 0) {
        thread_count = std::max(1u, std::thread::hardware_concurrency());
    }
    create_workers(thread_count);
}

ThreadPool::~ThreadPool() {
    stop_workers();
}

void ThreadPool::create_workers(size_t count) {
    m_stop = false;
    m_workers.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        m_workers.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->m_queue_mutex);
                    this->m_cv.wait(lock, [this]() {
                        return this->m_stop || !this->m_tasks.empty();
                    });

                    if (this->m_stop && this->m_tasks.empty()) {
                        return;
                    }

                    task = std::move(this->m_tasks.front());
                    this->m_tasks.pop();
                }

                task();
            }
        });
    }

    ENGINE_LOG_INFO("ThreadPool initialized with {} active worker threads.", count);
}

void ThreadPool::stop_workers() {
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_stop = true;
    }
    m_cv.notify_all();

    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    // Clear remaining tasks
    std::queue<std::function<void()>> empty;
    std::swap(m_tasks, empty);
}

void ThreadPool::set_thread_count(size_t thread_count) {
    if (thread_count == 0) {
        thread_count = std::max(1u, std::thread::hardware_concurrency());
    }
    if (thread_count == m_workers.size()) return;

    ENGINE_LOG_INFO("Re-configuring ThreadPool from {} to {} cores.", m_workers.size(), thread_count);
    stop_workers();
    create_workers(thread_count);
}

void ThreadPool::parallel_for(size_t start, size_t end, const std::function<void(size_t index)>& job) {
    if (start >= end) return;

    size_t total = end - start;
    size_t num_threads = m_workers.size();

    // If small workload or single thread, run inline on calling thread
    if (num_threads <= 1 || total < 256) {
        for (size_t i = start; i < end; ++i) {
            job(i);
        }
        return;
    }

    size_t chunk_size = (total + num_threads - 1) / num_threads;
    std::vector<std::future<void>> futures;
    futures.reserve(num_threads - 1);
    std::exception_ptr failure;

    try {
        for (size_t t = 0; t < num_threads - 1; ++t) {
            size_t chunk_start = start + t * chunk_size;
            size_t chunk_end = std::min(chunk_start + chunk_size, end);

            if (chunk_start < chunk_end) {
                futures.push_back(enqueue([chunk_start, chunk_end, &job]() {
                    for (size_t i = chunk_start; i < chunk_end; ++i) {
                        job(i);
                    }
                }));
            }
        }

        // Process last chunk on current thread (main thread)
        size_t main_start = start + (num_threads - 1) * chunk_size;
        size_t main_end = std::min(main_start + chunk_size, end);
        for (size_t i = main_start; i < main_end; ++i) {
            job(i);
        }
    } catch (...) {
        failure = std::current_exception();
    }

    // All exits join every submitted chunk before releasing the caller's captured state.
    for (auto& fut : futures) {
        try { fut.get(); } catch (...) { if (!failure) failure = std::current_exception(); }
    }
    if (failure) std::rethrow_exception(failure);
}

} // namespace Engine
