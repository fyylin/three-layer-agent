#pragma once
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace agent {

enum class EventType {
    TaskStarted,
    TaskCompleted,
    TaskFailed,
    AgentStuck,
    HighFailRate,
    CircuitBreakerOpen
};

struct Event {
    EventType type;
    std::string agent_id;
    std::string task_id;
    std::string payload;
    std::chrono::steady_clock::time_point timestamp;

    Event(EventType t, std::string aid, std::string tid, std::string p = "")
        : type(t), agent_id(std::move(aid)), task_id(std::move(tid)),
          payload(std::move(p)), timestamp(std::chrono::steady_clock::now()) {}
};

class EventBus {
public:
    using EventHandler = std::function<void(const Event&)>;

    void subscribe(EventType type, EventHandler handler);
    void publish(const Event& event);
    void unsubscribe_all();

private:
    std::mutex mu_;
    std::unordered_map<EventType, std::vector<EventHandler>> handlers_;
};

} // namespace agent
