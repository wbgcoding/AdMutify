#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

#include "autostart.h"
#include "settings.h"
#include "test.h"

namespace {

const wchar_t kTestKey[] = L"Software\\AdMutifyTest\\Run";

void UseTestKey() { SetAutostartKeyOverride(kTestKey); }

// A file that touches autostart must also install the key override.
bool HasAutostartGuardViolation(std::string_view text) {
    bool calls = text.find("SetAutostart(") != std::string_view::npos ||
                 text.find("IsAutostartEnabled(") != std::string_view::npos;
    bool guarded = text.find("SetAutostartKeyOverride") != std::string_view::npos;
    return calls && !guarded;
}

}  // namespace

TEST(autostart_on_off) {
    UseTestKey();
    CHECK(SetAutostart(false));
    CHECK(!IsAutostartEnabled());
    CHECK(SetAutostart(true));
    CHECK(IsAutostartEnabled());
    CHECK(SetAutostart(false));
    CHECK(!IsAutostartEnabled());
    CHECK(SetAutostart(false));
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AdMutifyTest");
}

TEST(autostart_foreign_path_is_off) {
    UseTestKey();
    HKEY key = nullptr;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, kTestKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS);
    const wchar_t foreign[] = L"\"C:\\Elsewhere\\Other.exe\" --autostart";
    RegSetValueExW(key, L"AdMutify", 0, REG_SZ, reinterpret_cast<const BYTE*>(foreign), sizeof(foreign));
    RegCloseKey(key);
    CHECK(!IsAutostartEnabled());
    CHECK(SetAutostart(true));
    CHECK(IsAutostartEnabled());
    CHECK(SetAutostart(false));
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AdMutifyTest");
}

TEST(autostart_startup_approved_bytes) {
    const unsigned char enabled[] = {0x02, 0, 0, 0};
    const unsigned char enabledAlt[] = {0x06, 0, 0, 0};
    const unsigned char disabled[] = {0x03, 0, 0, 0};
    const unsigned char disabledAlt[] = {0x07, 0, 0, 0};
    CHECK(!StartupApprovedMeansDisabled(enabled, sizeof(enabled)));
    CHECK(!StartupApprovedMeansDisabled(enabledAlt, sizeof(enabledAlt)));
    CHECK(StartupApprovedMeansDisabled(disabled, sizeof(disabled)));
    CHECK(StartupApprovedMeansDisabled(disabledAlt, sizeof(disabledAlt)));
    CHECK(!StartupApprovedMeansDisabled(disabled, 0));
    CHECK(!StartupApprovedMeansDisabled(nullptr, 4));
}

TEST(autostart_disabled_in_task_manager) {
    UseTestKey();
    CHECK(SetAutostart(true));
    CHECK(IsAutostartEnabled());
    // Task Manager's choice lives in a sibling key; the test override mirrors that layout.
    HKEY key = nullptr;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\AdMutifyTest\\RunApproved", 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                          &key, nullptr) == ERROR_SUCCESS);
    const unsigned char off[12] = {0x03};
    RegSetValueExW(key, L"AdMutify", 0, REG_BINARY, off, sizeof(off));
    RegCloseKey(key);
    CHECK(!IsAutostartEnabled());
    CHECK(SetAutostart(true));
    CHECK(IsAutostartEnabled());
    CHECK(SetAutostart(false));
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AdMutifyTest");
}

TEST(autostart_guard_fixture) {
    UseTestKey();
    CHECK(HasAutostartGuardViolation("void f() { SetAutostart(true); }"));
    CHECK(HasAutostartGuardViolation("bool b = IsAutostartEnabled();"));
    CHECK(!HasAutostartGuardViolation("SetAutostartKeyOverride(L\"x\"); SetAutostart(true);"));
    CHECK(!HasAutostartGuardViolation("int unrelated = 0;"));
}

TEST(autostart_guard_sources) {
    UseTestKey();
    std::wstring dir = std::wstring(ADMUTIFY_SOURCE_DIR) + L"\\tests\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.cpp").c_str(), &fd);
    CHECK(h != INVALID_HANDLE_VALUE);
    int scanned = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string text;
            CHECK(ReadFileUtf8(dir + fd.cFileName, text));
            CHECK(!HasAutostartGuardViolation(text));
            ++scanned;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    CHECK(scanned >= 5);
}
