#pragma once

#include <windows.h>

#include <string>

#include "settings.h"

struct AppOptions {
    bool autostart = false;
    bool secondInstance = false;
    bool trace = false;
};

// Runs the tray application until it is closed. Returns the process exit code.
int RunApp(HINSTANCE inst, const AppOptions& options);

// Settings access for the settings window.
const Settings& CurrentSettings();
// The status line of the tray tooltip (ad muted, no ad, disabled, ...).
const std::wstring& CurrentStatus();
// Stores new settings: saves them, applies language, enabled state and mode right away.
void CommitSettings(const Settings& settings);

#ifdef ADMUTIFY_REVIEW
// Review tool: keep settings in memory only; nothing is saved and no state logic runs.
void AppUseInMemorySettings(const Settings& settings);
#endif
