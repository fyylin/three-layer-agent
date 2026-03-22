#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "utils/utf8_fstream.hpp"
#include "utils/workspace.hpp"

using namespace agent;

namespace {

std::string test_root() {
    return WorkspaceManager::join(".", u8"测试工作区");
}

std::string read_all_utf8(const std::string& path) {
    utf8_ifstream f(path);
    assert(f.is_open());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

void test_chinese_path() {
    std::cout << "[Test] Chinese path support..." << std::endl;

    std::string test_dir = WorkspaceManager::join(test_root(), u8"测试目录");
    WorkspaceManager::mkdirs(test_dir);

    std::string test_file = WorkspaceManager::join(test_dir, u8"中文文件.txt");
    {
        utf8_ofstream f(test_file);
        assert(f.is_open() && "Failed to create Chinese filename");
        f << u8"测试中文内容\n";
        f << "Test Chinese content\n";
    }

    std::string content = read_all_utf8(test_file);
    assert(content.find(u8"测试中文内容") != std::string::npos &&
           "Chinese content corrupted");
}

void test_workspace_chinese() {
    std::cout << "[Test] Workspace with Chinese..." << std::endl;

    std::string root = WorkspaceManager::join(test_root(), u8"工作区");
    WorkspacePaths wp = WorkspaceManager::init(root, u8"运行_001");

    WorkspaceManager::write_state(wp, u8"测试代理", "{\"status\":\"running\"}");

    auto state = nlohmann::json::parse(read_all_utf8(wp.state_json));
    assert(state.contains(u8"测试代理") && "Failed to persist UTF-8 agent id");
}

int main() {
    std::cout << "\n=== Chinese Support Test ===" << std::endl;
    test_chinese_path();
    test_workspace_chinese();
    std::cout << "All tests passed." << std::endl;
    return 0;
}
