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
// Filled primary button and the text on it.
COLORREF ThemeAccent();
COLORREF ThemeAccentHot();
COLORREF ThemeAccentPressed();
COLORREF ThemeAccentText();
// Fill and text of a button that asks for a destructive confirmation.
COLORREF ThemeCritical();
COLORREF ThemeCriticalText();

// Immersive dark title bar on or off for a top-level window.
void ApplyTitleBarTheme(HWND hwnd);
// Dark or default visual style for a child control; darkName is used in dark mode, null restores the default.
void ApplyControlTheme(HWND control, const wchar_t* darkName);
