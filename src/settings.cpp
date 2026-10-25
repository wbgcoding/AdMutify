#include "settings.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <charconv>
#include <cstdio>

#include "util.h"

namespace {

std::wstring g_dataOverride;
std::wstring g_logOverride;
bool g_newerVersionSeen = false;
// The file exists but could not be read this run; saving must not replace it with defaults.
bool g_readFailedSeen = false;

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

bool ParseInt(std::string_view s, int& out) {
    int v = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size()) return false;
    out = v;
    return true;
}

bool ParseBool(std::string_view s, bool& out) {
    if (s == "1") { out = true; return true; }
    if (s == "0") { out = false; return true; }
    return false;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) result = p;
    CoTaskMemFree(p);
    return result;
}

}  // namespace

int ClampLowerPercent(int value) {
    if (value < 0) value = 0;
    if (value > 90) value = 90;
    return (value + 2) / 5 * 5;
}

int ClampVolumePercent(int value) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    return (value + 2) / 5 * 5;
}

namespace {
const char* AdMusicCode(AdMusic m) {
    switch (m) {
        case AdMusic::File: return "file";
        case AdMusic::Order: return "order";
        default: return "shuffle";
    }
}

// Line breaks would end the value early; a path cannot contain them anyway.
bool IniSafe(std::wstring_view s) { return s.find_first_of(L"\r\n") == std::wstring_view::npos; }
}  // namespace

namespace {
constexpr const char* kLanguageCodes[kLanguageCount] = {"system", "en", "de", "es", "pt", "it", "nl", "pl", "tr", "ja", "zh"};
}

const char* LanguageCode(Language language) {
    size_t i = static_cast<size_t>(language);
    return i < static_cast<size_t>(kLanguageCount) ? kLanguageCodes[i] : kLanguageCodes[0];
}

Language LanguageFromCode(std::string_view code) {
    for (int i = 1; i < kLanguageCount; ++i)
        if (code == kLanguageCodes[i]) return static_cast<Language>(i);
    return Language::System;
}

std::string FormatSettings(const Settings& s) {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "version=2\r\nenabled=%d\r\nmode=%s\r\nlower_percent=%d\r\nhide_tray=%d\r\nlanguage=%s\r\ndark_menus=%d\r\nmedia=%s\r\nmedia_percent=%d\r\n",
                  s.enabled ? 1 : 0, s.mode == Mode::Lower ? "lower" : (s.mode == Mode::Music ? "music" : "mute"), s.lowerPercent,
                  s.hideTray ? 1 : 0,
                  LanguageCode(s.language),
                  s.darkMenus ? 1 : 0, s.media == MediaMode::Lower ? "lower"
                  : (s.media == MediaMode::Mute ? "mute" : (s.media == MediaMode::Pause ? "pause" : "off")),
                  s.mediaPercent);
    std::string out = buf;
    // The path may be long (long paths are allowed), so it is appended outside the fixed buffer.
    out += "ad_music=";
    out += AdMusicCode(s.adMusic);
    out += "\r\nad_music_path=";
    if (IniSafe(s.adMusicPath)) out += WideToUtf8(s.adMusicPath);
    out += "\r\nad_music_volume=";
    out += std::to_string(s.adMusicVolume);
    out += "\r\n";
    if (s.trace) out += "trace=1\r\n";
    return out;
}

bool ParseSettings(std::string_view text, Settings& out) {
    Settings s;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);

    bool newer = false;
    // A file without a version line counts as current; only a version 1 file is migrated.
    int fileVersion = 2;
    std::string_view legacyMusic;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = Trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line.front() == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = Trim(line.substr(0, eq));
        std::string_view val = Trim(line.substr(eq + 1));

        int n = 0;
        bool b = false;
        if (key == "version") {
            if (ParseInt(val, n)) {
                if (n > 2) newer = true;
                else if (n < 2) fileVersion = 1;
            }
        } else if (key == "enabled") {
            if (ParseBool(val, b)) s.enabled = b;
        } else if (key == "mode") {
            if (val == "mute") s.mode = Mode::Mute;
            else if (val == "lower") s.mode = Mode::Lower;
            else if (val == "music") s.mode = Mode::Music;
            else s.mode = Mode::Mute;
        } else if (key == "lower_percent") {
            if (ParseInt(val, n)) s.lowerPercent = ClampLowerPercent(n);
        } else if (key == "hide_tray") {
            if (ParseBool(val, b)) s.hideTray = b;
        } else if (key == "language") {
            s.language = LanguageFromCode(val);
        } else if (key == "dark_menus") {
            if (ParseBool(val, b)) s.darkMenus = b;
        } else if (key == "media") {
            if (val == "lower") s.media = MediaMode::Lower;
            else if (val == "mute") s.media = MediaMode::Mute;
            else if (val == "pause") s.media = MediaMode::Pause;
            else s.media = MediaMode::Off;
        } else if (key == "media_percent") {
            if (ParseInt(val, n)) s.mediaPercent = ClampLowerPercent(n);
        } else if (key == "ad_music") {
            legacyMusic = val;
            if (val == "file") s.adMusic = AdMusic::File;
            else if (val == "order") s.adMusic = AdMusic::Order;
            else s.adMusic = AdMusic::Shuffle;
        } else if (key == "ad_music_path") {
            s.adMusicPath = Utf8ToWide(val);
        } else if (key == "ad_music_volume") {
            if (ParseInt(val, n)) s.adMusicVolume = ClampVolumePercent(n);
        } else if (key == "trace") {
            if (ParseBool(val, b)) s.trace = b;
        }
    }
    if (newer) {
        out = Settings{};
        return false;
    }
    // Version 1 had the music as a separate setting; a chosen source now means the music mode.
    if (fileVersion < 2 && (legacyMusic == "file" || legacyMusic == "shuffle" || legacyMusic == "order")) s.mode = Mode::Music;
    out = s;
    return true;
}

std::wstring ExeDir() {
    // The exe cannot move while it runs, and Log() asks for the directory on every line.
    static const std::wstring dir = [] {
        std::wstring path = ExePath();
        size_t slash = path.find_last_of(L'\\');
        return slash == std::wstring::npos ? path : path.substr(0, slash);
    }();
    return dir;
}

namespace {
bool g_portableBuild = false;
}  // namespace

void SetPortableBuild(bool on) { g_portableBuild = on; }

bool IsPortable() {
    if (g_portableBuild) return true;
    // The marker is looked up once, like the exe folder: Log() asks for the paths on every line.
    static const bool marker = GetFileAttributesW((ExeDir() + L"\\portable.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
    return marker;
}

std::wstring DataDir() {
    if (!g_dataOverride.empty()) return g_dataOverride;
    // Resolved on first use, after SetPortableBuild at start; the overrides above stay live for tests.
    static const std::wstring dir = [] {
        if (IsPortable()) return ExeDir();
        // A failed folder lookup must not turn into a path at the drive root.
        std::wstring base = KnownFolder(FOLDERID_RoamingAppData);
        return base.empty() ? ExeDir() : base + L"\\AdMutify";
    }();
    return dir;
}

std::wstring LogDir() {
    if (!g_logOverride.empty()) return g_logOverride;
    static const std::wstring dir = [] {
        if (IsPortable()) return ExeDir() + L"\\logs";
        std::wstring base = KnownFolder(FOLDERID_LocalAppData);
        return base.empty() ? ExeDir() + L"\\logs" : base + L"\\AdMutify\\logs";
    }();
    return dir;
}

std::wstring SettingsPath() { return DataDir() + L"\\settings.ini"; }
std::wstring StatePath() { return DataDir() + L"\\state.ini"; }

void SetDataDirOverride(const std::wstring& path) { g_dataOverride = path; }
void SetLogDirOverride(const std::wstring& path) { g_logOverride = path; }

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    out.clear();
    char buf[4096];
    DWORD got = 0;
    BOOL ok = TRUE;
    while ((ok = ReadFile(h, buf, sizeof(buf), &got, nullptr)) != FALSE && got > 0) out.append(buf, got);
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (!ok) {
        out.clear();
        SetLastError(err);
        return false;
    }
    return true;
}

bool EnsureDirectory(const std::wstring& path) {
    int r = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    if (r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS) return true;
    // The shell call returns the error code without setting the thread's last error.
    SetLastError(static_cast<DWORD>(r));
    return false;
}

namespace {

// Temp files of writers that died between create and rename; a live writer finishes within moments.
void RemoveStaleTempFiles() {
    std::wstring dir = DataDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.ini.*.tmp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now;
    now.LowPart = nowFt.dwLowDateTime;
    now.HighPart = nowFt.dwHighDateTime;
    const ULONGLONG kOneHour = 36000000000ULL;  // 100 ns units
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULARGE_INTEGER written;
        written.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        written.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        if (now.QuadPart > written.QuadPart && now.QuadPart - written.QuadPart > kOneHour)
            DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

}  // namespace

bool WriteFileAtomic(const std::wstring& path, std::string_view data, unsigned long* lastError) {
    auto fail = [&](DWORD err) {
        if (lastError) *lastError = err;
        return false;
    };
    size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos && !EnsureDirectory(path.substr(0, slash))) return fail(GetLastError());

    // Unique per call so two writers (threads or processes) never share a temp file.
    static std::atomic<unsigned> counter{0};
    std::wstring tmp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) +
                       L"." + std::to_wstring(counter.fetch_add(1)) + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail(GetLastError());
    DWORD written = 0;
    bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() &&
              FlushFileBuffers(h);
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (ok) {
        // A scanner or sync client may hold the target for a moment right after it was written.
        for (int attempt = 0;; ++attempt) {
            if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
            err = GetLastError();
            if (attempt >= 4 || (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED)) {
                ok = false;
                break;
            }
            Sleep(20);
        }
    }
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return fail(err);
    }
    if (lastError) *lastError = 0;
    return true;
}

bool SaveSettings(const Settings& s, unsigned long* lastError) {
    if (g_newerVersionSeen || g_readFailedSeen) {
        if (lastError) *lastError = 0;
        return true;
    }
    return WriteFileAtomic(SettingsPath(), FormatSettings(s), lastError);
}

LoadResult LoadOrCreateSettings() {
    LoadResult r;
    g_newerVersionSeen = false;
    g_readFailedSeen = false;
    RemoveStaleTempFiles();
    std::string text;
    if (!ReadFileUtf8(SettingsPath(), text)) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
            // Locked or unreadable right now: run on defaults and leave the file alone.
            r.readFailed = true;
            r.readError = err;
            g_readFailedSeen = true;
            return r;
        }
        r.created = true;
        r.saveFailed = !SaveSettings(r.settings, &r.saveError);
        return r;
    }
    if (!ParseSettings(text, r.settings)) {
        r.newerVersion = true;
        g_newerVersionSeen = true;
    }
    return r;
}

bool SettingsReadFailed() { return g_readFailedSeen; }

bool RetrySettingsRead(Settings& fromFile, unsigned long* error) {
    fromFile = Settings{};
    if (!g_readFailedSeen) return true;
    std::string text;
    if (!ReadFileUtf8(SettingsPath(), text)) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
            if (error) *error = err;
            return false;
        }
    } else if (!ParseSettings(text, fromFile)) {
        g_newerVersionSeen = true;
    }
    g_readFailedSeen = false;
    return true;
}

bool SettingsNewerVersion() { return g_newerVersionSeen; }

Settings MergeSettingsChange(const Settings& fromFile, const Settings& shown, const Settings& current) {
    Settings out = fromFile;
    if (current.enabled != shown.enabled) out.enabled = current.enabled;
    if (current.mode != shown.mode) out.mode = current.mode;
    if (current.lowerPercent != shown.lowerPercent) out.lowerPercent = current.lowerPercent;
    if (current.hideTray != shown.hideTray) out.hideTray = current.hideTray;
    if (current.language != shown.language) out.language = current.language;
    if (current.darkMenus != shown.darkMenus) out.darkMenus = current.darkMenus;
    if (current.media != shown.media) out.media = current.media;
    if (current.mediaPercent != shown.mediaPercent) out.mediaPercent = current.mediaPercent;
    if (current.adMusic != shown.adMusic) out.adMusic = current.adMusic;
    if (current.adMusicPath != shown.adMusicPath) out.adMusicPath = current.adMusicPath;
    if (current.adMusicVolume != shown.adMusicVolume) out.adMusicVolume = current.adMusicVolume;
    if (current.trace != shown.trace) out.trace = current.trace;
    return out;
}
