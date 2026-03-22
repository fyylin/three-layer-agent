#include <iostream>
#include <cassert>
#include "utils/tool_set.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    std::cout << "Testing get_current_dir with Chinese path..." << std::endl;

#ifdef _WIN32
    // Change to Chinese path
    std::wstring chinese_path = L"E:\\项目\\Claude_Code\\Workspace\\three_layer_agent";
    SetCurrentDirectoryW(chinese_path.c_str());
#endif

    // Call the tool
    std::string result = agent::tool_get_current_dir("");

    std::cout << "Result: " << result << std::endl;

    // Verify no mojibake (乱码)
    assert(result.find("��") == std::string::npos && "Contains mojibake!");
    assert(result.find("项目") != std::string::npos && "Chinese characters lost!");

    std::cout << "✓ PASS: Chinese path correctly returned" << std::endl;
    return 0;
}
