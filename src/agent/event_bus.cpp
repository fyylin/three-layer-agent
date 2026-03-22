#include "agent/event_bus.hpp"

namespace agent {

void EventBus::subscribe(EventType type, EventHandler handler) {
    std::lock_guard<std::mutex> lk(mu_);
    handlers_[type].push_back(std::move(handler));
}

void EventBus::publish(const Event& event) {
    std::vector<EventHandler> handlers_copy;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = handlers_.find(event.type);
        if (it != handlers_.end()) {
            handlers_copy = it->second;
        }
    }

    for (const auto& handler : handlers_copy) {
        try {
            handler(event);
        } catch (...) {
            // Ignore handler exceptions
        }
    }
}

void EventBus::unsubscribe_all() {
    std::lock_guard<std::mutex> lk(mu_);
    handlers_.clear();
}

} // namespace agent
