// Edge tests for the stats file format and the Spotify process name helpers.
#include <windows.h>

#include <cstdint>
#include <string>

#include "spotify_watch.h"
#include "stats.h"
#include "test.h"

TEST(stats_edges_count_limits_and_roundtrip) {
    uint64_t v = 7;
    const uint64_t big = UINT64_MAX;
    CHECK(ParseStats(FormatStats(big), v) == StatsParse::Ok);
    CHECK(v == big);
    CHECK(ParseStats(FormatStats(0), v) == StatsParse::Ok);
    CHECK(v == 0);
    CHECK(ParseStats("ads_muted=18446744073709551616\r\n", v) == StatsParse::Unreadable);
    CHECK(v == 0);
    CHECK(ParseStats("ads_muted=0007\r\n", v) == StatsParse::Ok);
    CHECK(v == 7);
}

TEST(stats_edges_odd_values_are_not_usable) {
    uint64_t v = 7;
    CHECK(ParseStats("ads_muted=+5\r\n", v) == StatsParse::Unreadable);
    CHECK(ParseStats("ads_muted=5 6\r\n", v) == StatsParse::Unreadable);
    CHECK(ParseStats("ads_muted=5.0\r\n", v) == StatsParse::Unreadable);
    CHECK(ParseStats("ads_muted=0x10\r\n", v) == StatsParse::Unreadable);
    CHECK(ParseStats("ads_muted=\xD9\xA5\r\n", v) == StatsParse::Unreadable);
    CHECK(v == 0);
    // A bad value anywhere makes the whole file unusable, even after a good one.
    CHECK(ParseStats("ads_muted=5\r\nads_muted=x\r\n", v) == StatsParse::Unreadable);
    CHECK(v == 0);
}

TEST(stats_edges_duplicates_and_layout) {
    uint64_t v = 0;
    // The last good value wins; blank lines, tabs and a missing final break are fine.
    CHECK(ParseStats("ads_muted=5\r\n\r\nads_muted=9", v) == StatsParse::Ok);
    CHECK(v == 9);
    CHECK(ParseStats("\tads_muted\t=\t12\t\n", v) == StatsParse::Ok);
    CHECK(v == 12);
    CHECK(ParseStats("ads_muted=3\n", v) == StatsParse::Ok);
    CHECK(v == 3);
    // A line without a separator or a commented-out line does nothing.
    CHECK(ParseStats("ads_muted 5\r\n# ads_muted=8\r\n", v) == StatsParse::Ok);
    CHECK(v == 0);
    CHECK(ParseStats(std::string("ads_muted=4\0junk", 16), v) == StatsParse::Unreadable);
}

TEST(stats_edges_version_values) {
    uint64_t v = 0;
    // Version 0 and unreadable versions read as a plain file; only a higher version blocks it.
    CHECK(ParseStats("version=0\r\nads_muted=2\r\n", v) == StatsParse::Ok);
    CHECK(v == 2);
    CHECK(ParseStats("version=abc\r\nads_muted=2\r\n", v) == StatsParse::Ok);
    CHECK(v == 2);
    CHECK(ParseStats("version=1x\r\nads_muted=2\r\n", v) == StatsParse::Ok);
    CHECK(ParseStats("version=\r\nads_muted=2\r\n", v) == StatsParse::Ok);
    CHECK(ParseStats("version=3\r\n", v) == StatsParse::Newer);
    CHECK(ParseStats("version=4294967295\r\n", v) == StatsParse::Newer);
    CHECK(v == 0);
    // A version too large for the number type reads as unreadable and is ignored.
    CHECK(ParseStats("version=99999999999\r\nads_muted=2\r\n", v) == StatsParse::Ok);
}

TEST(stats_edges_count_rule_table) {
    for (int a = 0; a < 2; ++a)
        for (int w = 0; w < 2; ++w)
            for (int c = 0; c < 2; ++c) CHECK(CountsAsNewAdMute(w != 0, a != 0, c != 0) == (w && a && !c));
}

TEST(spotify_edges_process_name_forms) {
    SetTargetProcessName(L"Spotify.exe");
    // Only the part after the last separator counts, with either kind of separator.
    CHECK(IsSpotifyProcessName(L"\\Spotify.exe"));
    CHECK(IsSpotifyProcessName(L"/Spotify.exe"));
    CHECK(IsSpotifyProcessName(L"C:\\a/b\\c/SPOTIFY.exe"));
    CHECK(IsSpotifyProcessName(L"\\\\server\\share\\Spotify.exe"));
    CHECK(!IsSpotifyProcessName(L"C:\\Spotify.exe\\"));
    CHECK(!IsSpotifyProcessName(L"C:\\Spotify.exe\\x"));
    CHECK(!IsSpotifyProcessName(L"Spotify.exe "));
    CHECK(!IsSpotifyProcessName(L" Spotify.exe"));
    CHECK(!IsSpotifyProcessName(L"Spotify"));
    CHECK(!IsSpotifyProcessName(L"Spotify.exe.bak"));
    CHECK(!IsSpotifyProcessName(L"\\"));
    CHECK(!IsSpotifyProcessName(std::wstring(L"Spotify.exe\0x", 13)));
    CHECK(!IsSpotifyProcessName(std::wstring(10000, L'x') + L"\\x.exe"));
}

TEST(spotify_edges_target_name_switch) {
    SetTargetProcessName(L"Rig.exe");
    CHECK(IsSpotifyProcessName(L"C:\\rig\\RIG.EXE"));
    CHECK(!IsSpotifyProcessName(L"Spotify.exe"));
    // Unknown and invalid pids are no target, with any name set.
    CHECK(!IsTargetProcess(0));
    CHECK(!IsTargetProcess(0xFFFFFFF0u));
    CHECK(!IsTargetProcess(GetCurrentProcessId()));
    // An empty name matches only an empty file name.
    SetTargetProcessName(L"");
    CHECK(!IsSpotifyProcessName(L"Spotify.exe"));
    CHECK(IsSpotifyProcessName(L"C:\\dir\\"));
    SetTargetProcessName(L"Spotify.exe");
    CHECK(IsSpotifyProcessName(L"Spotify.exe"));
}
