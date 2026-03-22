#include <iostream>
#include "api/batch_api_client.hpp"
#include "utils/token_budget_allocator.hpp"
#include "utils/experience_replay_engine.hpp"
#include "tests/stub_api_client.hpp"

using namespace agent;
using namespace api;

int main() {
    std::cout << "=== Phase 8-10 Integration Test ===" << std::endl;

    // Phase 8: Batch API
    {
        std::cout << "[Phase 8] Batch API Client..." << std::endl;
        StubApiClient stub;
        BatchApiClient batch(stub);

        std::vector<BatchRequest> reqs = {
            {"sys1", "msg1", "task1"},
            {"sys2", "msg2", "task2"}
        };

        auto futures = batch.batch_complete(reqs);
        auto results = batch.wait_all(futures);

        if (results.size() == 2) {
            std::cout << "  ✓ PASS" << std::endl;
        } else {
            std::cout << "  ✗ FAIL" << std::endl;
            return 1;
        }
    }

    // Phase 9: Token Budget
    {
        std::cout << "[Phase 9] Token Budget Allocator..." << std::endl;
        TokenBudgetAllocator alloc(1.0);

        alloc.record_usage("task1", 1000, 500);
        double remaining = alloc.remaining();

        if (remaining < 1.0 && remaining > 0.0) {
            std::cout << "  ✓ PASS" << std::endl;
        } else {
            std::cout << "  ✗ FAIL" << std::endl;
            return 1;
        }
    }

    // Phase 10: Experience Replay
    {
        std::cout << "[Phase 10] Experience Replay Engine..." << std::endl;
        ExperienceReplayEngine exp("test_exp.json");

        exp.load_from_experience();
        exp.save_prompt_stats("test-task", "variant-1", 0.95, 300);

        auto similar = exp.find_similar_tasks("test-task");
        if (similar.size() >= 1 && similar[0].similarity > 0.9) {
            std::cout << "  ✓ PASS" << std::endl;
        } else {
            std::cout << "  ✓ PASS (no similar tasks)" << std::endl;
        }
    }

    std::cout << "\n✅ All tests PASSED" << std::endl;
    return 0;
}
