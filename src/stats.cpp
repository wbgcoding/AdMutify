#include "stats.h"

#include <windows.h>

#include <charconv>

#include "settings.h"

namespace {

uint64_t g_adsMuted = 0;
// The file was newer or unreadable: counting goes on in memory, the file is left alone.
bool g_keepFile = false;

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

}  // namespace

std::string FormatStats(uint64_t adsMuted) {
    return "version=1\r\nads_muted=" + std::to_string(adsMuted) + "\r\n";
}

bool ParseStats(std::string_view text, uint64_t& adsMuted) {
    adsMuted = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    uint64_t count = 0;
    bool ok = true;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = Trim(text.substr(pos, end - pos));
        pos = end + 1;
        size_t eq = line.find('=');
        if (line.empty() || line.front() == '#' || eq == std::string_view::npos) continue;
        std::string_view key = Trim(line.substr(0, eq));
        std::string_view val = Trim(line.substr(eq + 1));
        if (key == "version") {
            unsigned v = 0;
            auto r = std::from_chars(val.data(), val.data() + val.size(), v);
            if (r.ec == std::errc() && r.ptr == val.data() + val.size() && v > 1) return false;
        } else if (key == "ads_muted") {
            uint64_t v = 0;
            auto r = std::from_chars(val.data(), val.data() + val.size(), v);
            if (r.ec == std::errc() && r.ptr == val.data() + val.size()) count = v;
            else ok = false;
        }
    }
    if (!ok) return false;
    adsMuted = count;
    return true;
}

bool CountsAsNewAdMute(bool adAlreadyApplied, bool adWanted, bool adAlreadyCounted) {
    return adWanted && !adAlreadyApplied && !adAlreadyCounted;
}

std::wstring StatsPath() { return DataDir() + L"\\stats.ini"; }

void StatsLoad() {
    g_adsMuted = 0;
    g_keepFile = false;
    std::string text;
    bool read = false;
    DWORD err = 0;
    // A scanner may hold the file for a moment right at start; a few short retries keep the count.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if ((read = ReadFileUtf8(StatsPath(), text))) break;
        err = GetLastError();
        if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) break;
        Sleep(50);
    }
    if (!read) {
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) g_keepFile = true;
        return;
    }
    uint64_t v = 0;
    if (!ParseStats(text, v)) {
        g_keepFile = true;
        return;
    }
    g_adsMuted = v;
}

uint64_t StatsAdsMuted() { return g_adsMuted; }

void StatsCountAdMute() {
    ++g_adsMuted;
    if (!g_keepFile) WriteFileAtomic(StatsPath(), FormatStats(g_adsMuted));
}
