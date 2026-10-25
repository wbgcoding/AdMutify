#pragma once

#include <windows.h>

// Fixed control IDs (also the UI Automation identifiers).
enum SettingsControlId {
    IDC_AUTOSTART = 101,
    IDC_HIDETRAY = 102,
    IDC_GROUP_ADS = 104,
    IDC_MODE_MUTE = 105,
    IDC_MODE_LOWER = 106,
    IDC_MODE_MUSIC = 127,
    IDC_PERCENT = 107,
    IDC_PERCENT_VALUE = 108,
    IDC_LANGUAGE_LABEL = 109,
    IDC_LANGUAGE = 110,
    IDC_VERSION = 111,
    IDC_RESET = 112,
    IDC_CLOSE = 113,
    IDC_MEDIA_LABEL = 114,
    IDC_MEDIA = 115,
    IDC_MEDIA_PERCENT_LABEL = 116,
    IDC_MEDIA_PERCENT = 117,
    IDC_MEDIA_PERCENT_VALUE = 118,
    IDC_MUSIC_LABEL = 119,
    IDC_MUSIC = 120,
    IDC_MUSIC_PATH = 121,
    IDC_MUSIC_PICK = 122,
    IDC_MUSIC_VOLUME_LABEL = 123,
    IDC_MUSIC_VOLUME = 124,
    IDC_MUSIC_VOLUME_VALUE = 125,
    IDC_ADS_COUNT = 126,
    IDC_ADS_CAPTION = 128,
};

// Opens the settings window, or brings the open one to the front.
void OpenSettingsWindow();
// Lets the settings window handle keyboard navigation; returns true when the message was consumed.
bool SettingsWindowPreTranslate(MSG* msg);
// Puts the reset button of the open window into its confirm state (review shots).
void SettingsWindowArmReset();
// The open window or null.
HWND SettingsWindowHandle();
// Shows the current ad counter in the open window (no-op when closed).
void SettingsWindowUpdateCounter();
