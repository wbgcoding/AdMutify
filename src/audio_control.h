#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "settings.h"

// Marks every volume change made by this program so it can tell user changes apart.
inline constexpr GUID kAdMutifyEventContext = {
    0x5d2c6f36, 0x8b0f, 0x4c57, {0x9c, 0x5e, 0x0c, 0x7b, 0x6a, 0x1e, 0x2f, 0x44}};

float LoweredVolume(float oldVolume, int percent);
// True when a change from outside AdMutify touched the property the given mode controls.
bool UserOverrode(Mode mode, bool expectedMute, float expectedVolume, bool newMute, float newVolume);

struct SavedSession {
    std::wstring deviceId;
    std::wstring instanceId;
    std::wstring sessionId;
    Mode mode = Mode::Mute;
    bool oldMute = false;
    float oldVolume = 1.0f;
    // Lower mode: the volume AdMutify set. Mute mode: unused, the applied state is mute on.
    float appliedVolume = 1.0f;
    // Unix seconds when the state was applied; 0 = unknown (a file from before this field), treated as old.
    uint64_t savedAt = 0;
};

// Records older than this are no longer matched by session id and are dropped when unmatched.
inline constexpr uint64_t kPendingMaxAgeSeconds = 24 * 60 * 60;
// True when a record saved at savedAt (unix seconds) is still young enough to match by session id.
bool RecordFresh(uint64_t savedAt, uint64_t nowUnix);

// A session created while a reaction is applied can come up already carrying AdMutify's mute or level
// (Windows remembers it per app). When a sibling record (same device, same sessionId, same mode) shows that the live
// state is AdMutify's own, the sibling's old values are the real ones. Returns false when the live state is not
// the applied one, so the live values are the old ones.
bool InheritedOld(Mode mode, const SavedSession& sibling, bool liveMute, float liveVolume, bool& oldMute, float& oldVolume);

std::string FormatStateFile(const std::vector<SavedSession>& sessions);
bool ParseStateFile(std::string_view text, std::vector<SavedSession>& out);

class AudioControl {
public:
    AudioControl();
    ~AudioControl();

    // notifyWindow receives WM_APP_AUDIO_STATE and WM_APP_SESSION_CREATED.
    bool Init(HWND notifyWindow);
    void Shutdown();

    // Mutes or lowers every Spotify session that is not yet changed. Returns how many sessions were touched.
    int Apply(Mode mode, int percent);
    // Puts the saved values back, except for sessions the user changed meanwhile. Returns sessions restored.
    int Restore(int* userChanged = nullptr);
    // Mode switch inside one reaction: restores and applies the new mode. Sessions the user changed in this
    // reaction are left alone and stay skipped (by Apply too) until the next full Restore.
    int SwitchMode(Mode mode, int percent);
    // Lower mode: moves the lowered sessions to a new level in place, so the full volume never comes
    // through in between. Sessions the user changed stay as they are. Returns sessions changed.
    int ChangeLevel(int percent);
    // Start-up recovery from state.ini after a crash or kill. Returns sessions restored.
    // fileIgnored is set when the file was unreadable or of an unknown format and got discarded.
    int RestoreFromStateFile(bool* fileIgnored = nullptr);
    // Records still waiting for their session to come back.
    int PendingCount() const;

    bool IsSpotifyAudioActive();
    // True while a Spotify session is active and its meter shows sound.
    bool IsSpotifyAudible();
    // True while a browser or video player outside Spotify makes audible sound and does not use the
    // microphone. exeName (optional) receives one counting exe for trace output.
    bool IsContentSoundPlaying(std::wstring* exeName = nullptr);
    bool HasAppliedSessions() const;
    // Registers change events on all Spotify sessions and the new-session notification on all devices.
    void WatchSessions();
    void UnwatchSessions();

    // Crash path: no allocation, uses a fixed table of saved values.
    // Returns false when something may still be changed: a write failed, a session had no free slot, or
    // records of earlier sessions are still pending. A session the user changed meanwhile counts as fine.
    bool RestoreUnsafe();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Reaches the active instance from the crash handler.
// True when nothing was left behind (also with no active instance).
bool RestoreAudioUnsafe();
