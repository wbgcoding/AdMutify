#include "theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>

namespace {

int g_override = 0;

// The colour helpers run per control per paint, so the system state is read once and again after
// RefreshThemeCache (settings change, system colour change). UI thread only.
bool g_cacheValid = false;
bool g_cachedDark = false;
bool g_cachedHighContrast = false;
AccentColors g_cachedAccent = kDefaultAccent;
bool g_accentForced = false;
AccentColors g_forcedAccent = kDefaultAccent;

AccentColors ReadSystemAccent() {
    BYTE data[64] = {};
    DWORD size = sizeof(data);
    AccentColors out = kDefaultAccent;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette",
                     RRF_RT_REG_BINARY, nullptr, data, &size) == ERROR_SUCCESS)
        ParseAccentPalette(data, size, &out);
    return out;
}

void FillCache() {
    if (g_cacheValid) return;
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    g_cachedHighContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    g_cachedDark = SystemUsesDarkApps();
    g_cachedAccent = ReadSystemAccent();
    g_cacheValid = true;
}

const AccentColors& Accent() {
    if (g_accentForced) return g_forcedAccent;
    FillCache();
    return g_cachedAccent;
}

double Channel(int v) {
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double Luminance(COLORREF c) {
    return 0.2126 * Channel(GetRValue(c)) + 0.7152 * Channel(GetGValue(c)) + 0.0722 * Channel(GetBValue(c));
}

// Button fill: the accent itself on dark, its first darker shade on light (as Windows does for light mode).
COLORREF AccentFill() { return IsDarkTheme() ? Accent().base : Accent().dark1; }

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
bool ParseAccentPalette(const BYTE* data, size_t size, AccentColors* out) {
    // Eight RGBA entries, lightest first: light3, light2, light1, accent, dark1, dark2, dark3, unused.
    if (!data || !out || size < 32) return false;
    auto at = [&](int i) { return RGB(data[i * 4], data[i * 4 + 1], data[i * 4 + 2]); };
    out->light2 = at(1);
    out->base = at(3);
    out->dark1 = at(4);
    return true;
}

double ContrastRatio(COLORREF a, COLORREF b) {
    const double la = Luminance(a);
    const double lb = Luminance(b);
    return ((std::max)(la, lb) + 0.05) / ((std::min)(la, lb) + 0.05);
}

COLORREF BlendColor(COLORREF from, COLORREF to, int percentTo) {
    auto mix = [&](int a, int b) { return (a * (100 - percentTo) + b * percentTo + 50) / 100; };
    return RGB(mix(GetRValue(from), GetRValue(to)), mix(GetGValue(from), GetGValue(to)), mix(GetBValue(from), GetBValue(to)));
}

COLORREF TextOn(COLORREF fill) {
    // White wherever it is readable (the usual look on a coloured button), black only on light accents.
    return ContrastRatio(RGB(0xFF, 0xFF, 0xFF), fill) >= 4.5 ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x00, 0x00, 0x00);
}

void SetAccentForTests(const AccentColors* colors) {
    g_accentForced = colors != nullptr;
    if (colors) g_forcedAccent = *colors;
}

COLORREF ThemeAccent() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHT);
    return AccentFill();
}
// Hover and pressed shade toward the window background, like the 90 % and 80 % opacity of Windows buttons.
COLORREF ThemeAccentHot() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHT);
    return BlendColor(AccentFill(), ThemeBackground(), 10);
}
COLORREF ThemeAccentPressed() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHT);
    return BlendColor(AccentFill(), ThemeBackground(), 20);
}
COLORREF ThemeAccentText() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHTTEXT);
    return TextOn(AccentFill());
}
COLORREF ThemeAccentOnBackground() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOWTEXT);
    return IsDarkTheme() ? Accent().light2 : Accent().dark1;
}
COLORREF ThemeCritical() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOWTEXT);
    return IsDarkTheme() ? RGB(0xFF, 0x99, 0xA4) : RGB(0xC4, 0x2B, 0x1C);
}
COLORREF ThemeCriticalText() {
    if (IsHighContrast()) return GetSysColor(COLOR_WINDOW);
    return IsDarkTheme() ? RGB(0x1A, 0x1A, 0x1A) : RGB(0xFF, 0xFF, 0xFF);
}

void ApplyTitleBarTheme(HWND hwnd) {
    BOOL dark = IsDarkTheme() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &dark, sizeof(dark));
}

void ApplyControlTheme(HWND control, const wchar_t* darkName) {
    SetWindowTheme(control, IsDarkTheme() ? darkName : nullptr, nullptr);
}
