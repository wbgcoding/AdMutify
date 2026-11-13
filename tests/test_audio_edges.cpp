// Edge and error-path tests for the pure parts of audio_control: volume math, override detection, record age and
// the state file format. No audio endpoint is touched.
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "audio_control.h"
#include "test.h"

namespace {

SavedSession Rec(Mode mode, float oldVol, float appliedVol) {
    SavedSession s;
    s.deviceId = L"dev";
    s.instanceId = L"inst";
    s.sessionId = L"sess";
    s.mode = mode;
    s.oldVolume = oldVol;
    s.appliedVolume = appliedVol;
    return s;
}

}  // namespace

TEST(audio_lowered_volume_extremes) {
    CHECK(LoweredVolume(0.8f, -5) == 0.0f);
    CHECK(LoweredVolume(0.8f, INT_MIN) == 0.0f);
    CHECK(std::fabs(LoweredVolume(0.8f, INT_MAX) - 0.8f) < 1e-6f);
    CHECK(LoweredVolume(0.0f, 50) == 0.0f);
    CHECK(LoweredVolume(1.0f, 0) == 0.0f);
    CHECK(std::fabs(LoweredVolume(1.0f, 1) - 0.01f) < 1e-6f);
    CHECK(std::fabs(LoweredVolume(1.0f, 99) - 0.99f) < 1e-6f);
    // Never louder than the old volume, for every percent value.
    for (int p = -10; p <= 110; ++p) {
        const float v = LoweredVolume(0.7f, p);
        CHECK(v >= 0.0f);
        CHECK(v <= 0.7f + 1e-6f);
    }
}

TEST(audio_user_override_tolerance_and_directions) {
    // Mute mode compares only the mute flag, both directions.
    CHECK(UserOverrode(Mode::Mute, false, 1.0f, true, 1.0f));
    CHECK(!UserOverrode(Mode::Mute, false, 1.0f, false, 0.2f));
    CHECK(!UserOverrode(Mode::Mute, true, 0.3f, true, 0.9f));
    // Lower mode: a difference at the tolerance is still our own value, clearly above it is an override.
    CHECK(!UserOverrode(Mode::Lower, false, 0.5f, false, 0.5005f));
    CHECK(UserOverrode(Mode::Lower, false, 0.5f, false, 0.502f));
    CHECK(UserOverrode(Mode::Lower, false, 0.5f, false, 0.498f));
    // Mute state does not matter in lower mode, in either direction.
    CHECK(!UserOverrode(Mode::Lower, true, 0.5f, false, 0.5f));
    CHECK(!UserOverrode(Mode::Lower, false, 0.5f, true, 0.5f));
}

TEST(audio_inherited_old_values_edges) {
    bool m = true;
    float v = 0.123f;
    // Mute sibling with a lowered live volume: the live mute flag alone decides in mute mode.
    SavedSession mute = Rec(Mode::Mute, 0.4f, 0.4f);
    mute.oldMute = true;
    CHECK(InheritedOld(Mode::Mute, mute, true, 0.0f, m, v));
    CHECK(m);
    CHECK(v == 0.4f);

    // A lower-mode sibling never answers a mute-mode session and the other way round.
    SavedSession lower = Rec(Mode::Lower, 0.8f, 0.16f);
    m = false;
    v = 0.0f;
    CHECK(!InheritedOld(Mode::Mute, lower, true, 0.16f, m, v));
    CHECK(!InheritedOld(Mode::Lower, mute, false, 0.4f, m, v));
    CHECK(!m && v == 0.0f);

    // Tolerance around the applied level.
    CHECK(InheritedOld(Mode::Lower, lower, false, 0.1604f, m, v));
    CHECK(v == 0.8f);
    v = 0.0f;
    CHECK(!InheritedOld(Mode::Lower, lower, false, 0.1612f, m, v));
    CHECK(v == 0.0f);
    // Muted live state without our level is not ours in lower mode.
    CHECK(!InheritedOld(Mode::Lower, lower, true, 0.8f, m, v));
}

TEST(audio_record_freshness_extremes) {
    CHECK(!RecordFresh(0, 0));
    CHECK(!RecordFresh(0, UINT64_MAX));
    CHECK(RecordFresh(1, 1));
    CHECK(RecordFresh(1, 1 + kPendingMaxAgeSeconds));
    CHECK(!RecordFresh(1, 2 + kPendingMaxAgeSeconds));
    CHECK(RecordFresh(UINT64_MAX, 5));
    CHECK(RecordFresh(100, 0));
    // A record saved a second ago is fresh whatever the epoch is.
    CHECK(RecordFresh(1790000000, 1790000001));
}

TEST(audio_filetime_to_unix_extremes) {
    const uint64_t epoch = 11644473600ULL * 10000000ULL;
    // The largest tick value must not wrap or throw.
    CHECK(UnixFromFileTimeTicks(UINT64_MAX) == UINT64_MAX / 10000000ULL - 11644473600ULL);
    CHECK(UnixFromFileTimeTicks(epoch + 9999999ULL) == 0);
    CHECK(UnixFromFileTimeTicks(epoch + 10000000ULL * 86400) == 86400);
    CHECK(UnixFromFileTimeTicks(1) == 0);
}

TEST(audio_state_file_empty_and_header_only) {
    std::vector<SavedSession> out(3);
    bool newer = true;
    CHECK(ParseStateFile("version=2\r\n", out, &newer));
    CHECK(out.empty());
    CHECK(!newer);
    // Unix line ends and a missing final line end work as well.
    CHECK(ParseStateFile("version=2", out));
    CHECK(out.empty());
    CHECK(ParseStateFile("version=2\nsession=d|i|s|m|1|0.5|0.5", out));
    CHECK(out.size() == 1);
    // The header may come after the records, and blank lines and unknown keys are ignored.
    CHECK(ParseStateFile("\r\nfoo=bar\r\nsession=d|i|s|m|0|0.25|0.25\r\n\r\nversion=2\r\n", out));
    CHECK(out.size() == 1);
    // Trailing spaces on a line are cut.
    CHECK(ParseStateFile("version=2  \r\nsession=d|i|s|l|1|0.5|0.25|7  \r\n", out));
    CHECK(out.size() == 1);
    if (out.size() == 1) CHECK(out[0].savedAt == 7);
}

TEST(audio_state_file_bad_versions) {
    std::vector<SavedSession> out;
    bool newer = false;
    const char* bad[] = {"version=\r\n", "version=-2\r\n", "version=2x\r\n", "version=02x\r\n", "version=two\r\n",
                         "version=2\r\nversion=1\r\n", "version=1\r\nversion=2\r\n"};
    for (const char* text : bad) {
        out.assign(2, SavedSession{});
        CHECK(!ParseStateFile(text, out, &newer));
        CHECK(out.empty());
    }
    // A version above 2 is flagged as newer, one below or unparsable is not.
    CHECK(!ParseStateFile("version=99\r\n", out, &newer));
    CHECK(newer);
    CHECK(!ParseStateFile("version=0\r\n", out, &newer));
    CHECK(!newer);
    // Records of a rejected file are dropped, even valid ones in front of the bad header.
    out.clear();
    CHECK(!ParseStateFile("session=d|i|s|m|1|0.5|0.5\r\nversion=3\r\n", out));
    CHECK(out.empty());
    // A null out-flag is allowed.
    CHECK(!ParseStateFile("version=7\r\n", out, nullptr));
}

TEST(audio_state_file_volume_boundaries) {
    std::vector<SavedSession> out;
    CHECK(ParseStateFile(
        "version=2\r\n"
        "session=d|i|s|l|0|0|1\r\n"              // both ends allowed
        "session=d|i|s|l|0|1.001|1\r\n"          // just above one
        "session=d|i|s|l|0|0.5|inf\r\n"          // infinite
        "session=d|i|s|l|0| 0.5|0.5\r\n"         // leading space
        "session=d|i|s|l|0|0.5|0,5\r\n"          // comma decimal
        "session=d|i|s|l|0|+0.5|0.5\r\n"         // explicit plus
        "session=d|i|s|m|1|1e-1|0.5\r\n",        // exponent form is fine
        out));
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].oldVolume == 0.0f && out[0].appliedVolume == 1.0f);
        CHECK(std::fabs(out[1].oldVolume - 0.1f) < 1e-6f);
    }
}

TEST(audio_state_file_bad_saved_time) {
    std::vector<SavedSession> out;
    CHECK(ParseStateFile(
        "version=2\r\n"
        "session=d|i|s|l|0|0.5|0.5|\r\n"                       // empty time
        "session=d|i|s|l|0|0.5|0.5|-5\r\n"                      // negative time
        "session=d|i|s|l|0|0.5|0.5|12x\r\n"                     // trailing garbage
        "session=d|i|s|l|0|0.5|0.5|99999999999999999999999\r\n"  // overflow
        "session=d|i|s|l|0|0.5|0.5|18446744073709551615\r\n",   // exactly the largest value
        out));
    CHECK(out.size() == 1);
    if (out.size() == 1) CHECK(out[0].savedAt == UINT64_MAX);
}

TEST(audio_state_file_roundtrip_edge_fields) {
    std::vector<SavedSession> in(4);
    in[0] = Rec(Mode::Mute, 0.0f, 0.0f);
    in[0].deviceId.clear();
    in[0].instanceId.clear();
    in[0].sessionId.clear();
    in[1] = Rec(Mode::Lower, 1.0f, 1.0f);
    in[1].deviceId = L"line\r\nbreak|bar%percent";
    in[1].sessionId = L"%7C%0D%0A literal escapes";
    in[2] = Rec(Mode::Lower, 0.3f, 0.06f);
    in[2].deviceId = L"Lautsprecher (Gro\x00DF) \x4E2D\x6587 \xD83C\xDFB5";
    in[2].instanceId = L"{0.0.0.00000000}.{aaaa-bbbb}|{cccc}|\x00E4\x00F6\x00FC";
    in[2].oldMute = true;
    in[2].savedAt = UINT64_MAX;
    in[3] = Rec(Mode::Mute, 0.123456789f, 0.987654321f);
    in[3].deviceId = std::wstring(3000, L'd');

    const std::string text = FormatStateFile(in);
    // The format keeps every record on one line, whatever the fields hold.
    size_t lines = 0;
    for (char c : text)
        if (c == '\n') ++lines;
    CHECK(lines == 1 + in.size());

    std::vector<SavedSession> out;
    CHECK(ParseStateFile(text, out));
    CHECK(out.size() == in.size());
    if (out.size() != in.size()) return;
    for (size_t i = 0; i < in.size(); ++i) {
        CHECK(out[i].deviceId == in[i].deviceId);
        CHECK(out[i].instanceId == in[i].instanceId);
        CHECK(out[i].sessionId == in[i].sessionId);
        CHECK(out[i].mode == in[i].mode);
        CHECK(out[i].oldMute == in[i].oldMute);
        CHECK(out[i].oldVolume == in[i].oldVolume);
        CHECK(out[i].appliedVolume == in[i].appliedVolume);
        CHECK(out[i].savedAt == in[i].savedAt);
    }
}

TEST(audio_state_file_volumes_survive_exactly) {
    // Written with nine digits, so every float reads back bit for bit.
    std::vector<SavedSession> in;
    for (int i = 0; i <= 100; ++i) in.push_back(Rec(Mode::Lower, static_cast<float>(i) / 100.0f, static_cast<float>(i) / 333.0f));
    std::vector<SavedSession> out;
    CHECK(ParseStateFile(FormatStateFile(in), out));
    CHECK(out.size() == in.size());
    if (out.size() != in.size()) return;
    for (size_t i = 0; i < in.size(); ++i) {
        CHECK(out[i].oldVolume == in[i].oldVolume);
        CHECK(out[i].appliedVolume == in[i].appliedVolume);
    }
}

TEST(audio_state_file_many_records) {
    std::vector<SavedSession> in;
    for (int i = 0; i < 2000; ++i) {
        SavedSession s = Rec(i % 2 ? Mode::Mute : Mode::Lower, 0.5f, 0.5f);
        s.sessionId = L"session" + std::to_wstring(i);
        s.savedAt = 1790000000u + static_cast<uint64_t>(i);
        in.push_back(s);
    }
    std::vector<SavedSession> out;
    CHECK(ParseStateFile(FormatStateFile(in), out));
    CHECK(out.size() == 2000);
    if (out.size() == 2000) {
        CHECK(out[1999].sessionId == L"session1999");
        CHECK(out[1999].savedAt == 1790001999u);
    }
}

TEST(audio_state_file_binary_garbage_is_rejected) {
    std::vector<SavedSession> out;
    std::string junk;
    for (int i = 0; i < 4096; ++i) junk.push_back(static_cast<char>((i * 37 + 11) & 0xFF));
    CHECK(!ParseStateFile(junk, out));
    CHECK(out.empty());
    // A line made of terminator bytes does not hide the records after it.
    std::string withNul = "version=2\r\nsession=d|i|s|m|1|0.5|0.5\r\n";
    withNul.append("\0\0\0\r\n", 5);
    withNul += "session=e|i|s|m|1|0.5|0.5\r\n";
    CHECK(ParseStateFile(withNul, out));
    CHECK(out.size() == 2);
    // A byte order mark in front hides the header, so the file is discarded as unknown.
    CHECK(!ParseStateFile("\xEF\xBB\xBF" "version=2\r\n", out));
}
