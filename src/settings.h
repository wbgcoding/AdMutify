#pragma once

#include <string>
#include <string_view>

enum class Mode { Mute, Lower };
enum class MediaMode { Off, Lower, Mute, Pause };
// Own music during ads: off, one file (looped), a folder shuffled or in name order.
enum class AdMusic { Off, File, Shuffle, Order };
enum class Language { System, English, German, Spanish, Portuguese, Italian, Dutch, Polish, Turkish, Japanese, Chinese };
constexpr int kLanguageCount = 11;
// ini code of a language ("system", "en", "de", ...) and the reverse; unknown text gives System.
const char* LanguageCode(Language language);
Language LanguageFromCode(std::string_view code);

struct Settings {
    bool enabled = true;
    Mode mode = Mode::Mute;
    int lowerPercent = 20;
    bool hideTray = false;
    Language language = Language::System;
    bool darkMenus = true;
    // Reaction to video playback in other apps.
    MediaMode media = MediaMode::Off;
    // Level of the video reaction "Lower"; independent of the ad level.
    int mediaPercent = 20;
    // Music played while an ad is muted or lowered; path is a file (File) or a folder (Shuffle, Order).
    AdMusic adMusic = AdMusic::Off;
    std::wstring adMusicPath;
    int adMusicVolume = 50;
    // Hidden key `trace=1`: turns on the troubleshooting trace at start like --trace. Not in the window; only written
    // when set, and cleared by a reset.
    bool trace = false;
};

struct LoadResult {
    Settings settings;
    bool newerVersion = false;
    bool created = false;
    // The file exists but could not be read; defaults are used and the file is left untouched.
    bool readFailed = false;
    unsigned long readError = 0;
    bool saveFailed = false;
    unsigned long saveError = 0;
};

// Pure parts
int ClampLowerPercent(int value);
// 0-100 in steps of 5.
int ClampVolumePercent(int value);
std::string FormatSettings(const Settings& s);
// Returns false only when the text carries an unknown (newer) version.
bool ParseSettings(std::string_view text, Settings& out);

// Paths
std::wstring ExeDir();
bool IsPortable();
// The portable build keeps its data next to the exe without a marker file.
void SetPortableBuild(bool on);
std::wstring DataDir();
std::wstring LogDir();
std::wstring SettingsPath();
std::wstring StatePath();
void SetDataDirOverride(const std::wstring& path);
void SetLogDirOverride(const std::wstring& path);

// File helpers
bool ReadFileUtf8(const std::wstring& path, std::string& out);
bool WriteFileAtomic(const std::wstring& path, std::string_view data, unsigned long* lastError = nullptr);
bool EnsureDirectory(const std::wstring& path);

// Load and save
LoadResult LoadOrCreateSettings();
// True while the file could not be read at start and nothing is written to it.
bool SettingsReadFailed();
// Reads the file again after such a failure. On success the hold is lifted and the file's values are returned
// (defaults when the file is gone; a newer-version file keeps being protected). On failure returns false and
// sets error.
bool RetrySettingsRead(Settings& fromFile, unsigned long* error);
// True when the settings file was found to be from a newer version (it is then never overwritten).
bool SettingsNewerVersion();
// Three-way merge for the first change after a failed read: a field the user changed (current differs from
// shown) takes the new value, every other field keeps the value from the file.
Settings MergeSettingsChange(const Settings& fromFile, const Settings& shown, const Settings& current);
// A file with a newer version, or one that could not be read at start, is never overwritten;
// returns true without writing then.
bool SaveSettings(const Settings& s, unsigned long* lastError = nullptr);
