#pragma once

#include <cstdint>

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
    ResetConfirm,
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
    // Tray menu, the line above the status, with the compact count (FormatCountShort).
    TrayAdsCount,
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
// Thousands and decimal separator of a language ("1.234" and "1,2" in German, "1,234" and "1.2" in English).
struct NumberSeparators {
    const wchar_t* thousands;
    const wchar_t* decimal;
};
NumberSeparators SeparatorsFor(Language language);
// n with its digits grouped by three: GroupDigits(1234567, L".") is "1.234.567".
std::wstring GroupDigits(uint64_t n, const wchar_t* thousands);
// Short form for one menu line, never more than the real count: full below 10,000, then whole thousands ("12K"),
// millions with one decimal below 10 million ("1,2M", "3M"), whole millions, then billions the same way ("B").
std::wstring CompactCount(uint64_t n, NumberSeparators separators);
// The two above in the active language.
std::wstring FormatCount(uint64_t n);
std::wstring FormatCountShort(uint64_t n);
// "AdMutify" + newline + status, at most 127 characters.
std::wstring TooltipText(const wchar_t* status);
