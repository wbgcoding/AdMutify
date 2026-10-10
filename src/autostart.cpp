#include "autostart.h"

#include <windows.h>

#include <string>

#include "util.h"

namespace {

std::wstring g_keyOverride;
constexpr wchar_t kRealKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRealApprovedKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"AdMutify";

std::wstring KeyPath() { return g_keyOverride.empty() ? std::wstring(kRealKey) : g_keyOverride; }

// The test override gets its own sibling key, so tests never touch the real Task Manager state.
std::wstring ApprovedKeyPath() { return g_keyOverride.empty() ? std::wstring(kRealApprovedKey) : g_keyOverride + L"Approved"; }

bool DisabledInTaskManager() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, ApprovedKeyPath().c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    unsigned char data[16] = {};
    DWORD size = sizeof(data);
    DWORD type = 0;
    LSTATUS rc = RegQueryValueExW(key, kValueName, nullptr, &type, data, &size);
    RegCloseKey(key);
    // Task Manager writes 12 bytes. Anything that does not fit (ERROR_MORE_DATA leaves the buffer undefined) is
    // not its value, so it does not disable the entry.
    if (rc != ERROR_SUCCESS) return false;
    return type == REG_BINARY && StartupApprovedMeansDisabled(data, size);
}

void ClearTaskManagerChoice() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, ApprovedKeyPath().c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    RegDeleteValueW(key, kValueName);
    RegCloseKey(key);
}

std::wstring Command() { return L"\"" + ExePath() + L"\" --autostart"; }

}  // namespace

void SetAutostartKeyOverride(const wchar_t* subKey) { g_keyOverride = subKey ? subKey : L""; }

bool StartupApprovedMeansDisabled(const unsigned char* data, unsigned long size) { return data && size >= 1 && (data[0] & 1) != 0; }

bool IsAutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KeyPath().c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = 0;
    LSTATUS rc = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &size);
    std::wstring value;
    if (rc == ERROR_SUCCESS && type == REG_SZ) {
        value.resize(size / sizeof(wchar_t) + 1);
        size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        rc = RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size);
        value.resize(rc == ERROR_SUCCESS ? size / sizeof(wchar_t) : 0);
        while (!value.empty() && value.back() == L'\0') value.pop_back();
    }
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return false;
    return EqualsNoCase(value, Command()) && !DisabledInTaskManager();
}

bool SetAutostart(bool enable) {
    if (enable) {
        if (ExePath().empty()) return false;
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, KeyPath().c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
            ERROR_SUCCESS)
            return false;
        std::wstring cmd = Command();
        LSTATUS rc = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                                    static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        // Ticking the box again also undoes a "Disabled" set in Task Manager.
        if (rc == ERROR_SUCCESS) ClearTaskManagerChoice();
        return rc == ERROR_SUCCESS;
    }
    HKEY key = nullptr;
    LSTATUS open = RegOpenKeyExW(HKEY_CURRENT_USER, KeyPath().c_str(), 0, KEY_SET_VALUE, &key);
    if (open == ERROR_FILE_NOT_FOUND) return true;
    if (open != ERROR_SUCCESS) return false;
    LSTATUS rc = RegDeleteValueW(key, kValueName);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}
