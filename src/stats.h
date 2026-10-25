#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Lifetime counter of muted ads, kept in stats.ini next to the settings.
std::string FormatStats(uint64_t adsMuted);
// Returns false for a newer file version or an unreadable count; `adsMuted` is 0 then and the file must stay as it is.
bool ParseStats(std::string_view text, uint64_t& adsMuted);
// True when an ad reaction was just applied for a new ad: counted once per break, never for a re-apply or a retry.
bool CountsAsNewAdMute(bool adAlreadyApplied, bool adWanted);

std::wstring StatsPath();
// Reads the file; a missing file starts at 0, an unreadable one starts at 0 and is never overwritten.
void StatsLoad();
uint64_t StatsAdsMuted();
// Adds one and saves at once.
void StatsCountAdMute();
