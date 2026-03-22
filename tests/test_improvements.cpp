#include <iostream>
#include <cassert>
#include "agent/task_router.hpp"
#include "agent/task_context.hpp"
#include "agent/preflight_checker.hpp"
#include "agent/dependency_analyzer.hpp"
#include "agent/experience_db.hpp"

using namespace agent;

void test_task_router() {
    std::cout << "=== Testing TaskRouter ===" << std::endl;

    TaskRouter router;
    std::vector<std::string> tools = {"read_file", "write_file", "list_dir"};

    // 测试 FastPath
    auto decision1 = router.analyze("用 read_file 读取 test.txt", tools);
    assert(decision1 == TaskRouter::RouteDecision::FastPath);
    std::cout << "✓ FastPath: 单工具调用识别正确" << std::endl;

    // 测试 ManagerPath
    auto decision2 = router.analyze("列出目录并统计文件数量", tools);
    assert(decision2 == TaskRouter::RouteDecision::ManagerPath);
    std::cout << "✓ ManagerPath: 简单任务识别正确" << std::endl;

    // 测试 FullPath
    auto decision3 = router.analyze("重构整个认证模块，将 session 改为 JWT", tools);
    assert(decision3 == TaskRouter::RouteDecision::FullPath);
    std::cout << "✓ FullPath: 复杂任务识别正确" << std::endl;
}

void test_task_context() {
    std::cout << "\n=== Testing TaskContext ===" << std::endl;

    TaskContext ctx;
    ctx.original_goal = "实现用户登录功能";
    ctx.current_subtask = "验证用户密码";
    ctx.add_completed("创建用户表");
    ctx.add_completed("实现密码加密");
    ctx.set_state("db_connected", "true");

    std::string prompt = ctx.to_prompt_context();
    assert(prompt.find("实现用户登录功能") != std::string::npos);
    assert(prompt.find("创建用户表") != std::string::npos);
    assert(ctx.get_state("db_connected") == "true");

    std::cout << "✓ TaskContext: 上下文传递正常" << std::endl;
}

void test_preflight_checker() {
    std::cout << "\n=== Testing PreflightChecker ===" << std::endl;

    PreflightChecker checker;
    TaskContext ctx;
    ctx.original_goal = "读取配置文件";

    AtomicTask task;
    task.tool = "read_file";
    task.input = "/tmp/config.json";
    task.description = "读取配置文件内容";
    task.context = std::make_shared<TaskContext>(ctx);

    auto result = checker.check(task, ctx);
    assert(result.should_proceed);
    std::cout << "✓ PreflightChecker: 正常任务通过检查" << std::endl;

    // 测试路径无效
    AtomicTask bad_task;
    bad_task.tool = "read_file";
    bad_task.input = "<HOME>/config.json";
    bad_task.description = "读取配置";
    bad_task.context = std::make_shared<TaskContext>(ctx);

    auto bad_result = checker.check(bad_task, ctx);
    assert(!bad_result.should_proceed);
    std::cout << "✓ PreflightChecker: 占位符路径被拦截" << std::endl;
}

void test_dependency_analyzer() {
    std::cout << "\n=== Testing DependencyAnalyzer ===" << std::endl;

    DependencyAnalyzer analyzer;

    std::vector<AtomicTask> tasks(3);
    tasks[0].id = "t1";
    tasks[0].tool = "read_file";
    tasks[0].input = "input.txt";

    tasks[1].id = "t2";
    tasks[1].tool = "write_file";
    tasks[1].input = "output.txt";

    tasks[2].id = "t3";
    tasks[2].tool = "read_file";
    tasks[2].input = "output.txt";

    auto graph = analyzer.analyze(tasks);
    assert(graph.tasks.size() == 3);

    auto batches = analyzer.get_parallel_batches(graph);
    assert(batches.size() >= 1);
    std::cout << "✓ DependencyAnalyzer: 依赖分析正常，生成 "
              << batches.size() << " 个批次" << std::endl;
}

void test_experience_db() {
    std::cout << "\n=== Testing ExperienceDB ===" << std::endl;

    ExperienceDB db;

    db.record_failure("读取 /tmp/test.txt", "文件不存在");
    db.record_success("读取 /tmp/test.txt", "先检查文件是否存在");
    db.record_success("读取 /tmp/test.txt", "先检查文件是否存在");

    auto solution = db.query_solution("读取 /tmp/test.txt");
    assert(solution.has_value());
    assert(solution->find("检查") != std::string::npos);

    std::cout << "✓ ExperienceDB: 经验学习正常" << std::endl;
    std::cout << "  解决方案: " << *solution << std::endl;
}

int main() {
    try {
        test_task_router();
        test_task_context();
        test_preflight_checker();
        test_dependency_analyzer();
        test_experience_db();

        std::cout << "\n=== 所有测试通过 ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }
}
