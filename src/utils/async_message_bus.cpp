#include "utils/async_message_bus.hpp"

namespace agent {

AsyncMessageBus::AsyncMessageBus(size_t num_threads) : pool_(num_threads) {}

void AsyncMessageBus::send(const std::string& from, const std::string& to,
                            const std::string& type, const std::string& payload) {
    // Store in base class
    MessageBus::send(from, to, type, payload);

    // Async dispatch to handlers
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
        Message msg{from, to, type, payload, std::time(nullptr)};
        for (const auto& handler : it->second) {
            pool_.submit([handler, msg]() { handler(msg); });
        }
    }
}

void AsyncMessageBus::subscribe(const std::string& type, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[type].push_back(std::move(handler));
}

void AsyncMessageBus::unsubscribe(const std::string& type) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(type);
}

} // namespace agent
