#pragma once

#include <windows.h>

// True when Windows is set to dark app mode (AppsUseLightTheme = 0).
bool SystemUsesDarkApps();
// Forces a theme regardless of the system: 0 = follow system, 1 = light, 2 = dark.
void SetThemeOverride(int mode);
// The dark and high contrast flags are cached; call this when the system theme or colours changed.
void RefreshThemeCache();
bool IsDarkTheme();
// True while a Windows high contrast theme is active; colours then come from the system.
bool IsHighContrast();

COLORREF ThemeBackground();
COLORREF ThemeText();
COLORREF ThemeDisabledText();
// Raised surface behind grouped settings and its outline.
COLORREF ThemeCard();
COLORREF ThemeCardBorder();
// Input field (drop-down box) fill per state and its outline.
COLORREF ThemeControl();
COLORREF ThemeControlHot();
COLORREF ThemeControlPressed();
COLORREF ThemeControlBorder();
// The Windows accent colour with two of its shades.
struct AccentColors {
    COLORREF light2;
    COLORREF base;
    COLORREF dark1;
};
// The Windows default blue, used when the system palette cannot be read.
constexpr AccentColors kDefaultAccent{RGB(0x4C, 0xC2, 0xFF), RGB(0x00, 0x78, 0xD4), RGB(0x00, 0x67, 0xC0)};
// Reads the 32 byte AccentPalette registry value; false (out unchanged) when it is too short.
bool ParseAccentPalette(const BYTE* data, size_t size, AccentColors* out);
// WCAG contrast ratio, 1 to 21.
double ContrastRatio(COLORREF a, COLORREF b);
// Mixes percentTo % of `to` into `from`.
COLORREF BlendColor(COLORREF from, COLORREF to, int percentTo);
// White when it reaches 4.5:1 on fill, else black.
COLORREF TextOn(COLORREF fill);
// Test seam: a fixed accent instead of the system one; null follows the system again.
void SetAccentForTests(const AccentColors* colors);

// Filled primary button (and switch) in the Windows accent colour, and the text on it.
COLORREF ThemeAccent();
COLORREF ThemeAccentHot();
COLORREF ThemeAccentPressed();
COLORREF ThemeAccentText();
// Accent coloured text straight on the window background.
COLORREF ThemeAccentOnBackground();
// Fill and text of a button that asks for a destructive confirmation.
COLORREF ThemeCritical();
COLORREF ThemeCriticalText();

// Immersive dark title bar on or off for a top-level window.
void ApplyTitleBarTheme(HWND hwnd);
// Dark or default visual style for a child control; darkName is used in dark mode, null restores the default.
void ApplyControlTheme(HWND control, const wchar_t* darkName);
