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
        m_subscribers[type_idx].push_back(std::make_shared<Subscriber>(Subscriber{id, [callback = std::move(callback)](const IEvent& event) {
            callback(static_cast<const T&>(event));
        }, true}));
        return id;
    }

    bool unsubscribe(SubscriptionId id) {
        for (auto& [type, subscribers] : m_subscribers) {
            (void)type;
            const auto previous_size = subscribers.size();
            std::erase_if(subscribers, [id](const std::shared_ptr<Subscriber>& subscriber) {
                if (subscriber->id != id) return false;
                subscriber->active = false;
                return true;
            });
            if (subscribers.size() != previous_size) return true;
        }
        return false;
    }

    // Subscribers added during a dispatch participate in the next dispatch.
    // Unsubscription takes effect immediately, including during nested dispatch.
    template<typename T>
    void publish(const T& event) {
        dispatch(std::type_index(typeid(T)), event);
    }

    // Queue an event to be dispatched later (deferred at frame boundary)
    template<typename T>
    void queue(const T& event) {
        m_event_queue.push_back(std::make_unique<T>(event));
    }

    // Dispatch all queued events
    void dispatch_queued() {
        std::vector<std::unique_ptr<IEvent>> events;
        events.swap(m_event_queue);
        const auto generation = m_clear_generation;
        for (const auto& event : events) {
            if (generation != m_clear_generation) break;
            if (event) dispatch(event->get_type_index(), *event);
        }
    }

    // Clear all subscribers and queued events
    void clear() {
        ++m_clear_generation;
        for (auto& [type, subscribers] : m_subscribers) {
            for (auto& subscriber : subscribers) subscriber->active = false;
        }
        m_subscribers.clear();
        m_event_queue.clear();
    }

private:
    using InternalCallback = std::function<void(const IEvent&)>;
    struct Subscriber {
        SubscriptionId id{0};
        InternalCallback callback;
        bool active{true};
    };
    void dispatch(std::type_index type, const IEvent& event) {
        const auto found = m_subscribers.find(type);
        if (found == m_subscribers.end()) return;
        const auto subscribers = found->second;
        for (const auto& subscriber : subscribers) {
            if (subscriber->active) subscriber->callback(event);
        }
    }
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<Subscriber>>> m_subscribers;
    std::vector<std::unique_ptr<IEvent>> m_event_queue;
    SubscriptionId m_next_subscription_id{1};
    std::uint64_t m_clear_generation{0};
};

} // namespace Engine
