#include "media_detector.h"

#include "util.h"

PauseAction DecidePause(bool mediaActive, AdState state, bool pausedByUs, bool handledThisRun) {
    if (pausedByUs) {
        // An ad means the user (or Spotify) started playback again: nothing of ours is left to give back.
        if (state == AdState::Ad) return PauseAction::Forget;
        if (state == AdState::Music) return PauseAction::Forget;
        if (!mediaActive) {
            if (state == AdState::Idle) return PauseAction::Resume;
            if (state == AdState::NoSpotify) return PauseAction::Forget;
        }
        return PauseAction::None;
    }
    if (mediaActive && !handledThisRun && state == AdState::Music) return PauseAction::Pause;
    return PauseAction::None;
}

bool SuppressAdAudio(bool pausedByUs, bool spotifyAudible) {
    return pausedByUs && !spotifyAudible;
}

bool IsContentApp(std::wstring_view exeName) {
    static const wchar_t* const kApps[] = {
        L"chrome.exe",     L"msedge.exe",       L"firefox.exe",          L"opera.exe",
        L"brave.exe",      L"vivaldi.exe",      L"arc.exe",              L"librewolf.exe",
        L"waterfox.exe",   L"floorp.exe",       L"zen.exe",              L"vlc.exe",
        L"mpc-hc.exe",     L"mpc-hc64.exe",     L"mpc-be.exe",           L"mpc-be64.exe",
        L"mpv.exe",        L"potplayer.exe",    L"potplayer64.exe",      L"potplayermini.exe",
        L"potplayermini64.exe", L"wmplayer.exe", L"microsoft.media.player.exe", L"video.ui.exe",
        L"kodi.exe",       L"plex.exe",         L"plex htpc.exe",        L"jellyfinmediaplayer.exe",
        L"stremio.exe",
    };
    for (const wchar_t* app : kApps)
        if (EqualsNoCase(exeName, app)) return true;
    return false;
}

bool MediaDetector::Update(bool soundNow, uint64_t nowMs) {
    if (soundNow) {
        if (!active_) {
            if (!running_ || nowMs - lastSound_ > kMediaGapMs) {
                running_ = true;
                runStart_ = nowMs;
            }
            if (nowMs - runStart_ >= kMediaStartMs) active_ = true;
        }
        lastSound_ = nowMs;
        return active_;
    }
    if (active_) {
        if (nowMs - lastSound_ >= kMediaStopMs) {
            active_ = false;
            running_ = false;
        }
    } else if (running_ && nowMs - lastSound_ > kMediaGapMs) {
        running_ = false;
    }
    return active_;
}

void MediaDetector::Reset() {
    active_ = false;
    running_ = false;
    runStart_ = 0;
    lastSound_ = 0;
}
