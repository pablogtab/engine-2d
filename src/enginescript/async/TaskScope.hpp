#pragma once

#include "AsyncScheduler.hpp"

#include <cstdint>

namespace EngineScript {

class TaskScope {
public:
    TaskScope() = default;
    TaskScope(AsyncScheduler* scheduler, std::uint32_t scope_id)
        : m_scheduler(scheduler), m_scope_id(scope_id) {}

    TaskScope(const TaskScope&) = delete;
    TaskScope& operator=(const TaskScope&) = delete;
    TaskScope(TaskScope&&) = default;
    TaskScope& operator=(TaskScope&&) = default;

    ~TaskScope() { cancel(); }

    void cancel() {
        if (m_cancelled) return;
        m_cancelled = true;
        if (m_scheduler) m_scheduler->cancel_scope(m_scope_id);
    }

    [[nodiscard]] bool isCancelled() const { return m_cancelled; }
    [[nodiscard]] std::uint32_t id() const { return m_scope_id; }

private:
    AsyncScheduler* m_scheduler{nullptr};
    std::uint32_t m_scope_id{0};
    bool m_cancelled{false};
};

} // namespace EngineScript
