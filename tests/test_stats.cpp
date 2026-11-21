#include <windows.h>

#include <initializer_list>
#include <string>

#include "settings.h"
#include "stats.h"
#include "test.h"

TEST(stats_siphash_reference_vector) {
    // Official SipHash-2-4 test vector: key 00..0f, message 00..07.
    uint8_t key[16], msg[8];
    for (int i = 0; i < 16; ++i) key[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 8; ++i) msg[i] = static_cast<uint8_t>(i);
    CHECK(SipHash24(key, msg, 8) == 0x93f5f5799a932462ull);
    // Empty message vector.
    CHECK(SipHash24(key, msg, 0) == 0x726fdb47dd0e0e31ull);
}

TEST(stats_format_parse_roundtrip) {
    uint64_t back = 99;
    for (uint64_t n : {0ull, 1ull, 1234ull, 18446744073709551615ull}) {
        CHECK(ParseStats(FormatStats(n), back) == StatsParse::Ok);
        CHECK(back == n);
    }
    // Layout: version 2, 16 lowercase hex digits each, no plain number.
    const std::string f = FormatStats(7);
    CHECK(f.size() == std::string("version=2\r\ndata=\r\ncheck=\r\n").size() + 32);
    CHECK(f.rfind("version=2\r\ndata=", 0) == 0);
    CHECK(f.find("ads_muted") == std::string::npos);
    CHECK(f.find_first_not_of("0123456789abcdef=\r\nversiondatcheck") == std::string::npos);
    CHECK(FormatStats(7) != FormatStats(8));
}

TEST(stats_v1_file_still_read) {
    uint64_t v = 99;
    CHECK(ParseStats("version=1\r\nads_muted=5\r\n", v) == StatsParse::Ok);
    CHECK(v == 5);
    CHECK(ParseStats("\xEF\xBB\xBF" "version=1\nads_muted=5\n", v) == StatsParse::Ok);
    CHECK(v == 5);
    CHECK(ParseStats("# note\r\n ads_muted = 8 \r\n", v) == StatsParse::Ok);
    CHECK(v == 8);
    // Missing key or empty file: nothing counted yet.
    CHECK(ParseStats("version=1\r\n", v) == StatsParse::Ok);
    CHECK(v == 0);
    CHECK(ParseStats("", v) == StatsParse::Ok);
    CHECK(v == 0);
    // Garbage count keeps the file for the user: not usable.
    CHECK(ParseStats("version=1\r\nads_muted=abc\r\n", v) == StatsParse::Unreadable);
    CHECK(v == 0);
    CHECK(ParseStats("ads_muted=-3\r\n", v) == StatsParse::Unreadable);
    CHECK(ParseStats("ads_muted=99999999999999999999999\r\n", v) == StatsParse::Unreadable);
    CHECK(ParseStats("ads_muted=\r\n", v) == StatsParse::Unreadable);
    // Unrelated garbage lines and unknown keys are ignored.
    CHECK(ParseStats("\x01\x02 junk\r\nads_muted=4\r\nother=1\r\n", v) == StatsParse::Ok);
    CHECK(v == 4);
}

TEST(stats_v2_bom_and_layout_tolerance) {
    uint64_t v = 99;
    const std::string f = FormatStats(321);
    CHECK(ParseStats("\xEF\xBB\xBF" + f, v) == StatsParse::Ok);
    CHECK(v == 321);
    // LF only, no final break, other key order, comments.
    std::string lf;
    for (char c : f) if (c != '\r') lf += c;
    CHECK(ParseStats(lf, v) == StatsParse::Ok);
    CHECK(v == 321);
    CHECK(ParseStats("# x\n" + lf.substr(lf.find("data=")) + "version=2", v) == StatsParse::Ok);
    CHECK(v == 321);
}

TEST(stats_v2_edit_is_tampered) {
    for (uint64_t n : {0ull, 1234ull, 18446744073709551615ull}) {
        const std::string f = FormatStats(n);
        const size_t dataPos = f.find("data=") + 5;
        const size_t checkPos = f.find("check=") + 6;
        uint64_t v = 99;
        // One changed hex digit in data or in check.
        for (size_t pos : {dataPos, dataPos + 15, checkPos, checkPos + 15}) {
            std::string e = f;
            e[pos] = e[pos] == '0' ? '1' : '0';
            CHECK(ParseStats(e, v) == StatsParse::Tampered);
            CHECK(v == 0);
        }
        // Uppercase hex is not what we write.
        std::string up = f;
        for (size_t i = dataPos; i < dataPos + 16; ++i) up[i] = static_cast<char>(toupper(up[i]));
        if (up != f) CHECK(ParseStats(up, v) == StatsParse::Tampered);
    }
    // Data of another valid count without its check.
    const std::string a = FormatStats(5), b = FormatStats(6);
    const std::string mixed = a.substr(0, a.find("check=")) + b.substr(b.find("check="));
    uint64_t v = 99;
    CHECK(ParseStats(mixed, v) == StatsParse::Tampered);
    CHECK(v == 0);
    const std::string mixed2 = b.substr(0, b.find("check=")) + a.substr(a.find("check="));
    CHECK(ParseStats(mixed2, v) == StatsParse::Tampered);
    // Missing or garbled lines, a plain count in a version 2 file.
    CHECK(ParseStats("version=2\r\n", v) == StatsParse::Tampered);
    CHECK(ParseStats("version=2\r\nads_muted=5\r\n", v) == StatsParse::Tampered);
    CHECK(ParseStats(a.substr(0, a.find("check=")), v) == StatsParse::Tampered);
    CHECK(ParseStats("version=2\r\n" + a.substr(a.find("check=")), v) == StatsParse::Tampered);
    CHECK(ParseStats("version=2\r\ndata=zz\r\ncheck=\r\n", v) == StatsParse::Tampered);
    CHECK(ParseStats("version=2\r\ndata=0000000000000000\r\ncheck=0000000000000000\r\n", v) == StatsParse::Tampered);
}

TEST(stats_newer_version_not_usable) {
    uint64_t v = 99;
    CHECK(ParseStats("version=3\r\nads_muted=5\r\n", v) == StatsParse::Newer);
    CHECK(v == 0);
    CHECK(ParseStats("ads_muted=5\r\nversion=3\r\n", v) == StatsParse::Newer);
    CHECK(ParseStats(FormatStats(9) + "version=3\r\n", v) == StatsParse::Newer);
    CHECK(v == 0);
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
    const std::string newer = "version=3\r\nads_muted=41\r\nfuture=1\r\n";
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

TEST(stats_v1_file_migrates_on_load) {
    CHECK(WriteFileAtomic(StatsPath(), "version=1\r\nads_muted=41\r\n"));
    StatsLoad();
    CHECK(StatsAdsMuted() == 41);
    std::string text;
    CHECK(ReadFileUtf8(StatsPath(), text));
    CHECK(text == FormatStats(41));
    CHECK(text.find("ads_muted") == std::string::npos);
    StatsCountAdMute();
    StatsLoad();
    CHECK(StatsAdsMuted() == 42);
    DeleteFileW(StatsPath().c_str());
    StatsLoad();
}

TEST(stats_tampered_file_restarts_and_is_replaced) {
    std::string edited = FormatStats(500);
    const size_t pos = edited.find("data=") + 5;
    edited[pos] = edited[pos] == 'a' ? 'b' : 'a';
    CHECK(WriteFileAtomic(StatsPath(), edited));
    StatsLoad();
    CHECK(StatsAdsMuted() == 0);
    // Not kept: the next count writes a valid file again.
    StatsCountAdMute();
    std::string text;
    CHECK(ReadFileUtf8(StatsPath(), text));
    CHECK(text == FormatStats(1));
    StatsLoad();
    CHECK(StatsAdsMuted() == 1);
    DeleteFileW(StatsPath().c_str());
    StatsLoad();
}

TEST(stats_toggle_during_ad_counts_once) {
    // First apply of the break counts; off and on again during the same break does not.
    CHECK(CountsAsNewAdMute(false, true, false));
    CHECK(!CountsAsNewAdMute(false, true, true));
    CHECK(!CountsAsNewAdMute(true, true, false));
}
