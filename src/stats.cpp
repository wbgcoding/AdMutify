#include "stats.h"

#include <windows.h>

#include <charconv>

#include "crash.h"
#include "settings.h"

uint64_t SipHash24(const uint8_t key[16], const uint8_t* data, size_t len) {
    auto rotl = [](uint64_t x, int b) { return (x << b) | (x >> (64 - b)); };
    auto load = [](const uint8_t* p, size_t n) {
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
        return v;
    };
    const uint64_t k0 = load(key, 8), k1 = load(key + 8, 8);
    uint64_t v0 = k0 ^ 0x736f6d6570736575ull, v1 = k1 ^ 0x646f72616e646f6dull;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ull, v3 = k1 ^ 0x7465646279746573ull;
    auto round = [&] {
        v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32);
        v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;
        v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;
        v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32);
    };
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        const uint64_t m = load(data + i, 8);
        v3 ^= m;
        round();
        round();
        v0 ^= m;
    }
    const uint64_t last = load(data + i, len - i) | (static_cast<uint64_t>(len) << 56);
    v3 ^= last;
    round();
    round();
    v0 ^= last;
    v2 ^= 0xff;
    round();
    round();
    round();
    round();
    return v0 ^ v1 ^ v2 ^ v3;
}

namespace {

uint64_t g_adsMuted = 0;
// The file was newer or unreadable: counting goes on in memory, the file is left alone.
bool g_keepFile = false;

// Tamper protection, not security: the source is public, so this only stops casual editing of the counter file.
// `data` holds the count XOR a mask (no plain number in the file); `check` is a keyed hash of the count.
const uint8_t kMaskKey[16] = {0x5a, 0xc1, 0x3e, 0x97, 0x08, 0xd4, 0x6b, 0xf2, 0x1d, 0xa9, 0x74, 0x30, 0xe6, 0x8b, 0x4f, 0x02};
const uint8_t kCheckKey[16] = {0xb3, 0x2f, 0x68, 0xd1, 0x95, 0x0a, 0xe7, 0x4c, 0x7e, 0x13, 0xca, 0x59, 0x86, 0xf0, 0x21, 0x3d};
const char kMaskSeed[] = "AdMutify stats data";

uint64_t DataMask() { return SipHash24(kMaskKey, reinterpret_cast<const uint8_t*>(kMaskSeed), sizeof(kMaskSeed) - 1); }

uint64_t CountCheck(uint64_t count) {
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>(count >> (8 * i));
    return SipHash24(kCheckKey, bytes, sizeof(bytes));
}

std::string Hex16(uint64_t v) {
    static const char digits[] = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[static_cast<size_t>(i)] = digits[v & 0xF];
    return s;
}

// Exactly 16 lowercase hex digits.
bool ParseHex16(std::string_view s, uint64_t& out) {
    if (s.size() != 16) return false;
    uint64_t v = 0;
    for (char c : s) {
        unsigned d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a') + 10;
        else return false;
        v = (v << 4) | d;
    }
    out = v;
    return true;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

}  // namespace

std::string FormatStats(uint64_t adsMuted) {
    return "version=2\r\ndata=" + Hex16(adsMuted ^ DataMask()) + "\r\ncheck=" + Hex16(CountCheck(adsMuted)) + "\r\n";
}

StatsParse ParseStats(std::string_view text, uint64_t& adsMuted) {
    adsMuted = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    uint64_t plain = 0;
    bool plainOk = true;
    bool v2 = false;
    bool haveData = false, haveCheck = false, dataOk = false, checkOk = false;
    uint64_t data = 0, check = 0;
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
            if (r.ec == std::errc() && r.ptr == val.data() + val.size()) {
                if (v > 2) return StatsParse::Newer;
                if (v == 2) v2 = true;
            }
        } else if (key == "ads_muted") {
            uint64_t v = 0;
            auto r = std::from_chars(val.data(), val.data() + val.size(), v);
            if (r.ec == std::errc() && r.ptr == val.data() + val.size()) plain = v;
            else plainOk = false;
        } else if (key == "data") {
            haveData = true;
            dataOk = ParseHex16(val, data);
        } else if (key == "check") {
            haveCheck = true;
            checkOk = ParseHex16(val, check);
        }
    }
    if (v2) {
        if (!haveData || !haveCheck || !dataOk || !checkOk) return StatsParse::Tampered;
        const uint64_t count = data ^ DataMask();
        if (CountCheck(count) != check) return StatsParse::Tampered;
        adsMuted = count;
        return StatsParse::Ok;
    }
    if (!plainOk) return StatsParse::Unreadable;
    adsMuted = plain;
    return StatsParse::Ok;
}

bool CountsAsNewAdMute(bool adWanted, bool sessionsHeld, bool adAlreadyCounted) {
    return adWanted && sessionsHeld && !adAlreadyCounted;
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
    switch (ParseStats(text, v)) {
        case StatsParse::Ok:
            g_adsMuted = v;
            // A file from an older release (plain number) is rewritten at once in the current format.
            if (text != FormatStats(v)) WriteFileAtomic(StatsPath(), FormatStats(v));
            break;
        case StatsParse::Tampered:
            // Starts over at 0; the file is replaced at the next count.
            Log("stats_tampered");
            break;
        case StatsParse::Newer:
        case StatsParse::Unreadable:
            g_keepFile = true;
            break;
    }
}

uint64_t StatsAdsMuted() { return g_adsMuted; }

void StatsCountAdMute() {
    ++g_adsMuted;
    if (!g_keepFile) WriteFileAtomic(StatsPath(), FormatStats(g_adsMuted));
}
