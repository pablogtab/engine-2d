#pragma once

#include "Event.hpp"
#include <unordered_map>
#include <vector>
#include <functional>
#include <typeindex>
#include <memory>

namespace Engine {

class EventDispatcher {
public:
    template<typename T>
    using EventCallback = std::function<void(const T&)>;

    EventDispatcher() = default;
    ~EventDispatcher() = default;

    // Subscribe to a specific Event type T
    template<typename T>
    void subscribe(EventCallback<T> callback) {
        std::type_index type_idx = std::type_index(typeid(T));
        m_subscribers[type_idx].push_back([callback](const IEvent& event) {
            callback(static_cast<const T&>(event));
        });
    }

    // Publish an event immediately to all subscribers (synchronous, 0 heap allocations)
    template<typename T>
    void publish(const T& event) {
        std::type_index type_idx = std::type_index(typeid(T));
        auto it = m_subscribers.find(type_idx);
        if (it != m_subscribers.end()) {
            for (const auto& listener : it->second) {
                listener(event);
            }
        }
    }

    // Queue an event to be dispatched later (deferred at frame boundary)
    template<typename T>
    void queue(const T& event) {
        m_event_queue.push_back(std::make_unique<T>(event));
    }

    // Dispatch all queued events
    void dispatch_queued() {
        for (auto& event_ptr : m_event_queue) {
            if (event_ptr) {
                std::type_index type_idx = event_ptr->get_type_index();
                auto it = m_subscribers.find(type_idx);
                if (it != m_subscribers.end()) {
                    for (const auto& listener : it->second) {
                        listener(*event_ptr);
                    }
                }
            }
        }
        m_event_queue.clear();
    }

    // Clear all subscribers and queued events
    void clear() {
        m_subscribers.clear();
        m_event_queue.clear();
    }

private:
    using InternalCallback = std::function<void(const IEvent&)>;
    std::unordered_map<std::type_index, std::vector<InternalCallback>> m_subscribers;
    std::vector<std::unique_ptr<IEvent>> m_event_queue;
};

} // namespace Engine
