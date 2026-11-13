// Boundary and error-path tests for the pure detectors (title classification, state machine, media merge, video
// detection, pause decision). Only the pure functions are used: no windows, no audio, no files.
#include <cstdint>
#include <string>

#include "ad_detector.h"
#include "media_detector.h"
#include "media_session.h"
#include "test.h"

namespace {

MediaTrack SongTrack() {
    MediaTrack t;
    t.valid = true;
    t.title = L"Halo";
    t.artist = L"Beyonce";
    t.album = L"I Am";
    t.trackNumber = 4;
    t.nextEnabled = true;
    t.prevEnabled = true;
    return t;
}

MediaTrack SpotTrack(const wchar_t* title) {
    MediaTrack t;
    t.valid = true;
    t.title = title;
    t.artist = L"Brand";
    return t;
}

}  // namespace

TEST(classify_title_whitespace_and_garbage) {
    // Tabs and spaces around a title are ignored.
    CHECK(ClassifyTitle(L"\t Spotify Free \t") == TitleKind::IdleOrAd);
    CHECK(ClassifyTitle(L"\t\t") == TitleKind::NoSpotify);
    CHECK(ClassifyTitle(L"  Advertisement  ") == TitleKind::Ad);
    // Separator needs its own spaces: a hyphen inside a word is no artist/title split.
    CHECK(ClassifyTitle(L"Spider-Man") == TitleKind::Ad);
    CHECK(ClassifyTitle(L"-") == TitleKind::Ad);
    CHECK(ClassifyTitle(L"\x2014") == TitleKind::Ad);
    // A lone separator trims down to a hyphen and stays a plain title.
    CHECK(ClassifyTitle(L" - ") == TitleKind::Ad);
    // Only the first separator decides, later ones belong to the song title.
    CHECK(ClassifyTitle(L"A - B - C") == TitleKind::Music);
    CHECK(ClassifyTitle(L"Spotify - A - B") == TitleKind::Ad);
    // Extra spaces around the artist part still count as the Spotify artist.
    CHECK(ClassifyTitle(L"Spotify  - Premium") == TitleKind::Ad);
    // Case of the artist does not matter, the idle titles are exact.
    CHECK(ClassifyTitle(L"SPOTIFY - X") == TitleKind::Ad);
    // An embedded terminator is just another character.
    const std::wstring withNul(L"Artist\0 - Song", 14);
    CHECK(ClassifyTitle(withNul) == TitleKind::Music);
    // A very long title does not trip anything.
    const std::wstring longSong = std::wstring(5000, L'x') + L" - " + std::wstring(5000, L'y');
    CHECK(ClassifyTitle(longSong) == TitleKind::Music);
    CHECK(ClassifyTitle(std::wstring(20000, L'z')) == TitleKind::Ad);
    // Lone surrogate halves are only characters too.
    CHECK(ClassifyTitle(L"\xD800") == TitleKind::Ad);
}

TEST(ad_state_names_are_distinct) {
    CHECK(std::string(AdStateName(AdState::Idle)) == "Idle");
    CHECK(std::string(AdStateName(AdState::Music)) == "Music");
    CHECK(std::string(AdStateName(static_cast<AdState>(99))) == "?");
}

TEST(state_ad_exit_boundary_to_idle_and_gone) {
    for (TitleKind exitKind : {TitleKind::NoSpotify, TitleKind::IdleOrAd}) {
        AdStateMachine m;
        CHECK(m.Update(TitleKind::Ad, true, 1000, true) == AdState::Ad);
        // IdleOrAd needs an inactive session to read as idle.
        CHECK(m.Update(exitKind, false, 2000, false) == AdState::Ad);
        CHECK(m.PendingMs(2000) == kAdExitDebounceMs);
        CHECK(m.PendingMs(2000 + kAdExitDebounceMs - 1) == 1);
        CHECK(m.Update(exitKind, false, 2000 + kAdExitDebounceMs - 1, false) == AdState::Ad);
        CHECK(m.PendingMs(2000 + kAdExitDebounceMs) == 0);
        const AdState left = m.Update(exitKind, false, 2000 + kAdExitDebounceMs, false);
        CHECK(left == (exitKind == TitleKind::NoSpotify ? AdState::NoSpotify : AdState::Idle));
        CHECK(m.PendingMs(5000) == 0);
    }
}

TEST(state_ad_returning_cancels_the_exit_wait) {
    AdStateMachine m;
    m.Update(TitleKind::Ad, true, 0, true);
    CHECK(m.Update(TitleKind::NoSpotify, false, 100, false) == AdState::Ad);
    CHECK(m.PendingMs(100) == kAdExitDebounceMs);
    // The ad title comes back before the debounce ends: nothing pending any more.
    CHECK(m.Update(TitleKind::Ad, true, 500, true) == AdState::Ad);
    CHECK(m.PendingMs(500) == 0);
    // A new exit starts a fresh wait, it does not inherit the old start time.
    CHECK(m.Update(TitleKind::NoSpotify, false, 650, false) == AdState::Ad);
    CHECK(m.Update(TitleKind::NoSpotify, false, 650 + kAdExitDebounceMs - 1, false) == AdState::Ad);
    CHECK(m.Update(TitleKind::NoSpotify, false, 650 + kAdExitDebounceMs, false) == AdState::NoSpotify);
}

TEST(state_uncertain_ad_boundary_and_pending_report) {
    AdStateMachine m;
    CHECK(m.Update(TitleKind::IdleOrAd, true, 10, true) == AdState::NoSpotify);
    CHECK(m.PendingMs(10) == kUncertainAdMs);
    CHECK(m.PendingMs(10 + kUncertainAdMs - 1) == 1);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 10 + kUncertainAdMs - 1, true) == AdState::NoSpotify);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 10 + kUncertainAdMs, true) == AdState::Ad);
    CHECK(m.PendingMs(10 + kUncertainAdMs) == 0);
}

TEST(state_tick_values_near_the_end_of_the_range) {
    // The elapsed time is an unsigned difference, so a counter wrapping past its maximum still measures right.
    const uint64_t start = UINT64_MAX - 100;
    AdStateMachine m;
    m.Update(TitleKind::Ad, true, start - 10, true);
    CHECK(m.Update(TitleKind::NoSpotify, false, start, false) == AdState::Ad);
    const uint64_t almost = start + (kAdExitDebounceMs - 1);
    const uint64_t done = start + kAdExitDebounceMs;
    CHECK(almost < start);
    CHECK(m.PendingMs(almost) == 1);
    CHECK(m.Update(TitleKind::NoSpotify, false, almost, false) == AdState::Ad);
    CHECK(m.Update(TitleKind::NoSpotify, false, done, false) == AdState::NoSpotify);

    // And a plain huge value with no wrap.
    AdStateMachine big;
    const uint64_t huge = UINT64_MAX / 2;
    CHECK(big.Update(TitleKind::IdleOrAd, true, huge, true) == AdState::NoSpotify);
    CHECK(big.Update(TitleKind::IdleOrAd, true, huge + kUncertainAdMs, true) == AdState::Ad);
}

TEST(state_music_to_idle_and_gone_are_immediate) {
    AdStateMachine m;
    CHECK(m.Update(TitleKind::Music, true, 0, true) == AdState::Music);
    CHECK(m.Update(TitleKind::NoSpotify, false, 1, false) == AdState::NoSpotify);
    CHECK(m.PendingMs(1) == 0);
    CHECK(m.Update(TitleKind::Music, true, 2, true) == AdState::Music);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 3, false) == AdState::Idle);
    CHECK(m.PendingMs(3) == 0);
}

TEST(state_ad_title_after_idle_enters_without_wait) {
    AdStateMachine m;
    m.Update(TitleKind::IdleOrAd, false, 0, false);
    CHECK(m.Current() == AdState::Idle);
    // A clear ad title needs neither sound nor a wait.
    CHECK(m.Update(TitleKind::Ad, false, 5, false) == AdState::Ad);
    CHECK(m.PendingMs(5) == 0);
}

TEST(ad_session_guard_reset_paths) {
    AdSessionGuard g;
    // Counting starts inside an ad, then the ad ends (not inAd): the count is dropped.
    CHECK(!g.Update(true, true, false, 0));
    CHECK(!g.Update(false, true, false, 3000));
    // A later ad counts from its own start, not from the old one.
    CHECK(!g.Update(true, true, false, 4000));
    CHECK(!g.Update(true, true, false, 4000 + kAdSessionGoneMs - 1));
    CHECK(g.Update(true, true, false, 4000 + kAdSessionGoneMs));
    // The ad title changing takes the verdict back.
    CHECK(!g.Update(true, false, false, 4000 + kAdSessionGoneMs + 1));
    // And the next ad title starts over with a full wait.
    const uint64_t t = 20000;
    CHECK(!g.Update(true, true, false, t));
    CHECK(!g.Update(true, true, false, t + kAdSessionGoneMs - 1));
    CHECK(g.Update(true, true, false, t + kAdSessionGoneMs));
    // A session that plays again ends the verdict and a silent period must run the full time once more.
    CHECK(!g.Update(true, true, true, t + kAdSessionGoneMs + 10));
    CHECK(!g.Update(true, true, false, t + kAdSessionGoneMs + 20));
    CHECK(!g.Update(true, true, false, t + 2 * kAdSessionGoneMs + 19));
    CHECK(g.Update(true, true, false, t + 2 * kAdSessionGoneMs + 20));
}

TEST(ad_session_guard_wraps_like_the_machine) {
    AdSessionGuard g;
    const uint64_t start = UINT64_MAX - 10;
    CHECK(!g.Update(true, true, false, start));
    CHECK(!g.Update(true, true, false, start + kAdSessionGoneMs - 1));
    CHECK(g.Update(true, true, false, start + kAdSessionGoneMs));
}

TEST(ad_session_guard_without_ad_title_never_fires) {
    AdSessionGuard g;
    for (uint64_t t = 0; t < 4 * kAdSessionGoneMs; t += 1000) CHECK(!g.Update(true, false, false, t));
    for (uint64_t t = 0; t < 4 * kAdSessionGoneMs; t += 1000) CHECK(!g.Update(false, false, false, t));
}

TEST(media_merge_settle_boundaries) {
    // A plain title the session has not caught up with is held as music for kMediaSettleMs, then trusted.
    const MediaTrack song = SongTrack();
    CHECK(MergeMediaTrack(TitleKind::Ad, AdState::Music, song, L"Zalando", kMediaSettleMs - 1) == TitleKind::Music);
    CHECK(MergeMediaTrack(TitleKind::Ad, AdState::Music, song, L"Zalando", kMediaSettleMs) == TitleKind::Ad);
    CHECK(MergeMediaTrack(TitleKind::Ad, AdState::Music, song, L"Zalando", 0) == TitleKind::Music);
    // Already in Ad the title is trusted at once.
    CHECK(MergeMediaTrack(TitleKind::Ad, AdState::Ad, song, L"Zalando", 0) == TitleKind::Ad);

    // A song title during an ad break is held as Ad for the same time while the session still shows the spot.
    const MediaTrack spot = SpotTrack(L"Other Brand");
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, spot, L"Band - Song", kMediaSettleMs - 1) == TitleKind::Ad);
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, spot, L"Band - Song", kMediaSettleMs) == TitleKind::Music);
    // Outside an ad break the same combination stays music.
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Music, spot, L"Band - Song", 10) == TitleKind::Music);
}

TEST(media_merge_flash_hold_needs_missing_controls) {
    // Album and track number set, so the track is not an ad track; only the controls vary.
    MediaTrack flash = SongTrack();
    flash.nextEnabled = false;
    flash.prevEnabled = false;
    const std::wstring_view title = L"Beyonce - Halo";
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, flash, title, kMediaSettleMs - 1) == TitleKind::Ad);
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, flash, title, kMediaSettleMs) == TitleKind::Music);
    flash.nextEnabled = true;
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, flash, title, 0) == TitleKind::Music);
    flash.nextEnabled = false;
    flash.prevEnabled = true;
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, flash, title, 0) == TitleKind::Music);
}

TEST(media_merge_other_kinds_pass_through) {
    const MediaTrack song = SongTrack();
    CHECK(MergeMediaTrack(TitleKind::NoSpotify, AdState::NoSpotify, song, L"", 0) == TitleKind::NoSpotify);
    CHECK(MergeMediaTrack(TitleKind::IdleOrAd, AdState::Idle, song, L"Spotify Free", 0) == TitleKind::IdleOrAd);
    CHECK(MergeMediaTrack(TitleKind::IdleOrAd, AdState::Ad, song, L"Spotify Free", 100) == TitleKind::IdleOrAd);
    // An empty window title never matches a track, so an ad track cannot raise an alarm from nothing.
    CHECK(MergeMediaTrack(TitleKind::NoSpotify, AdState::NoSpotify, SpotTrack(L"x"), L"", 0) == TitleKind::NoSpotify);
    // Invalid track: the verdict stays whatever the title said, for every kind.
    for (TitleKind k : {TitleKind::NoSpotify, TitleKind::Music, TitleKind::Ad, TitleKind::IdleOrAd})
        CHECK(MergeMediaTrack(k, AdState::Ad, MediaTrack{}, L"x", 0) == k);
}

TEST(media_track_helpers_edge_cases) {
    MediaTrack t = SongTrack();
    CHECK(IsContentTrack(t));
    t.album.clear();
    CHECK(!IsContentTrack(t));
    CHECK(!IsContentTrack(MediaTrack{}));
    // An invalid track is never an ad, whatever the other fields hold.
    MediaTrack bare;
    bare.title = L"x";
    CHECK(!IsAdTrack(bare));
    // Tabs and line breaks around the fields are trimmed like spaces.
    MediaTrack spaced = SongTrack();
    spaced.title = L"\tHalo\r\n";
    spaced.artist = L" Beyonce\t";
    CHECK(TrackMatchesTitle(spaced, L"\nBeyonce - Halo "));
    CHECK(TrackMatchesTitle(spaced, L"Halo"));
    // A different artist, a swapped order and a partial title do not match.
    CHECK(!TrackMatchesTitle(spaced, L"Halo - Beyonce"));
    CHECK(!TrackMatchesTitle(spaced, L"Other - Halo"));
    CHECK(!TrackMatchesTitle(spaced, L"Hal"));
    // Without an artist only the plain title matches.
    spaced.artist.clear();
    CHECK(TrackMatchesTitle(spaced, L"Halo"));
    CHECK(!TrackMatchesTitle(spaced, L"Beyonce - Halo"));
}

TEST(media_detector_gap_boundary) {
    // A gap of exactly kMediaGapMs keeps the run, one more millisecond ends it.
    MediaDetector keep;
    CHECK(!keep.Update(true, 0));
    CHECK(!keep.Update(true, kMediaGapMs));
    CHECK(!keep.Update(true, 2 * kMediaGapMs));
    CHECK(keep.Update(true, 3 * kMediaGapMs));

    MediaDetector broken;
    CHECK(!broken.Update(true, 0));
    CHECK(!broken.Update(true, kMediaGapMs + 1));
    CHECK(!broken.Update(true, 2 * (kMediaGapMs + 1)));
    CHECK(!broken.Update(true, 3 * (kMediaGapMs + 1)));
    CHECK(!broken.Active());
}

TEST(media_detector_start_boundary) {
    MediaDetector d;
    for (uint64_t t = 0; t < kMediaStartMs; t += 500) CHECK(!d.Update(true, t));
    CHECK(!d.Update(true, kMediaStartMs - 1));
    CHECK(d.Update(true, kMediaStartMs));
    CHECK(d.Active());
    // Sound keeps it on, no matter how long.
    CHECK(d.Update(true, kMediaStartMs + 60000));
}

TEST(media_detector_stop_boundary) {
    MediaDetector d;
    for (uint64_t t = 0; t <= kMediaStartMs; t += 500) d.Update(true, t);
    CHECK(d.Active());
    const uint64_t last = kMediaStartMs;
    CHECK(d.Update(false, last + kMediaStopMs - 1));
    CHECK(!d.Update(false, last + kMediaStopMs));
    // After the stop a new video needs the full start time again.
    const uint64_t again = last + kMediaStopMs + 100;
    for (uint64_t t = again; t < again + kMediaStartMs; t += 500) CHECK(!d.Update(true, t));
    CHECK(!d.Update(true, again + kMediaStartMs - 1));
    CHECK(d.Update(true, again + kMediaStartMs));
}

TEST(media_detector_sound_resumes_before_stop) {
    MediaDetector d;
    for (uint64_t t = 0; t <= kMediaStartMs; t += 500) d.Update(true, t);
    CHECK(d.Update(false, kMediaStartMs + kMediaStopMs - 1));
    // The sound is back in time: no new start time is needed and the stop clock restarts.
    CHECK(d.Update(true, kMediaStartMs + kMediaStopMs));
    CHECK(d.Update(false, kMediaStartMs + 2 * kMediaStopMs - 1));
    CHECK(!d.Update(false, kMediaStartMs + 2 * kMediaStopMs));
}

TEST(media_detector_silence_only_and_wrap) {
    MediaDetector quiet;
    for (uint64_t t = 0; t < 20000; t += 700) CHECK(!quiet.Update(false, t));
    CHECK(!quiet.Active());

    // Tick values close to the end of the range keep the arithmetic right.
    MediaDetector d;
    const uint64_t start = UINT64_MAX - 1000;
    uint64_t t = start;
    bool active = false;
    for (int i = 0; i < 12; ++i, t += 500) active = d.Update(true, t);
    CHECK(t < start);
    CHECK(active);
    CHECK(d.Update(false, t + kMediaStopMs - 500 - 1));
    CHECK(!d.Update(false, t + kMediaStopMs - 500));
}

TEST(pause_decision_exhaustive_rules) {
    const AdState states[] = {AdState::NoSpotify, AdState::Idle, AdState::Music, AdState::Ad};
    for (bool media : {false, true}) {
        for (AdState s : states) {
            for (bool handled : {false, true}) {
                // Without our own pause only one situation pauses: video, music playing, not handled yet.
                const PauseAction plain = DecidePause(media, s, false, handled);
                CHECK(plain == (media && !handled && s == AdState::Music ? PauseAction::Pause : PauseAction::None));

                // With our pause the handled flag is irrelevant.
                const PauseAction held = DecidePause(media, s, true, handled);
                CHECK(held == DecidePause(media, s, true, !handled));
                PauseAction want = PauseAction::None;
                if (s == AdState::Ad || s == AdState::Music) want = PauseAction::Forget;
                else if (!media && s == AdState::Idle) want = PauseAction::Resume;
                else if (!media && s == AdState::NoSpotify) want = PauseAction::Forget;
                CHECK(held == want);
            }
        }
    }
}

TEST(suppress_ad_audio_truth_table) {
    CHECK(SuppressAdAudio(true, false));
    CHECK(!SuppressAdAudio(true, true));
    CHECK(!SuppressAdAudio(false, false));
    CHECK(!SuppressAdAudio(false, true));
}

TEST(content_app_names_edge_cases) {
    CHECK(IsContentApp(L"CHROME.EXE"));
    CHECK(IsContentApp(L"Plex HTPC.exe"));
    // Full paths are not file names; callers pass the file name only.
    CHECK(!IsContentApp(L"C:\\Program Files\\Google\\chrome.exe"));
    CHECK(!IsContentApp(L"chrome"));
    CHECK(!IsContentApp(L"chrome.exe "));
    CHECK(!IsContentApp(L"Spotify.exe"));
    CHECK(!IsContentApp(L"vlc.exe.bak"));
}
