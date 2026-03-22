#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>

namespace agent {

struct NodeInfo {
    std::string id;
    std::string host;
    int port;
    int capacity = 4;
    int active_tasks = 0;
    std::chrono::steady_clock::time_point last_heartbeat;

    bool is_alive() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count() < 30;
    }

    int available_slots() const { return capacity - active_tasks; }
};

class NodeRegistry {
public:
    void register_node(const NodeInfo& node) {
        std::lock_guard<std::mutex> lk(mu_);
        nodes_[node.id] = node;
    }

    void heartbeat(const std::string& node_id) {
        std::lock_guard<std::mutex> lk(mu_);
        if (nodes_.count(node_id))
            nodes_[node_id].last_heartbeat = std::chrono::steady_clock::now();
    }

    std::vector<NodeInfo> get_alive_nodes() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<NodeInfo> result;
        for (auto& [id, node] : nodes_)
            if (node.is_alive()) result.push_back(node);
        return result;
    }

    NodeInfo* select_node() {
        std::lock_guard<std::mutex> lk(mu_);
        NodeInfo* best = nullptr;
        int max_slots = -1;
        for (auto& [id, node] : nodes_) {
            if (node.is_alive() && node.available_slots() > max_slots) {
                max_slots = node.available_slots();
                best = &node;
            }
        }
        return best;
    }

private:
    mutable std::mutex mu_;
    std::map<std::string, NodeInfo> nodes_;
};

} // namespace agent
