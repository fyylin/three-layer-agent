// =============================================================================
// src/utils/message_bus.cpp
// =============================================================================
#include "utils/message_bus.hpp"
#include <algorithm>

namespace agent {

void MessageBus::send(AgentMessage msg) {
    msg.timestamp_ms = now_ms();
    ++total_sent_;

    std::vector<std::function<void(const AgentMessage&)>> cbs;

    {
        std::lock_guard<std::mutex> lk(mu_);

        // Deliver to inbox (point-to-point or broadcast)
        if (msg.to_id == "*") {
            // Broadcast: deliver to every registered inbox
            for (auto& [aid, _] : inboxes_)
                inboxes_[aid].push(msg);
        } else {
            inboxes_[msg.to_id].push(msg);
        }

        // Collect subscribers for this recipient (outside lock: invoke below)
        auto collect = [&](const std::string& id) {
            auto it = subscribers_.find(id);
            if (it != subscribers_.end())
                for (auto& cb : it->second) cbs.push_back(cb);
        };
        collect(msg.to_id);
        if (msg.to_id != "*") collect("*");  // wildcard subscribers
    }

    cv_.notify_all();

    // Invoke subscriber callbacks outside the lock
    for (auto& cb : cbs) cb(msg);
}

void MessageBus::send(const std::string& from,
                       const std::string& to,
                       const std::string& type,
                       const std::string& payload) {
    send(AgentMessage{from, to, type, payload, 0});
}

void MessageBus::broadcast(const std::string& from,
                             const std::string& type,
                             const std::string& payload) {
    send(from, "*", type, payload);
}

std::optional<AgentMessage> MessageBus::try_receive(const std::string& agent_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = inboxes_.find(agent_id);
    if (it == inboxes_.end() || it->second.empty())
        return std::nullopt;
    auto msg = std::move(it->second.front());
    it->second.pop();
    return msg;
}

std::optional<AgentMessage> MessageBus::receive(const std::string& agent_id,
                                                  int timeout_ms) {
    if (timeout_ms == 0) return try_receive(agent_id);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    std::unique_lock<std::mutex> lk(mu_);
    bool ok = cv_.wait_until(lk, deadline, [&] {
        auto it = inboxes_.find(agent_id);
        return it != inboxes_.end() && !it->second.empty();
    });
    if (!ok) return std::nullopt;

    auto& q = inboxes_[agent_id];
    auto msg = std::move(q.front());
    q.pop();
    return msg;
}

void MessageBus::subscribe(const std::string& agent_id,
                            std::function<void(const AgentMessage&)> cb) {
    std::lock_guard<std::mutex> lk(mu_);
    subscribers_[agent_id].push_back(std::move(cb));
    // Ensure inbox exists
    inboxes_[agent_id];
}

void MessageBus::unsubscribe(const std::string& agent_id) {
    std::lock_guard<std::mutex> lk(mu_);
    subscribers_.erase(agent_id);
}

size_t MessageBus::pending_count(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = inboxes_.find(agent_id);
    return (it != inboxes_.end()) ? it->second.size() : 0;
}

} // namespace agent
