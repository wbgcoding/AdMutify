#include <cmath>
#include <string>

#include "audio_control.h"
#include "crash.h"
#include "settings.h"
#include "spotify_watch.h"
#include "test.h"

TEST(audio_user_override_only_counts_for_controlled_property) {
    // Mute mode: moving the volume slider during a muted ad is not an override, unmuting is.
    CHECK(!UserOverrode(Mode::Mute, true, 0.8f, true, 0.5f));
    CHECK(UserOverrode(Mode::Mute, true, 0.8f, false, 0.8f));
    // Lower mode: toggling mute is not an override, moving the slider is.
    CHECK(!UserOverrode(Mode::Lower, false, 0.16f, true, 0.16f));
    CHECK(UserOverrode(Mode::Lower, false, 0.16f, false, 0.5f));
    CHECK(!UserOverrode(Mode::Lower, false, 0.16f, false, 0.1605f));
}

TEST(audio_lowered_volume) {
    CHECK(std::fabs(LoweredVolume(0.8f, 20) - 0.16f) < 1e-6f);
    CHECK(LoweredVolume(0.8f, 0) == 0.0f);
    CHECK(std::fabs(LoweredVolume(0.5f, 100) - 0.5f) < 1e-6f);
    CHECK(std::fabs(LoweredVolume(0.5f, 150) - 0.5f) < 1e-6f);
}

TEST(audio_inherited_old_values) {
    SavedSession sib;
    sib.mode = Mode::Mute;
    sib.oldMute = false;
    sib.oldVolume = 0.6f;
    sib.appliedVolume = 0.6f;
    bool m = true;
    float v = 0.0f;
    // Mute: a new session that already reads muted carries our mute, the sibling knows the real values.
    CHECK(InheritedOld(Mode::Mute, sib, true, 0.6f, m, v));
    CHECK(!m);
    CHECK(v == 0.6f);
    // Not muted: the user's own state, nothing to inherit.
    m = true;
    v = 0.0f;
    CHECK(!InheritedOld(Mode::Mute, sib, false, 0.9f, m, v));
    CHECK(m && v == 0.0f);
    // Another mode never matches.
    CHECK(!InheritedOld(Mode::Lower, sib, true, 0.6f, m, v));

    // Lower: live volume 0.12 equals the sibling's applied level 0.12 (old 0.6 at 20 percent).
    sib.mode = Mode::Lower;
    sib.oldMute = true;
    sib.oldVolume = 0.6f;
    sib.appliedVolume = 0.12f;
    CHECK(InheritedOld(Mode::Lower, sib, false, 0.1205f, m, v));
    CHECK(m);
    CHECK(v == 0.6f);
    // Live volume 0.5 differs from 0.12: the user's value.
    m = false;
    v = 0.0f;
    CHECK(!InheritedOld(Mode::Lower, sib, false, 0.5f, m, v));
    CHECK(!m && v == 0.0f);
    CHECK(!InheritedOld(Mode::Mute, sib, true, 0.12f, m, v));
}

TEST(audio_state_file_roundtrip) {
    std::vector<SavedSession> in(2);
    in[0].deviceId = L"{0.0.0.00000000}.{abc|def}";
    in[0].instanceId = L"inst|with|bars%25\\x";
    in[0].sessionId = L"sess|id%1\\Spotify.exe%b0";
    in[0].mode = Mode::Mute;
    in[0].oldMute = true;
    in[0].oldVolume = 0.37f;
    in[0].appliedVolume = 0.37f;
    in[1].deviceId = L"{0.0.0.00000000}.{second}";
    in[1].instanceId = L"{0.0.0.00000000}.{second}|{guid}|1234";
    in[1].sessionId = L"{0.0.0.00000000}.{second}|Spotify.exe%b0";
    in[1].mode = Mode::Lower;
    in[1].oldMute = false;
    in[1].oldVolume = 1.0f;
    in[1].appliedVolume = 0.2f;
    in[0].savedAt = 1790000000;
    in[1].savedAt = 0;

    std::string text = FormatStateFile(in);
    std::vector<SavedSession> out;
    CHECK(ParseStateFile(text, out));
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        for (int i = 0; i < 2; ++i) {
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
}

TEST(audio_state_file_rejects_other_versions) {
    std::vector<SavedSession> out;
    CHECK(!ParseStateFile("version=1\r\nsession=a|b|1|0.5\r\n", out));
    CHECK(out.empty());
    CHECK(!ParseStateFile("version=3\r\nsession=a|b|c|m|1|0.5|0.5\r\n", out));
    CHECK(out.empty());
    CHECK(!ParseStateFile("session=a|b|c|m|1|0.5|0.5\r\n", out));
    CHECK(out.empty());
    CHECK(!ParseStateFile("", out));
}

TEST(audio_state_file_accepts_files_without_saved_time) {
    std::vector<SavedSession> out;
    CHECK(ParseStateFile("version=2\r\nsession=d|i|s|l|0|0.5|0.25\r\nsession=d|j|t|m|1|0.5|0.5|1790000000\r\n", out));
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].savedAt == 0);
        CHECK(out[1].savedAt == 1790000000);
    }
}

TEST(audio_record_freshness) {
    const uint64_t now = 1790000000;
    CHECK(!RecordFresh(0, now));
    CHECK(RecordFresh(now, now));
    CHECK(RecordFresh(now - kPendingMaxAgeSeconds, now));
    CHECK(!RecordFresh(now - kPendingMaxAgeSeconds - 1, now));
    CHECK(RecordFresh(now + 5000, now));
}

TEST(audio_state_file_keeps_invalid_escapes) {
    std::vector<SavedSession> out;
    CHECK(ParseStateFile("version=2\r\nsession=d|i%4G|s%|l|0|0.5|0.25\r\n", out));
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        CHECK(out[0].instanceId == L"i%4G");
        CHECK(out[0].sessionId == L"s%");
    }
}

TEST(audio_state_file_skips_bad_lines) {
    std::vector<SavedSession> out;
    const char* text =
        "version=2\r\n"
        "session=d|i|s|x|1|0.5|0.5\r\n"        // bad mode
        "session=d|i|s|m|2|0.5|0.5\r\n"        // bad mute flag
        "session=d|i|s|l|0|1.5|0.5\r\n"        // volume above 1
        "session=d|i|s|l|0|0.5|-0.1\r\n"       // volume below 0
        "session=d|i|s|l|0|nan|0.5\r\n"        // NaN
        "session=d|i|s|l|0|0.5x|0.5\r\n"       // trailing garbage
        "session=d|i|s|l|0|0.5|\r\n"           // empty volume
        "session=d|i|l|0|0.5|0.5\r\n"          // six fields
        "session=d|i|s|l|0|0.5|0.5|extra\r\n"  // non numeric eighth field
        "session=d|i|s|l|0|0.5|0.25\r\n";      // valid
    CHECK(ParseStateFile(text, out));
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        CHECK(out[0].mode == Mode::Lower);
        CHECK(out[0].oldVolume == 0.5f);
        CHECK(out[0].appliedVolume == 0.25f);
    }
}

TEST(audio_apply_without_spotify) {
    // A process name that cannot exist keeps this test away from a running Spotify.
    SetTargetProcessName(L"AdMutifyNoSuchProcess.exe");
    {
        AudioControl ac;
        ac.Init(nullptr);
        CHECK(ac.Apply(Mode::Mute, 20) == 0);
        CHECK(ac.Apply(Mode::Lower, 20) == 0);
        CHECK(!ac.IsSpotifyAudioActive());
        CHECK(!ac.HasAppliedSessions());
        int changed = 5;
        CHECK(ac.Restore(&changed) == 0);
        CHECK(changed == 0);
        CHECK(ac.RestoreFromStateFile() == 0);
        ac.WatchSessions();
        ac.UnwatchSessions();
    }
    SetTargetProcessName(L"Spotify.exe");
}

TEST(audio_changes_stop_while_crash_in_progress) {
    AudioControl ac;
    ac.Init(nullptr);
    SetCrashInProgressForTests(true);
    CHECK(CrashInProgress());
    CHECK(ac.Apply(Mode::Mute, 20) == 0);
    CHECK(ac.Restore() == 0);
    CHECK(ac.SwitchMode(Mode::Lower, 20) == 0);
    CHECK(ac.ChangeLevel(30) == 0);
    SetCrashInProgressForTests(false);
    CHECK(!CrashInProgress());
    ac.Shutdown();
}

TEST(audio_content_poll_smoke) {
    // The result depends on what the host plays; the poll must just run repeatedly without trouble.
    AudioControl ac;
    ac.Init(nullptr);
    std::wstring exe;
    for (int i = 0; i < 3; ++i) {
        bool playing = ac.IsContentSoundPlaying(&exe);
        CHECK(!playing || !exe.empty());
    }
    ac.Shutdown();
    CHECK(!ac.IsContentSoundPlaying());
}
