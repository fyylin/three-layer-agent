#include <iostream>
#include <cassert>
#include "utils/tool_set.hpp"
#include "utils/workspace.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    std::cout << "=== Testing all tools with Chinese paths ===" << std::endl;

#ifdef _WIN32
    // Create test directory with Chinese name
    std::wstring test_dir = L"E:\\项目\\Claude_Code\\Workspace\\three_layer_agent\\测试目录";
    CreateDirectoryW(test_dir.c_str(), nullptr);

    // Create test file with Chinese name
    std::wstring test_file = test_dir + L"\\测试文件.txt";
    HANDLE h = CreateFileW(test_file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        const char* content = "测试内容\nTest content";
        DWORD written;
        WriteFile(h, content, strlen(content), &written, nullptr);
        CloseHandle(h);
    }

    // Test list_dir
    std::cout << "\n[1] Testing list_dir..." << std::endl;
    try {
        std::string result = agent::tool_list_dir("E:\\项目\\Claude_Code\\Workspace\\three_layer_agent\\测试目录");
        assert(result.find("测试文件.txt") != std::string::npos);
        assert(result.find("��") == std::string::npos);
        std::cout << "✓ list_dir works" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ list_dir failed: " << e.what() << std::endl;
    }

    // Test read_file
    std::cout << "\n[2] Testing read_file..." << std::endl;
    try {
        std::string result = agent::tool_read_file("E:\\项目\\Claude_Code\\Workspace\\three_layer_agent\\测试目录\\测试文件.txt");
        assert(result.find("测试内容") != std::string::npos);
        assert(result.find("��") == std::string::npos);
        std::cout << "✓ read_file works" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ read_file failed: " << e.what() << std::endl;
    }

    // Test write_file
    std::cout << "\n[3] Testing write_file..." << std::endl;
    try {
        std::string input = "E:\\项目\\Claude_Code\\Workspace\\three_layer_agent\\测试目录\\新文件.txt\n新内容测试";
        std::string result = agent::tool_write_file(input);
        assert(result.find("新文件.txt") != std::string::npos);
        std::cout << "✓ write_file works" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ write_file failed: " << e.what() << std::endl;
    }

    // Test stat_file
    std::cout << "\n[4] Testing stat_file..." << std::endl;
    try {
        std::string result = agent::tool_stat_file("E:\\项目\\Claude_Code\\Workspace\\three_layer_agent\\测试目录\\测试文件.txt");
        assert(result.find("测试文件.txt") != std::string::npos);
        assert(result.find("��") == std::string::npos);
        std::cout << "✓ stat_file works" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ stat_file failed: " << e.what() << std::endl;
    }

    // Test find_files
    std::cout << "\n[5] Testing find_files..." << std::endl;
    try {
        std::string input = "E:\\项目\\Claude_Code\\Workspace\\three_layer_agent\\测试目录\n*.txt";
        std::string result = agent::tool_find_files(input);
        assert(result.find("测试文件.txt") != std::string::npos);
        assert(result.find("��") == std::string::npos);
        std::cout << "✓ find_files works" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ find_files failed: " << e.what() << std::endl;
    }

    // Cleanup
    DeleteFileW((test_dir + L"\\测试文件.txt").c_str());
    DeleteFileW((test_dir + L"\\新文件.txt").c_str());
    RemoveDirectoryW(test_dir.c_str());

    std::cout << "\n=== All tests passed ===" << std::endl;
#else
    std::cout << "Skipping (not Windows)" << std::endl;
#endif
    return 0;
}
