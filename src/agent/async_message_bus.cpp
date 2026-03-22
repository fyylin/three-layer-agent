#include "agent/async_message_bus.hpp"
#include <thread>
#include <future>

namespace agent {

std::future<AsyncMessageBus::Message> AsyncMessageBus::send_async(const Message& msg) {
    return std::async(std::launch::async, [this, msg]() {
        auto it = handlers_.find(msg.to);
        if (it == handlers_.end()) {
            return Message{msg.to, msg.from, "Handler not found", msg.task_id};
        }
        return it->second(msg);
    });
}

void AsyncMessageBus::register_handler(const std::string& agent_id, MessageHandler handler) {
    handlers_[agent_id] = std::move(handler);
}

void AsyncMessageBus::broadcast(const Message& msg) {
    for (auto& [id, handler] : handlers_) {
        std::thread([handler, msg]() { handler(msg); }).detach();
    }
}

} // namespace agent
