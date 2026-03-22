#include "agent/dependency_analyzer.hpp"
#include <unordered_set>
#include <queue>

namespace agent {

DependencyAnalyzer::TaskGraph DependencyAnalyzer::analyze(
        const std::vector<AtomicTask>& tasks) const {

    TaskGraph graph;
    graph.tasks = tasks;

    // 分析任务间依赖
    for (size_t i = 0; i < tasks.size(); ++i) {
        for (size_t j = i + 1; j < tasks.size(); ++j) {
            if (has_dependency(tasks[i], tasks[j])) {
                graph.dependencies.push_back({i, j});
            }
        }
    }

    return graph;
}

std::vector<std::vector<int>> DependencyAnalyzer::get_parallel_batches(
        const TaskGraph& graph) const {

    std::vector<std::vector<int>> batches;
    std::unordered_set<int> completed;
    std::vector<int> in_degree(graph.tasks.size(), 0);

    // 计算入度
    for (const auto& [from, to] : graph.dependencies) {
        ++in_degree[to];
    }

    // 拓扑排序分批
    while (completed.size() < graph.tasks.size()) {
        std::vector<int> current_batch;

        // 找出所有入度为 0 的任务
        for (size_t i = 0; i < graph.tasks.size(); ++i) {
            if (completed.count(i) == 0 && in_degree[i] == 0) {
                current_batch.push_back(i);
            }
        }

        if (current_batch.empty())
            break; // 有环或错误

        batches.push_back(current_batch);

        // 更新入度
        for (int idx : current_batch) {
            completed.insert(idx);
            for (const auto& [from, to] : graph.dependencies) {
                if (from == idx) {
                    --in_degree[to];
                }
            }
        }
    }

    return batches;
}

bool DependencyAnalyzer::has_dependency(
        const AtomicTask& a, const AtomicTask& b) const {

    // 写同一资源必须串行
    if (writes_to_same_resource(a, b))
        return true;

    // b 依赖 a 的输出
    if (depends_on_output(a, b))
        return true;

    return false;
}

bool DependencyAnalyzer::writes_to_same_resource(
        const AtomicTask& a, const AtomicTask& b) const {

    // 检查是否写同一文件
    if ((a.tool == "write_file" || a.tool == "edit_file") &&
        (b.tool == "write_file" || b.tool == "edit_file")) {
        return a.input == b.input;
    }

    return false;
}

bool DependencyAnalyzer::depends_on_output(
        const AtomicTask& a, const AtomicTask& b) const {

    // 简单启发式：如果 b 的输入包含 a 的输出路径
    if (a.tool == "write_file" && !a.input.empty()) {
        if (b.input.find(a.input) != std::string::npos)
            return true;
    }

    // 如果 b 的描述提到 a 的结果
    if (b.description.find("result") != std::string::npos ||
        b.description.find("output") != std::string::npos ||
        b.description.find("上一步") != std::string::npos) {
        return true;
    }

    return false;
}

} // namespace agent
