#include "ad_detector.h"

#include "util.h"

namespace {

std::wstring_view Trim(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.remove_suffix(1);
    return s;
}

}  // namespace

TitleKind ClassifyTitle(std::wstring_view title) {
    title = Trim(title);
    if (title.empty()) return TitleKind::NoSpotify;
    if (EqualsNoCase(title, L"Advertisement")) return TitleKind::Ad;
    if (title == L"Spotify" || title == L"Spotify Free" || title == L"Spotify Premium") return TitleKind::IdleOrAd;
    size_t dash = title.find(L" - ");
    if (dash != std::wstring_view::npos) {
        // Spotify's own promotions show "Spotify" as the artist.
        return EqualsNoCase(Trim(title.substr(0, dash)), L"Spotify") ? TitleKind::Ad : TitleKind::Music;
    }
    return TitleKind::Ad;
}

const char* AdStateName(AdState s) {
    switch (s) {
        case AdState::NoSpotify: return "NoSpotify";
        case AdState::Idle: return "Idle";
        case AdState::Music: return "Music";
        case AdState::Ad: return "Ad";
    }
    return "?";
}

AdState AdStateMachine::Update(TitleKind kind, bool audioActive, uint64_t nowMs, bool audible) {
    AdState candidate = AdState::NoSpotify;
    switch (kind) {
        case TitleKind::NoSpotify: candidate = AdState::NoSpotify; break;
        case TitleKind::Music: candidate = AdState::Music; break;
        case TitleKind::Ad: candidate = AdState::Ad; break;
        case TitleKind::IdleOrAd:
            if (!audioActive) candidate = AdState::Idle;
            else if (state_ == AdState::Ad) candidate = AdState::Ad;
            // Sound only starts the wait; a quiet sample in a speech gap does not cancel it.
            else if (audible || (pending_ && pendingState_ == AdState::Ad)) candidate = AdState::Ad;
            else candidate = AdState::Idle;
            break;
    }

    if (candidate == state_) {
        pending_ = false;
        return state_;
    }

    uint64_t need = 0;
    if (candidate == AdState::Ad) {
        if (kind == TitleKind::IdleOrAd) need = kUncertainAdMs;
    } else if (state_ == AdState::Ad && candidate != AdState::Music) {
        need = kAdExitDebounceMs;
    }

    if (need == 0) {
        state_ = candidate;
        pending_ = false;
        return state_;
    }

    if (!pending_ || pendingState_ != candidate) {
        pending_ = true;
        pendingState_ = candidate;
        pendingSince_ = nowMs;
        pendingNeed_ = need;
    }
    if (nowMs - pendingSince_ >= pendingNeed_) {
        state_ = candidate;
        pending_ = false;
    }
    return state_;
}

uint64_t AdStateMachine::PendingMs(uint64_t nowMs) const {
    if (!pending_) return 0;
    uint64_t elapsed = nowMs - pendingSince_;
    return elapsed >= pendingNeed_ ? 0 : pendingNeed_ - elapsed;
}

bool AdSessionGuard::Update(bool inAd, bool adTitle, bool sessionActive, uint64_t nowMs) {
    if (!adTitle || sessionActive) {
        counting_ = false;
        gone_ = false;
        return false;
    }
    if (gone_) return true;
    if (!inAd) {
        counting_ = false;
        return false;
    }
    if (!counting_) {
        counting_ = true;
        since_ = nowMs;
    }
    gone_ = nowMs - since_ >= kAdSessionGoneMs;
    return gone_;
}
