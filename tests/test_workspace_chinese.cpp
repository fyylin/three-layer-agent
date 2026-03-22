#include <iostream>
#include <fstream>
#include "utils/workspace.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    std::cout << "Testing workspace operations with Chinese paths..." << std::endl;

    // Test std::ofstream with Chinese path
    std::string test_path = "E:\\项目\\Claude_Code\\Workspace\\three_layer_agent\\测试.txt";

    std::cout << "\n[1] Testing std::ofstream (ANSI - will fail)..." << std::endl;
    {
        std::ofstream f(test_path);
        if (f.is_open()) {
            f << "test content";
            f.close();
            std::cout << "✗ Unexpectedly succeeded (should fail with Chinese path)" << std::endl;
        } else {
            std::cout << "✓ Failed as expected (std::ofstream uses ANSI on Windows)" << std::endl;
        }
    }

    std::cout << "\n[2] Testing _wfopen (Unicode - will work)..." << std::endl;
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, test_path.c_str(), -1, nullptr, 0);
        std::wstring wpath(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, test_path.c_str(), -1, &wpath[0], wlen);

        FILE* f = _wfopen(wpath.c_str(), L"w");
        if (f) {
            fputs("test content", f);
            fclose(f);
            std::cout << "✓ Success with _wfopen" << std::endl;
            DeleteFileW(wpath.c_str());
        } else {
            std::cout << "✗ Failed" << std::endl;
        }
    }

    std::cout << "\n=== Conclusion: Need to replace std::fstream with _wfopen ===" << std::endl;
#else
    std::cout << "Skipping (not Windows)" << std::endl;
#endif
    return 0;
}
