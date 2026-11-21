#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Lifetime counter of muted ads, kept in stats.ini next to the settings.
// Format version 2 stores the count masked plus a keyed check, so a hand edit is noticed (see stats.cpp).
enum class StatsParse {
    Ok,          // `adsMuted` holds the count (version 2, or a plain version 1 file from an older release)
    Newer,       // file version above 2: `adsMuted` is 0 and the file must stay as it is
    Unreadable,  // version 1 file with a garbled count: `adsMuted` is 0 and the file must stay as it is
    Tampered     // version 2 file with missing, garbled or mismatching data/check: `adsMuted` is 0, the file may be replaced
};

// Always writes version 2.
std::string FormatStats(uint64_t adsMuted);
StatsParse ParseStats(std::string_view text, uint64_t& adsMuted);
// SipHash-2-4 with a 128-bit key (public-domain reference algorithm); exposed for the test vector.
uint64_t SipHash24(const uint8_t key[16], const uint8_t* data, size_t len);
// True when an ad reaction was just applied for a new ad: counted once per break, never for a re-apply or a retry.
// adAlreadyCounted: this ad was counted before (turning AdMutify off and on during it applies again).
bool CountsAsNewAdMute(bool adAlreadyApplied, bool adWanted, bool adAlreadyCounted = false);

std::wstring StatsPath();
// Reads the file; a missing file starts at 0, an unreadable or newer one starts at 0 and is never overwritten,
// a tampered one starts at 0 and is replaced at the next count, a version 1 file is converted at once.
void StatsLoad();
uint64_t StatsAdsMuted();
// Adds one and saves at once.
void StatsCountAdMute();
