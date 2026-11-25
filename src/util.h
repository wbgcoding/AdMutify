#pragma once

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <string>
#include <string_view>

// printf-style formatting into a string of the needed length, so a long translation never overflows a buffer.
inline std::wstring FormatText(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int length = _vscwprintf(format, args);
    va_end(args);
    if (length <= 0) return {};
    std::wstring out(static_cast<size_t>(length) + 1, L'\0');
    va_start(args, format);
    _vsnwprintf_s(out.data(), out.size(), _TRUNCATE, format, args);
    va_end(args);
    out.resize(static_cast<size_t>(length));
    return out;
}

// Strips spaces, tabs and line breaks at both ends (ini lines).
inline std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

inline bool EqualsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}

// Full path of the running exe, empty on failure. The buffer grows for paths beyond MAX_PATH.
inline std::wstring ExePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
}

// File name of a process image (no folder), empty when it cannot be queried.
inline std::wstring ImageFileName(HANDLE process) {
    // Long path aware: an image path may exceed MAX_PATH.
    std::wstring path(32768, L'\0');
    DWORD len = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &len)) return {};
    std::wstring_view p(path.data(), len);
    size_t slash = p.find_last_of(L"\\/");
    return std::wstring(slash == std::wstring_view::npos ? p : p.substr(slash + 1));
}

inline std::wstring ExeNameOfPid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    std::wstring name = ImageFileName(h);
    CloseHandle(h);
    return name;
}

inline std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

inline std::string WideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
