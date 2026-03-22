#pragma once
#include <string>
#include <functional>
#include <future>
#include <unordered_map>

namespace agent {

class AsyncMessageBus {
public:
    struct Message {
        std::string from;
        std::string to;
        std::string content;
        std::string task_id;
    };

    using MessageHandler = std::function<Message(const Message&)>;

    std::future<Message> send_async(const Message& msg);
    void register_handler(const std::string& agent_id, MessageHandler handler);
    void broadcast(const Message& msg);

private:
    std::unordered_map<std::string, MessageHandler> handlers_;
};

} // namespace agent
