#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>

// Phase 3: Parallel
#include "utils/parallel_scheduler.hpp"

// Phase 4: Cache
#include "utils/result_cache.hpp"

// Phase 5: Prompts
#include "utils/prompt_optimizer.hpp"

// Phase 6: Multimodal
#include "utils/multimodal_handler.hpp"

// Phase 7: Distributed
#include "distributed/node_registry.hpp"
#include "distributed/task_dispatcher.hpp"
#include "distributed/result_collector.hpp"

using namespace agent;

void test_phase3_parallel() {
    std::cout << "[Phase 3] Parallel Scheduler..." << std::endl;
    ParallelScheduler sched;
    std::atomic<int> counter{0};
    std::atomic<bool> t1_done{false};
    sched.add_task("t1", [&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        counter++;
        t1_done = true;
    });
    sched.add_task("t2", [&]() {
        assert(t1_done.load()); // verify dependency
        counter++;
    }, {"t1"});
    sched.execute();
    assert(counter.load() == 2);
    std::cout << "  ✓ PASS" << std::endl;
}

void test_phase4_cache() {
    std::cout << "[Phase 4] Result Cache..." << std::endl;
    ResultCache cache(2);
    cache.put("k1", "v1");
    cache.put("k2", "v2");
    cache.put("k3", "v3"); // evicts k1
    std::string result;
    assert(!cache.get("k1", result)); // k1 evicted
    assert(cache.get("k2", result) && result == "v2");
    assert(cache.get("k3", result) && result == "v3");
    std::cout << "  ✓ PASS" << std::endl;
}

void test_phase5_prompts() {
    std::cout << "[Phase 5] Prompt Optimizer..." << std::endl;
    PromptOptimizer opt;
    opt.register_variant("test", "v1", "prompt v1");
    opt.register_variant("test", "v2", "prompt v2");
    opt.record_result("test", "v1", true, 100);
    opt.record_result("test", "v2", false, 200);
    auto best = opt.select_best("test");
    assert(best == "prompt v1"); // returns content, not id
    std::cout << "  ✓ PASS" << std::endl;
}

void test_phase6_multimodal() {
    std::cout << "[Phase 6] Multimodal Handler..." << std::endl;
    std::vector<unsigned char> data = {0x89, 0x50, 0x4E, 0x47};
    std::string b64 = MultimodalHandler::encode_base64(data);
    assert(b64 == "iVBORw==");

    MediaInput media{MediaType::Image, "test.png", "image/png", data};
    std::string formatted = MultimodalHandler::format_for_llm(media);
    assert(formatted.find("\"type\":\"image\"") != std::string::npos);
    assert(formatted.find("iVBORw==") != std::string::npos);
    std::cout << "  ✓ PASS" << std::endl;
}

void test_phase7_distributed() {
    std::cout << "[Phase 7] Distributed System..." << std::endl;
    auto registry = std::make_shared<NodeRegistry>();
    NodeInfo node{"node1", "localhost", 8080, 4};
    registry->register_node(node);
    registry->heartbeat("node1");
    auto alive = registry->get_alive_nodes();
    assert(alive.size() == 1);

    auto dispatcher = std::make_shared<TaskDispatcher>(registry);
    DistributedTask task{"t1", "test"};
    dispatcher->submit(task);
    assert(dispatcher->pending_count() == 1);

    ResultCollector collector;
    TaskResult result{"t1", "node1", true, "done"};
    collector.submit(result);
    TaskResult out;
    assert(collector.get("t1", out) && out.success);
    std::cout << "  ✓ PASS" << std::endl;
}

int main() {
    std::cout << "\n=== Phase 3-7 Integration Test ===" << std::endl;
    try {
        test_phase3_parallel();
        test_phase4_cache();
        test_phase5_prompts();
        test_phase6_multimodal();
        test_phase7_distributed();
        std::cout << "\n✅ All tests PASSED\n" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
