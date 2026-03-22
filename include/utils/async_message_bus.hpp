#pragma once
#include "utils/message_bus.hpp"
#include "utils/thread_pool.hpp"
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace agent {

using MessageHandler = std::function<void(const Message&)>;

class AsyncMessageBus : public MessageBus {
public:
    explicit AsyncMessageBus(size_t num_threads = 4);
    ~AsyncMessageBus() override = default;

    void send(const std::string& from, const std::string& to,
              const std::string& type, const std::string& payload) override;

    void subscribe(const std::string& type, MessageHandler handler);
    void unsubscribe(const std::string& type);

private:
    ThreadPool pool_;
    std::unordered_map<std::string, std::vector<MessageHandler>> handlers_;
    std::mutex mutex_;
};

} // namespace agent
