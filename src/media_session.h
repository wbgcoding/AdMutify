#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "ad_detector.h"

// What the Windows media session reports for the Spotify app. Ads carry no track number and no album, and the
// player offers no next or previous track.
struct MediaTrack {
    bool valid = false;
    std::wstring title, artist, album;
    uint32_t trackNumber = 0;
    bool nextEnabled = false;
    bool prevEnabled = false;

    bool operator==(const MediaTrack& o) const {
        return valid == o.valid && title == o.title && artist == o.artist && album == o.album &&
               trackNumber == o.trackNumber && nextEnabled == o.nextEnabled && prevEnabled == o.prevEnabled;
    }
};

// The media session usually lags the window title by a few hundred ms.
constexpr uint64_t kMediaSettleMs = 1500;

bool IsAdTrack(const MediaTrack& t);
// Has an album (a song's album or a podcast's show): never a Spotify spot. Skip controls are not used, Spotify
// turns them off for a moment while switching episodes.
bool IsContentTrack(const MediaTrack& t);
// The window title is the track title, or "artist - title".
bool TrackMatchesTitle(const MediaTrack& t, std::wstring_view windowTitle);
// Second signal on top of ClassifyTitle: catches ads whose title looks like a song, and holds a running ad
// mute until the media session has caught up with a new window title. An invalid track changes nothing.
TitleKind MergeMediaTrack(TitleKind kind, AdState current, const MediaTrack& t, std::wstring_view windowTitle,
                          uint64_t msSinceTitleChange);

// Watches the Spotify entry of the Windows media session on its own thread. Any failure leaves the snapshot
// invalid, so the caller falls back to the window title alone.
class MediaSession {
public:
    MediaSession();
    ~MediaSession();
    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;

    // Starts the thread; every change of the snapshot posts `message` to `notify`.
    void Start(HWND notify, UINT message);
    // Revokes the handlers and joins the thread, waiting at most kStopWaitMs.
    void Stop();
    static constexpr DWORD kStopWaitMs = 1500;
    MediaTrack Snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
