#include "media_detector.h"
#include "test.h"

namespace {

// Feeds sound for [from, to) in 100 ms steps and returns the first time the detector became active (0 = never).
uint64_t FeedSound(MediaDetector& d, uint64_t from, uint64_t to) {
    for (uint64_t t = from; t < to; t += 100)
        if (d.Update(true, t)) return t;
    return 0;
}

bool FeedSilence(MediaDetector& d, uint64_t from, uint64_t to) {
    bool active = d.Active();
    for (uint64_t t = from; t < to; t += 100) active = d.Update(false, t);
    return active;
}

}  // namespace

TEST(content_app_list) {
    CHECK(IsContentApp(L"chrome.exe"));
    CHECK(IsContentApp(L"CHROME.EXE"));
    CHECK(IsContentApp(L"Plex HTPC.exe"));
    CHECK(IsContentApp(L"mpc-be64.exe"));
    CHECK(!IsContentApp(L"spotify.exe"));
    CHECK(!IsContentApp(L"discord.exe"));
    CHECK(!IsContentApp(L"msedgewebview2.exe"));
    CHECK(!IsContentApp(L"game.exe"));
    CHECK(!IsContentApp(L""));
}

TEST(media_short_sound_never_active) {
    MediaDetector d;
    CHECK(FeedSound(d, 0, 1000) == 0);
    CHECK(!FeedSilence(d, 1000, 10000));
    CHECK(!d.Active());
}

TEST(media_continuous_sound_becomes_active_at_threshold) {
    MediaDetector d;
    CHECK(FeedSound(d, 0, 3900) == 0);
    CHECK(!d.Active());
    CHECK(d.Update(true, 4000));
    CHECK(d.Active());
}

TEST(media_short_gaps_do_not_break_the_run) {
    MediaDetector d;
    uint64_t t = 0;
    bool active = false;
    // 1 s sound, 1 s gap, repeated for 6 s.
    while (t < 6000) {
        bool sound = (t / 1000) % 2 == 0;
        active = d.Update(sound, t) || active;
        t += 100;
    }
    CHECK(active);
}

TEST(media_long_gap_during_buildup_resets) {
    MediaDetector d;
    CHECK(FeedSound(d, 0, 2000) == 0);
    CHECK(!FeedSilence(d, 2000, 4000));
    // New run starts at 4000: 3.9 s later it is still not active, 4 s later it is.
    CHECK(FeedSound(d, 4000, 7900) == 0);
    CHECK(d.Update(true, 8000));
}

TEST(media_gap_ends_run_even_without_silent_polls) {
    MediaDetector d;
    CHECK(!d.Update(true, 0));
    CHECK(!d.Update(true, 100));
    // Sparse polling: the 3.9 s jump is a gap, so the old run does not count.
    CHECK(!d.Update(true, 4000));
    CHECK(!d.Active());
}

TEST(media_stays_active_through_short_silence) {
    MediaDetector d;
    CHECK(FeedSound(d, 0, 5000) == 4000);
    // Last sound at 4000: 2.5 s of silence keeps it active, 3.1 s ends it.
    CHECK(FeedSilence(d, 4100, 6501));
    CHECK(d.Update(false, 6900));
    CHECK(!d.Update(false, 7100));
    CHECK(!d.Active());
}

TEST(media_reset_clears_state) {
    MediaDetector d;
    CHECK(FeedSound(d, 0, 5000) == 4000);
    d.Reset();
    CHECK(!d.Active());
    CHECK(!d.Update(true, 5100));
}

TEST(pause_decision_pauses_once_per_video) {
    CHECK(DecidePause(true, AdState::Music, false, false) == PauseAction::Pause);
    CHECK(DecidePause(true, AdState::Music, false, true) == PauseAction::None);
    // Never during an ad, never when Spotify is idle or gone, never without a video.
    CHECK(DecidePause(true, AdState::Ad, false, false) == PauseAction::None);
    CHECK(DecidePause(true, AdState::Idle, false, false) == PauseAction::None);
    CHECK(DecidePause(true, AdState::NoSpotify, false, false) == PauseAction::None);
    CHECK(DecidePause(false, AdState::Music, false, false) == PauseAction::None);
}

TEST(pause_decision_resume_and_forget) {
    // Video over and Spotify still paused: resume.
    CHECK(DecidePause(false, AdState::Idle, true, true) == PauseAction::Resume);
    // While the video runs and Spotify is paused by us: wait.
    CHECK(DecidePause(true, AdState::Idle, true, true) == PauseAction::None);
    // The user pressed play during the video: forget, with or without the video still running.
    CHECK(DecidePause(true, AdState::Music, true, true) == PauseAction::Forget);
    CHECK(DecidePause(false, AdState::Music, true, true) == PauseAction::Forget);
    // Spotify closed or an ad started after the video ended: forget.
    CHECK(DecidePause(false, AdState::NoSpotify, true, true) == PauseAction::Forget);
    CHECK(DecidePause(false, AdState::Ad, true, true) == PauseAction::Forget);
    // An ad that starts while the video still runs also ends our hold, so no resume toggle comes later.
    CHECK(DecidePause(true, AdState::Ad, true, true) == PauseAction::Forget);
    // Nothing held, nothing to do.
    CHECK(DecidePause(false, AdState::Idle, false, false) == PauseAction::None);
}

TEST(pause_decision_full_cycle) {
    bool paused = false, handled = false;
    auto step = [&](bool media, AdState st) {
        PauseAction a = DecidePause(media, st, paused, handled);
        if (a == PauseAction::Pause) paused = handled = true;
        if (a == PauseAction::Resume || a == PauseAction::Forget) paused = false;
        if (!media) handled = false;
        return a;
    };
    CHECK(step(true, AdState::Music) == PauseAction::Pause);
    CHECK(step(true, AdState::Idle) == PauseAction::None);
    CHECK(step(false, AdState::Idle) == PauseAction::Resume);
    // User presses play during the next video: we pause once, forget, and do not pause again.
    CHECK(step(true, AdState::Music) == PauseAction::Pause);
    CHECK(step(true, AdState::Music) == PauseAction::Forget);
    CHECK(step(true, AdState::Music) == PauseAction::None);
    CHECK(step(false, AdState::Music) == PauseAction::None);
    CHECK(step(true, AdState::Music) == PauseAction::Pause);
}

TEST(hold_suppresses_only_silent_ad_audio) {
    // A silent active session during our hold, however long, is not an ad.
    CHECK(SuppressAdAudio(true, false));
    // Sound during our hold means the user pressed play: the ad logic must see it.
    CHECK(!SuppressAdAudio(true, true));
    // Without a hold of ours nothing is suppressed.
    CHECK(!SuppressAdAudio(false, false));
}
