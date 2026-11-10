#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <conio.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
};

inline void check(BOOL ok, const char* action) {
    if (!ok) {
        DWORD code = GetLastError();
        throw std::runtime_error(std::string(action) + ": WinAPI error " + std::to_string(code));
    }
}
inline void consoleSetup() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}
inline long long number(const std::string& text) {
    size_t used = 0;
    long long n = std::stoll(text, &used);
    if (used != text.size()) throw std::runtime_error("Ожидалось целое число");
    return n;
}
inline std::string line(const char* prompt) {
    std::cout << prompt << std::flush;
    std::string s;
    if (!std::getline(std::cin, s)) throw std::runtime_error("Ввод завершён");
    return s;
}
inline std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    check(n != 0, "MultiByteToWideChar");
    std::wstring w(n, L'\0');
    check(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), &w[0], n), "MultiByteToWideChar");
    return w;
}
inline std::wstring folder() {
    std::vector<wchar_t> buffer(32768);
    DWORD n = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
    check(n && n < buffer.size(), "GetModuleFileNameW");
    std::wstring s(buffer.data(), n);
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}
inline std::wstring systemProgram(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    check(n && n < MAX_PATH, "GetSystemDirectoryW");
    return std::wstring(path) + L"\\" + name;
}
inline PROCESS_INFORMATION launch(const std::wstring& app, const std::wstring& args, DWORD flags = 0) {
    std::wstring command = L"\"" + app + L"\" " + args;
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    check(CreateProcessW(app.c_str(), buffer.data(), nullptr, nullptr, FALSE,
                         flags, nullptr, folder().c_str(), &si, &pi), "CreateProcessW");
    return pi;
}
// Короткая проверка ввода позволяет продолжать наблюдение за рабочими потоками.
inline bool command(std::string& buffer, std::string& ready) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (GetFileType(input) == FILE_TYPE_PIPE) {
        DWORD available = 0;
        while (PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) && available) {
            char c; DWORD read = 0;
            if (!ReadFile(input, &c, 1, &read, nullptr) || !read) break;
            if (c == '\n') { ready = buffer; buffer.clear(); return true; }
            if (c != '\r') buffer += c;
        }
        return false;
    }
    while (_kbhit()) {
        int c = _getch();
        if (c == 0 || c == 224) { _getch(); continue; }
        if (c == '\r') {
            std::cout << '\n'; ready = buffer; buffer.clear(); return true;
        }
        if (c == '\b') {
            if (!buffer.empty()) { buffer.pop_back(); std::cout << "\b \b"; }
        } else if (c >= 32 && c < 127) { buffer += char(c); std::cout << char(c) << std::flush; }
    }
    return false;
}
inline unsigned long long ticks(FILETIME t) {
    return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
