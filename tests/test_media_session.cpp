#include <windows.h>

#include <string>

#include "ad_detector.h"
#include "media_session.h"
#include "test.h"

namespace {

MediaTrack Ad(const wchar_t* title, const wchar_t* artist) {
    MediaTrack t;
    t.valid = true;
    t.title = title;
    t.artist = artist;
    return t;
}

MediaTrack Song() {
    MediaTrack t;
    t.valid = true;
    t.title = L"sleep it off";
    t.artist = L"leon chang";
    t.album = L"okay";
    t.trackNumber = 1;
    t.nextEnabled = true;
    t.prevEnabled = true;
    return t;
}

}  // namespace

TEST(media_is_ad_track_samples) {
    CHECK(IsAdTrack(Ad(L"—", L"")));
    CHECK(IsAdTrack(Ad(L"Weihnachts-Hits 2026", L"Topsify Germany")));
    CHECK(IsAdTrack(Ad(L"Hör Musik ohne Werbepausen.", L"Spotify")));
    CHECK(IsAdTrack(Ad(L"JETZT HÖREN", L"Best Christmas Playlists")));
    CHECK(!IsAdTrack(Song()));
    CHECK(!IsAdTrack(MediaTrack{}));
    // Each ad trait on its own is not enough.
    MediaTrack t = Ad(L"x", L"y");
    t.trackNumber = 1;
    CHECK(!IsAdTrack(t));
    t = Ad(L"x", L"y");
    t.album = L"a";
    CHECK(!IsAdTrack(t));
    t = Ad(L"x", L"y");
    t.nextEnabled = true;
    CHECK(!IsAdTrack(t));
    t = Ad(L"x", L"y");
    t.prevEnabled = true;
    CHECK(!IsAdTrack(t));
}

TEST(media_track_matches_title) {
    const MediaTrack song = Song();
    CHECK(TrackMatchesTitle(song, L"leon chang - sleep it off"));
    CHECK(TrackMatchesTitle(song, L"  leon chang - sleep it off  "));
    CHECK(TrackMatchesTitle(song, L"sleep it off"));
    CHECK(!TrackMatchesTitle(song, L"leon chang - other song"));
    CHECK(!TrackMatchesTitle(song, L"Spotify Free"));
    CHECK(!TrackMatchesTitle(MediaTrack{}, L"sleep it off"));

    const MediaTrack dash = Ad(L"—", L"");
    CHECK(TrackMatchesTitle(dash, L"—"));
    CHECK(!TrackMatchesTitle(dash, L" - —"));
    // A track without a title never matches, not even an empty window title.
    CHECK(!TrackMatchesTitle(Ad(L"", L""), L""));
}

TEST(media_merge_regression_song_like_ad) {
    MediaTrack t = Ad(L"Alle Oasis Hits!", L"Because Maybeeee");
    const std::wstring_view title = L"Because Maybeeee - Alle Oasis Hits!";
    CHECK(ClassifyTitle(title) == TitleKind::Music);
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Music, t, title, 5000) == TitleKind::Ad);
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Idle, t, title, 0) == TitleKind::Ad);
}

TEST(media_merge_song_stays_music) {
    const MediaTrack song = Song();
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Music, song, L"leon chang - sleep it off", 100) == TitleKind::Music);
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, song, L"leon chang - sleep it off", 100) == TitleKind::Music);
}

TEST(media_merge_hold_until_session_catches_up) {
    // An ad is muted; the title switches to a song-like one while the session still shows the old ad.
    const MediaTrack oldAd = Ad(L"JETZT HÖREN", L"Best Christmas Playlists");
    const std::wstring_view next = L"leon chang - sleep it off";
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, oldAd, next, 0) == TitleKind::Ad);
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, oldAd, next, kMediaSettleMs - 1) == TitleKind::Ad);
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, oldAd, next, kMediaSettleMs) == TitleKind::Music);
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, oldAd, next, 10000) == TitleKind::Music);
    // The session confirms the real song: released at once.
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Ad, Song(), next, 100) == TitleKind::Music);
    // No hold outside a running ad.
    CHECK(MergeMediaTrack(TitleKind::Music, AdState::Music, oldAd, next, 0) == TitleKind::Music);
}

TEST(media_merge_invalid_track_changes_nothing) {
    const MediaTrack none;
    const TitleKind kinds[] = {TitleKind::NoSpotify, TitleKind::Music, TitleKind::Ad, TitleKind::IdleOrAd};
    const AdState states[] = {AdState::NoSpotify, AdState::Idle, AdState::Music, AdState::Ad};
    for (TitleKind k : kinds)
        for (AdState s : states) CHECK(MergeMediaTrack(k, s, none, L"a - b", 0) == k);
}

TEST(media_session_stop_without_start) {
    MediaSession s;
    CHECK(!s.Snapshot().valid);
    s.Stop();
    CHECK(!s.Snapshot().valid);
}

TEST(media_session_start_stop_is_clean) {
    // Whatever the system reports (Spotify running or not), the thread must start and stop cleanly.
    HWND w = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    CHECK(w != nullptr);
    MediaSession s;
    s.Start(w, WM_APP + 6);
    Sleep(800);
    s.Stop();
    CHECK(!s.Snapshot().valid);
    s.Start(w, WM_APP + 6);
    s.Stop();
    if (w) DestroyWindow(w);
}
