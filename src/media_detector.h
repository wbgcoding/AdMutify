#pragma once

#include <cstdint>
#include <string_view>

#include "ad_detector.h"

// Sound has to be present this long before it counts as a video; gaps up to kMediaGapMs keep the run alive.
constexpr uint64_t kMediaStartMs = 4000;
constexpr uint64_t kMediaGapMs = 1500;
// An active detection ends after this much silence.
constexpr uint64_t kMediaStopMs = 3000;

// True for browsers and video players (file name only, case-insensitive).
bool IsContentApp(std::wstring_view exeName);

// What the "Pause Spotify" video choice does on one evaluation.
enum class PauseAction { None, Pause, Resume, Forget };

// Pure decision. Pause: a video plays, Spotify plays music and we neither paused it before nor handled this video.
// Forget: while we hold a pause, the user pressed play (state Music) or an ad started (Ad), or Spotify went away
// once the video is over (NoSpotify); our pause flag is dropped without touching Spotify. Spotify going away while
// the video still runs decides nothing here (None); the caller's media timer reset ends that run. Resume: the video
// ended and Spotify still sits paused (Idle). The caller sets pausedByUs/handledThisRun from the result and clears
// handledThisRun once the video is over.
PauseAction DecidePause(bool mediaActive, AdState state, bool pausedByUs, bool handledThisRun);

// After our pause Spotify needs a moment to change its title.
constexpr uint64_t kPauseSettleMs = 3000;

// While we hold a pause, Spotify's session can stay active without sound for a long time; that is not an ad.
// As soon as Spotify is audible again the user pressed play, and the ad logic must see it.
bool SuppressAdAudio(bool pausedByUs, bool spotifyAudible);

class MediaDetector {
public:
    // Feeds one poll result; returns whether video playback is currently detected.
    bool Update(bool soundNow, uint64_t nowMs);
    bool Active() const { return active_; }
    void Reset();

private:
    bool active_ = false;
    bool running_ = false;
    uint64_t runStart_ = 0;
    uint64_t lastSound_ = 0;
};
