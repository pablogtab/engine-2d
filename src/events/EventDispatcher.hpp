#pragma once

#include "Event.hpp"
#include <unordered_map>
#include <vector>
#include <functional>
#include <typeindex>
#include <memory>
#include <cstdint>
#include <algorithm>

namespace Engine {

class EventDispatcher {
public:
    using SubscriptionId = std::uint64_t;

    template<typename T>
    using EventCallback = std::function<void(const T&)>;

    EventDispatcher() = default;
    ~EventDispatcher() = default;

    // Subscribe to a specific Event type T
    template<typename T>
    SubscriptionId subscribe(EventCallback<T> callback) {
        std::type_index type_idx = std::type_index(typeid(T));
        const SubscriptionId id = m_next_subscription_id++;
        m_subscribers[type_idx].push_back({id, [callback = std::move(callback)](const IEvent& event) {
            callback(static_cast<const T&>(event));
        }});
        return id;
    }

    bool unsubscribe(SubscriptionId id) {
        for (auto& [type, subscribers] : m_subscribers) {
            (void)type;
            const auto previous_size = subscribers.size();
            std::erase_if(subscribers, [id](const Subscriber& subscriber) {
                return subscriber.id == id;
            });
            if (subscribers.size() != previous_size) return true;
        }
        return false;
    }

    // Publish an event immediately to all subscribers (synchronous, 0 heap allocations)
    template<typename T>
    void publish(const T& event) {
        std::type_index type_idx = std::type_index(typeid(T));
        auto it = m_subscribers.find(type_idx);
        if (it != m_subscribers.end()) {
            for (const auto& subscriber : it->second) {
                subscriber.callback(event);
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
                    for (const auto& subscriber : it->second) {
                        subscriber.callback(*event_ptr);
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
    struct Subscriber {
        SubscriptionId id{0};
        InternalCallback callback;
    };
    std::unordered_map<std::type_index, std::vector<Subscriber>> m_subscribers;
    std::vector<std::unique_ptr<IEvent>> m_event_queue;
    SubscriptionId m_next_subscription_id{1};
};

} // namespace Engine
