#pragma once

#include <cstdint>
#include <string_view>

enum class TitleKind { NoSpotify, Music, Ad, IdleOrAd };
enum class AdState { NoSpotify, Idle, Music, Ad };

constexpr uint64_t kAdExitDebounceMs = 600;
// A plain "Spotify Free" title with active audio is also what Spotify shows for a moment after
// pressing play, so it only counts as an ad once it lasts this long.
constexpr uint64_t kUncertainAdMs = 2000;

TitleKind ClassifyTitle(std::wstring_view title);
const char* AdStateName(AdState s);

class AdStateMachine {
public:
    // An ad title enters Ad at once. A plain title enters Ad only with an active session that is also audible
    // (a paused player keeps its session active, silently) and only after kUncertainAdMs; sound starts that
    // wait and a quiet sample does not cancel it while the session stays active. Once in Ad, a plain
    // title with an active session stays Ad whatever the meter says: our own mute makes an ad read silent.
    // A song title leaves Ad at once; going idle or losing Spotify waits for kAdExitDebounceMs.
    AdState Update(TitleKind kind, bool audioActive, uint64_t nowMs, bool audible);
    AdState Current() const { return state_; }
    // Remaining wait in ms before a pending transition takes effect, 0 when none is pending.
    uint64_t PendingMs(uint64_t nowMs) const;

private:
    AdState state_ = AdState::NoSpotify;
    bool pending_ = false;
    AdState pendingState_ = AdState::NoSpotify;
    uint64_t pendingSince_ = 0;
    uint64_t pendingNeed_ = 0;
};
