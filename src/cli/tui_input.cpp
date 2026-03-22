#include "tui_input.hpp"
#include <iostream>
#include <windows.h>
#include <thread>
#include <atomic>

namespace agent::cli {

std::string TUIInput::read_line(const char* prompt) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << prompt << std::flush;

    WCHAR wbuf[4096] = {};
    DWORD nr = 0;
    std::wstring current_input;
    std::atomic<bool> reading{true};

    // Background thread to show hints
    std::thread hint_thread([&]() {
        while (reading) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!reading) break;

            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hOut, &csbi);

            // Get current input from console
            COORD start = csbi.dwCursorPosition;
            start.X = (SHORT)strlen(prompt);

            if (current_input.length() > 0 && current_input[0] == L'/' && completion_cb_) {
                std::string utf8_input;
                int needed = WideCharToMultiByte(CP_UTF8, 0, current_input.c_str(), -1, nullptr, 0, nullptr, nullptr);
                utf8_input.resize(needed);
                WideCharToMultiByte(CP_UTF8, 0, current_input.c_str(), -1, &utf8_input[0], needed, nullptr, nullptr);

                auto hints = completion_cb_(utf8_input);
                if (!hints.empty()) {
                    COORD hint_pos = csbi.dwCursorPosition;
                    hint_pos.Y++;
                    SetConsoleCursorPosition(hOut, hint_pos);

                    std::cout << "\033[2K\033[90m";
                    for (size_t i = 0; i < hints.size() && i < 5; ++i) {
                        std::cout << hints[i] << " ";
                    }
                    std::cout << "\033[0m";

                    SetConsoleCursorPosition(hOut, csbi.dwCursorPosition);
                }
            }
        }
    });

    ReadConsoleW(hIn, wbuf, 4095, &nr, nullptr);
    reading = false;
    hint_thread.join();

    int needed = WideCharToMultiByte(CP_UTF8, 0, wbuf, nr, nullptr, 0, nullptr, nullptr);
    std::string input(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf, nr, &input[0], needed, nullptr, nullptr);

    while (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
        input.pop_back();
    }

    return input;
}

} // namespace agent::cli
