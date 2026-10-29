#pragma once

#include <string>

#include "settings.h"

enum class StrId {
    StatusActive,
    StatusMuted,
    StatusLowered,
    StatusMediaMuted,
    StatusMediaLowered,
    StatusMediaPaused,
    StatusNoSpotify,
    StatusDisabled,
    MenuEnabled,
    MenuSettings,
    MenuExit,
    MenuHideTray,
    SettingsTitle,
    AdsMutedCaption,
    ToggleOn,
    ToggleOff,
    OptAutostart,
    OptHideTray,
    OptHideTrayHint,
    GroupAds,
    ModeMute,
    ModeLower,
    ModeMusic,
    PercentValue,
    MediaLabel,
    MediaOff,
    MediaLower,
    MediaMute,
    MediaPause,
    MediaHint,
    MediaPercentLabel,
    MusicSource,
    MusicFile,
    MusicShuffle,
    MusicOrder,
    MusicHint,
    MusicNoFile,
    MusicNoFolder,
    MusicPick,
    MusicVolumeLabel,
    MusicPickFileTitle,
    MusicPickFolderTitle,
    MusicFilterName,
    ErrMusicEmpty,
    LanguageLabel,
    LangSystem,
    ResetButton,
    ResetConfirmClick,
    Close,
    ErrSettingsNewer,
    ErrSettingsSave,
    ErrAutostart,
    ErrTechnical,
    CrashTitle,
    CrashBody,
    CrashBodyNotRestored,
    CrashCopy,
    CrashOpenLogs,
    Count
};

void SetLanguage(Language language);
const wchar_t* Str(StrId id);
// Table access independent of the active language (tests, previews). System resolves via the Windows UI language.
const wchar_t* StrFor(Language language, StrId id);
// Pure mapping of a Windows UI language to the table used for it (unknown languages fall back to English).
Language LanguageFromLangId(unsigned short langId);
// Name of a language in that language itself (never translated); System has none and returns L"".
const wchar_t* LanguageNativeName(Language language);
// Concrete language of the language setting (System resolved).
Language ResolveLanguage(Language language);

// A table text with one %d (status lines, percent values) filled in.
std::wstring FormatPercent(StrId id, int percent);
// "AdMutify" + newline + status, at most 127 characters.
std::wstring TooltipText(const wchar_t* status);
