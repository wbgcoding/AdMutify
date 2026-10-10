#pragma once

#include <windows.h>

#include <cwctype>
#include <string>
#include <string_view>

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
    wchar_t path[MAX_PATH * 2];
    DWORD len = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    if (!QueryFullProcessImageNameW(process, 0, path, &len)) return {};
    std::wstring_view p(path, len);
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
