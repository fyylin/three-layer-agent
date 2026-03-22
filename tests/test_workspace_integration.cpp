#include <iostream>
#include "utils/workspace.hpp"
#include "utils/memory_store.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    std::cout << "=== Testing workspace with Chinese paths ===" << std::endl;

    std::string test_root = "E:\\项目\\Claude_Code\\Workspace\\three_layer_agent\\测试工作区";
    std::string run_id = "test_run_中文";

    std::cout << "\n[1] Initializing workspace..." << std::endl;
    auto wp = agent::WorkspaceManager::init(test_root, run_id);
    std::cout << "✓ Workspace initialized" << std::endl;

    std::cout << "\n[2] Writing state..." << std::endl;
    agent::WorkspaceManager::write_state(wp, "test_agent", "test_state", "{\"status\":\"ok\"}");
    std::cout << "✓ State written" << std::endl;

    std::cout << "\n[3] Logging activity..." << std::endl;
    agent::WorkspaceManager::log_activity(wp, "test_agent", "测试消息", "{\"type\":\"test\"}");
    std::cout << "✓ Activity logged" << std::endl;

    std::cout << "\n[4] Testing memory store..." << std::endl;
    agent::MemoryStore mem(wp);
    mem.add_memory("测试记忆", "这是一个中文测试");
    auto memories = mem.get_all_memories();
    if (!memories.empty()) {
        std::cout << "✓ Memory stored and retrieved: " << memories.size() << " entries" << std::endl;
    } else {
        std::cout << "✗ Memory retrieval failed" << std::endl;
        return 1;
    }

    std::cout << "\n[5] Cleaning up..." << std::endl;
    std::wstring wpath;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, test_root.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        wpath.resize(wlen - 1);
        MultiByteToWideChar(CP_UTF8, 0, test_root.c_str(), -1, &wpath[0], wlen);
        std::wstring cmd = L"cmd /c rmdir /s /q \"" + wpath + L"\" 2>nul";
        _wsystem(cmd.c_str());
    }
    std::cout << "✓ Cleanup complete" << std::endl;

    std::cout << "\n=== All workspace tests passed ===" << std::endl;
#else
    std::cout << "Skipping (not Windows)" << std::endl;
#endif
    return 0;
}
