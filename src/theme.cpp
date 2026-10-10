#include "theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace {

int g_override = 0;

// The colour helpers run per control per paint, so the system state is read once and again after
// RefreshThemeCache (settings change, system colour change). UI thread only.
bool g_cacheValid = false;
bool g_cachedDark = false;
bool g_cachedHighContrast = false;

void FillCache() {
    if (g_cacheValid) return;
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    g_cachedHighContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    g_cachedDark = SystemUsesDarkApps();
    g_cacheValid = true;
}

constexpr DWORD kUseImmersiveDarkMode = 20;

}  // namespace

bool SystemUsesDarkApps() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS rc = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                              L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return rc == ERROR_SUCCESS && value == 0;
}

void SetThemeOverride(int mode) { g_override = mode; }

void RefreshThemeCache() { g_cacheValid = false; }

bool IsDarkTheme() {
    if (IsHighContrast()) return false;
    if (g_override == 1) return false;
    if (g_override == 2) return true;
    FillCache();
    return g_cachedDark;
}

bool IsHighContrast() {
    FillCache();
    return g_cachedHighContrast;
}

COLORREF ThemeBackground() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOW);
    return IsDarkTheme() ? RGB(0x20, 0x20, 0x20) : RGB(0xF3, 0xF3, 0xF3);
}
COLORREF ThemeText() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOWTEXT);
    return IsDarkTheme() ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x1A, 0x1A, 0x1A);
}
COLORREF ThemeDisabledText() {
    if (IsHighContrast()) return GetSysColor(COLOR_GRAYTEXT);
    return IsDarkTheme() ? RGB(0x9D, 0x9D, 0x9D) : RGB(0x6E, 0x6E, 0x6E);
}
COLORREF ThemeCard() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOW);
    return IsDarkTheme() ? RGB(0x2B, 0x2B, 0x2B) : RGB(0xFB, 0xFB, 0xFB);
}
COLORREF ThemeCardBorder() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOWTEXT);
    return IsDarkTheme() ? RGB(0x1C, 0x1C, 0x1C) : RGB(0xE5, 0xE5, 0xE5);
}
COLORREF ThemeControl() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOW);
    return IsDarkTheme() ? RGB(0x37, 0x37, 0x37) : RGB(0xFF, 0xFF, 0xFF);
}
COLORREF ThemeControlHot() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOW);
    return IsDarkTheme() ? RGB(0x3D, 0x3D, 0x3D) : RGB(0xF6, 0xF6, 0xF6);
}
COLORREF ThemeControlPressed() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOW);
    return IsDarkTheme() ? RGB(0x32, 0x32, 0x32) : RGB(0xF0, 0xF0, 0xF0);
}
COLORREF ThemeControlBorder() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOWTEXT);
    return IsDarkTheme() ? RGB(0x48, 0x48, 0x48) : RGB(0xD5, 0xD5, 0xD5);
}
COLORREF ThemeAccent() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHT);
    return IsDarkTheme() ? RGB(0x4C, 0xC2, 0xFF) : RGB(0x00, 0x5F, 0xB8);
}
COLORREF ThemeAccentHot() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHT);
    return IsDarkTheme() ? RGB(0x48, 0xB1, 0xE8) : RGB(0x19, 0x75, 0xC5);
}
COLORREF ThemeAccentPressed() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHT);
    return IsDarkTheme() ? RGB(0x42, 0xA1, 0xD2) : RGB(0x31, 0x83, 0xCA);
}
COLORREF ThemeAccentText() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHTTEXT);
    return IsDarkTheme() ? RGB(0x00, 0x00, 0x00) : RGB(0xFF, 0xFF, 0xFF);
}

void ApplyTitleBarTheme(HWND hwnd) {
    BOOL dark = IsDarkTheme() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &dark, sizeof(dark));
}

void ApplyControlTheme(HWND control, const wchar_t* darkName) {
    SetWindowTheme(control, IsDarkTheme() ? darkName : nullptr, nullptr);
}
