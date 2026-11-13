// Edge tests for the autostart entry: odd registry value types, Task Manager bytes and foreign entries.
// Only the test key is used, never the real Run key.
#include <windows.h>

#include <cwctype>
#include <string>

#include "autostart.h"
#include "settings.h"
#include "test.h"
#include "util.h"

namespace {

const wchar_t kEdgeKey[] = L"Software\\AdMutifyTest\\Run";
const wchar_t kEdgeApproved[] = L"Software\\AdMutifyTest\\RunApproved";

void Begin() {
    SetAutostartKeyOverride(kEdgeKey);
    SetAutostartPortableOverride(0);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AdMutifyTest");
}

void End() {
    SetAutostartPortableOverride(-1);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AdMutifyTest");
}

bool PutValue(const wchar_t* subKey, const wchar_t* name, DWORD type, const void* data, DWORD size) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const bool ok = RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(data), size) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

bool PutText(const wchar_t* subKey, const wchar_t* name, DWORD type, const std::wstring& text) {
    return PutValue(subKey, name, type, text.c_str(), static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
}

bool HasValue(const wchar_t* subKey, const wchar_t* name) {
    return RegGetValueW(HKEY_CURRENT_USER, subKey, name, RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

std::wstring OwnCommand() { return L"\"" + ExePath() + L"\" --autostart"; }

}  // namespace

TEST(autostart_edges_startup_approved_first_byte) {
    const unsigned char b00[] = {0x00};
    const unsigned char b01[] = {0x01};
    const unsigned char b02[] = {0x02};
    const unsigned char bFE[] = {0xFE};
    const unsigned char bFF[] = {0xFF};
    CHECK(!StartupApprovedMeansDisabled(b00, 1));
    CHECK(StartupApprovedMeansDisabled(b01, 1));
    CHECK(!StartupApprovedMeansDisabled(b02, 1));
    CHECK(!StartupApprovedMeansDisabled(bFE, 1));
    CHECK(StartupApprovedMeansDisabled(bFF, 1));
    // Only the first byte decides; later bytes never do.
    const unsigned char later[] = {0x02, 0x01, 0x01, 0x01};
    CHECK(!StartupApprovedMeansDisabled(later, sizeof(later)));
    CHECK(!StartupApprovedMeansDisabled(b01, 0));
}

TEST(autostart_edges_run_value_must_be_our_exact_command) {
    Begin();
    CHECK(!IsAutostartEnabled());
    // The same text in another case still counts (Windows paths ignore case).
    std::wstring upper = OwnCommand();
    for (wchar_t& c : upper) c = static_cast<wchar_t>(towupper(c));
    CHECK(PutText(kEdgeKey, L"AdMutify", REG_SZ, upper));
    CHECK(IsAutostartEnabled());
    // Without the switch, with other switches, empty or unquoted: not ours.
    CHECK(PutText(kEdgeKey, L"AdMutify", REG_SZ, L"\"" + ExePath() + L"\""));
    CHECK(!IsAutostartEnabled());
    CHECK(PutText(kEdgeKey, L"AdMutify", REG_SZ, OwnCommand() + L" --extra"));
    CHECK(!IsAutostartEnabled());
    CHECK(PutText(kEdgeKey, L"AdMutify", REG_SZ, L""));
    CHECK(!IsAutostartEnabled());
    CHECK(PutText(kEdgeKey, L"AdMutify", REG_SZ, ExePath() + L" --autostart"));
    CHECK(!IsAutostartEnabled());
    End();
}

TEST(autostart_edges_wrong_value_types_do_not_count) {
    Begin();
    // The same command stored as another type is not what this app writes.
    CHECK(PutText(kEdgeKey, L"AdMutify", REG_EXPAND_SZ, OwnCommand()));
    CHECK(!IsAutostartEnabled());
    const DWORD number = 3;
    CHECK(PutValue(kEdgeKey, L"AdMutify", REG_DWORD, &number, sizeof(number)));
    CHECK(!IsAutostartEnabled());
    const unsigned char bytes[] = {1, 2, 3};
    CHECK(PutValue(kEdgeKey, L"AdMutify", REG_BINARY, bytes, sizeof(bytes)));
    CHECK(!IsAutostartEnabled());
    // Switching on replaces the odd value with a proper string.
    CHECK(SetAutostart(true));
    CHECK(IsAutostartEnabled());
    CHECK(SetAutostart(false));
    CHECK(!HasValue(kEdgeKey, L"AdMutify"));
    End();
}

TEST(autostart_edges_task_manager_value_shapes) {
    Begin();
    CHECK(SetAutostart(true));
    CHECK(IsAutostartEnabled());
    // Disabled marker of the right shape.
    unsigned char off[12] = {0x03};
    CHECK(PutValue(kEdgeApproved, L"AdMutify", REG_BINARY, off, sizeof(off)));
    CHECK(!IsAutostartEnabled());
    // Enabled marker.
    unsigned char on[12] = {0x02};
    CHECK(PutValue(kEdgeApproved, L"AdMutify", REG_BINARY, on, sizeof(on)));
    CHECK(IsAutostartEnabled());
    // A value of the wrong type or too large for the reader never disables the entry.
    const DWORD number = 3;
    CHECK(PutValue(kEdgeApproved, L"AdMutify", REG_DWORD, &number, sizeof(number)));
    CHECK(IsAutostartEnabled());
    CHECK(PutText(kEdgeApproved, L"AdMutify", REG_SZ, L"\x0003"));
    CHECK(IsAutostartEnabled());
    unsigned char big[64];
    for (unsigned char& b : big) b = 0x03;
    CHECK(PutValue(kEdgeApproved, L"AdMutify", REG_BINARY, big, sizeof(big)));
    CHECK(IsAutostartEnabled());
    // An empty binary value has no first byte.
    CHECK(PutValue(kEdgeApproved, L"AdMutify", REG_BINARY, on, 0));
    CHECK(IsAutostartEnabled());
    CHECK(SetAutostart(false));
    End();
}

TEST(autostart_edges_switching_off_is_safe_when_nothing_is_there) {
    Begin();
    // No key at all.
    CHECK(SetAutostart(false));
    CHECK(!IsAutostartEnabled());
    // A key without the value.
    CHECK(PutText(kEdgeKey, L"Other", REG_SZ, L"x"));
    CHECK(SetAutostart(false));
    CHECK(SetAutostart(false));
    // Foreign values next to ours stay untouched.
    CHECK(HasValue(kEdgeKey, L"Other"));
    CHECK(SetAutostart(true));
    CHECK(SetAutostart(false));
    CHECK(HasValue(kEdgeKey, L"Other"));
    End();
}

TEST(autostart_edges_portable_leaves_foreign_installed_entry) {
    Begin();
    SetAutostartPortableOverride(1);
    // An entry under the installed name that points elsewhere belongs to another copy.
    CHECK(PutText(kEdgeKey, L"AdMutify", REG_SZ, L"\"C:\\Other\\AdMutify.exe\" --autostart"));
    CHECK(!IsAutostartEnabled());
    CHECK(SetAutostart(true));
    CHECK(IsAutostartEnabled());
    CHECK(HasValue(kEdgeKey, L"AdMutify Portable"));
    CHECK(HasValue(kEdgeKey, L"AdMutify"));
    CHECK(SetAutostart(false));
    CHECK(!HasValue(kEdgeKey, L"AdMutify Portable"));
    CHECK(HasValue(kEdgeKey, L"AdMutify"));
    CHECK(!IsAutostartEnabled());
    End();
}
