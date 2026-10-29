#include <windows.h>

#include <initializer_list>
#include <string>

#include "settings.h"
#include "stats.h"
#include "test.h"

TEST(stats_format_parse_roundtrip) {
    uint64_t back = 99;
    CHECK(ParseStats(FormatStats(0), back));
    CHECK(back == 0);
    CHECK(ParseStats(FormatStats(12345), back));
    CHECK(back == 12345);
    CHECK(ParseStats(FormatStats(18446744073709551615ull), back));
    CHECK(back == 18446744073709551615ull);
    CHECK(FormatStats(7) == "version=1\r\nads_muted=7\r\n");
}

TEST(stats_parse_tolerant_and_strict) {
    uint64_t v = 99;
    CHECK(ParseStats("\xEF\xBB\xBF" "version=1\nads_muted=5\n", v));
    CHECK(v == 5);
    CHECK(ParseStats("# note\r\n ads_muted = 8 \r\n", v));
    CHECK(v == 8);
    // Missing key or empty file: nothing counted yet.
    CHECK(ParseStats("version=1\r\n", v));
    CHECK(v == 0);
    CHECK(ParseStats("", v));
    CHECK(v == 0);
    // Garbage count keeps the file for the user: not usable.
    CHECK(!ParseStats("version=1\r\nads_muted=abc\r\n", v));
    CHECK(v == 0);
    CHECK(!ParseStats("ads_muted=-3\r\n", v));
    CHECK(!ParseStats("ads_muted=99999999999999999999999\r\n", v));
    CHECK(!ParseStats("ads_muted=\r\n", v));
    // Unrelated garbage lines and unknown keys are ignored.
    CHECK(ParseStats("\x01\x02 junk\r\nads_muted=4\r\nother=1\r\n", v));
    CHECK(v == 4);
}

TEST(stats_newer_version_not_usable) {
    uint64_t v = 99;
    CHECK(!ParseStats("version=2\r\nads_muted=5\r\n", v));
    CHECK(v == 0);
    CHECK(!ParseStats("ads_muted=5\r\nversion=3\r\n", v));
}

TEST(stats_count_once_per_break) {
    // Walks a series of evaluations like the app does: `want` is the reaction wanted at that moment.
    enum Want { None, Ad, Video };
    auto run = [](std::initializer_list<Want> series) {
        int counted = 0;
        bool applied = false;
        Want appliedWant = None;
        for (Want want : series) {
            const bool adWasApplied = applied && appliedWant == Ad;
            if (want == None) {
                applied = false;
                appliedWant = None;
                continue;
            }
            if (!applied || appliedWant != want) {
                applied = true;
                appliedWant = want;
            }
            if (CountsAsNewAdMute(adWasApplied, want == Ad)) ++counted;
        }
        return counted;
    };
    // Several evaluations or re-applies in one break count once.
    CHECK(run({Ad, Ad, Ad}) == 1);
    CHECK(run({None, Ad, Ad, None}) == 1);
    // Two breaks count twice.
    CHECK(run({Ad, Ad, None, Ad}) == 2);
    // The video reaction never counts.
    CHECK(run({Video, Video, None, Video}) == 0);
    // An ad that takes over from a video reaction counts once.
    CHECK(run({Video, Ad, Ad}) == 1);
    CHECK(run({}) == 0);
}

TEST(stats_file_count_and_persist) {
    DeleteFileW(StatsPath().c_str());
    StatsLoad();
    CHECK(StatsAdsMuted() == 0);
    StatsCountAdMute();
    StatsCountAdMute();
    CHECK(StatsAdsMuted() == 2);
    std::string text;
    CHECK(ReadFileUtf8(StatsPath(), text));
    CHECK(text == FormatStats(2));
    StatsLoad();
    CHECK(StatsAdsMuted() == 2);
    DeleteFileW(StatsPath().c_str());
}

TEST(stats_newer_and_garbage_files_stay) {
    const std::string newer = "version=2\r\nads_muted=41\r\nfuture=1\r\n";
    CHECK(WriteFileAtomic(StatsPath(), newer));
    StatsLoad();
    CHECK(StatsAdsMuted() == 0);
    StatsCountAdMute();
    std::string after;
    CHECK(ReadFileUtf8(StatsPath(), after));
    CHECK(after == newer);

    const std::string garbage = "ads_muted=lots\r\n";
    CHECK(WriteFileAtomic(StatsPath(), garbage));
    StatsLoad();
    StatsCountAdMute();
    CHECK(ReadFileUtf8(StatsPath(), after));
    CHECK(after == garbage);
    DeleteFileW(StatsPath().c_str());
    StatsLoad();
}

TEST(stats_toggle_during_ad_counts_once) {
    // First apply of the break counts; off and on again during the same break does not.
    CHECK(CountsAsNewAdMute(false, true, false));
    CHECK(!CountsAsNewAdMute(false, true, true));
    CHECK(!CountsAsNewAdMute(true, true, false));
}
