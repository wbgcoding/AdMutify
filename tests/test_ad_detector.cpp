#include <cstdio>
#include <string_view>
#include <vector>

#include "ad_detector.h"
#include "test.h"

TEST(classify_title_table) {
    struct Row {
        const wchar_t* title;
        TitleKind kind;
    };
    const Row rows[] = {
        {L"Daft Punk - One More Time", TitleKind::Music},
        {L"Spotify Free", TitleKind::IdleOrAd},
        {L"Spotify Premium", TitleKind::IdleOrAd},
        {L"Spotify", TitleKind::IdleOrAd},
        {L"Advertisement", TitleKind::Ad},
        {L"ADVERTISEMENT", TitleKind::Ad},
        {L"advertisement", TitleKind::Ad},
        {L"Zalando", TitleKind::Ad},
        {L"", TitleKind::NoSpotify},
        {L"   ", TitleKind::NoSpotify},
        {L"Podcast Name - Episode 3", TitleKind::Music},
        {L"Beyoncé - Halo", TitleKind::Music},
        {L"McDonald's", TitleKind::Ad},
        {L"AC-DC", TitleKind::Ad},
        {L"AC/DC - Thunderstruck", TitleKind::Music},
        {L"Spotify Free - extra", TitleKind::Music},
        {L"Spotify Family", TitleKind::Ad},
        {L"Spotify - Premium testen", TitleKind::Ad},
        {L"spotify - Try Premium", TitleKind::Ad},
        {L"Spotify Singles - Song", TitleKind::Music},
    };
    for (const Row& r : rows) CHECK(ClassifyTitle(r.title) == r.kind);
}

TEST(state_music_to_ad_immediate) {
    AdStateMachine m;
    CHECK(m.Update(TitleKind::Music, false, 0, true) == AdState::Music);
    CHECK(m.Update(TitleKind::Ad, false, 10, true) == AdState::Ad);
}

TEST(state_ad_exit_to_idle_debounced) {
    AdStateMachine m;
    m.Update(TitleKind::Ad, false, 1000, true);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 2000, true) == AdState::Ad);
    CHECK(m.PendingMs(2000) == 600);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 2599, true) == AdState::Ad);
    CHECK(m.PendingMs(2599) == 1);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 2600, true) == AdState::Idle);
    CHECK(m.PendingMs(2600) == 0);
}

TEST(state_ad_exit_to_music_immediate) {
    AdStateMachine m;
    m.Update(TitleKind::Ad, false, 0, true);
    CHECK(m.Update(TitleKind::Music, false, 100, true) == AdState::Music);
    CHECK(m.PendingMs(100) == 0);
}

TEST(state_ad_flicker_to_idle_stays_ad) {
    AdStateMachine m;
    m.Update(TitleKind::Ad, false, 0, true);
    CHECK(m.Update(TitleKind::NoSpotify, false, 100, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::Ad, false, 300, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::NoSpotify, false, 400, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::NoSpotify, false, 999, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::NoSpotify, false, 1000, true) == AdState::NoSpotify);
}

TEST(state_pending_candidate_change_restarts_timer) {
    AdStateMachine m;
    m.Update(TitleKind::Ad, false, 0, true);
    CHECK(m.Update(TitleKind::NoSpotify, false, 100, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 500, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 1099, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 1100, true) == AdState::Idle);
}

TEST(state_play_pressed_is_not_an_ad) {
    // Pressing play: audio starts while the title still says "Spotify Free", the song title follows.
    AdStateMachine m;
    CHECK(m.Update(TitleKind::IdleOrAd, false, 0, true) == AdState::Idle);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 1000, true) == AdState::Idle);
    CHECK(m.PendingMs(1000) == 2000);
    CHECK(m.Update(TitleKind::Music, true, 1600, true) == AdState::Music);
    CHECK(m.PendingMs(1600) == 0);
}

TEST(state_idle_title_with_audio_becomes_ad_after_wait) {
    AdStateMachine m;
    CHECK(m.Update(TitleKind::IdleOrAd, false, 0, true) == AdState::Idle);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 10, true) == AdState::Idle);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 2009, true) == AdState::Idle);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 2010, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 2020, true) == AdState::Ad);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 2620, true) == AdState::Idle);
}

TEST(state_audio_stops_before_wait_cancels_ad) {
    AdStateMachine m;
    m.Update(TitleKind::IdleOrAd, false, 0, true);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 100, true) == AdState::Idle);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 900, true) == AdState::Idle);
    CHECK(m.PendingMs(900) == 0);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 2500, true) == AdState::Idle);
}

TEST(state_no_spotify_and_names) {
    AdStateMachine m;
    CHECK(m.Update(TitleKind::NoSpotify, false, 0, true) == AdState::NoSpotify);
    CHECK(std::string_view(AdStateName(AdState::Ad)) == "Ad");
    CHECK(std::string_view(AdStateName(AdState::NoSpotify)) == "NoSpotify");
}

TEST(state_paused_title_with_fading_audio_is_not_an_ad) {
    // After "Pause Spotify" the title turns to the plain idle one while the session may stay active briefly.
    AdStateMachine m;
    CHECK(m.Update(TitleKind::Music, true, 0, true) == AdState::Music);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 100, true) == AdState::Music);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 1500, true) == AdState::Music);
    CHECK(m.Update(TitleKind::IdleOrAd, false, 1700, true) == AdState::Idle);
    // A session that stays active and audible past kUncertainAdMs is an ad; the app suppresses that while it holds a pause.
    CHECK(m.Update(TitleKind::IdleOrAd, true, 2000, true) == AdState::Idle);
    CHECK(m.Update(TitleKind::IdleOrAd, true, 2000 + kUncertainAdMs, true) == AdState::Ad);
}

TEST(state_plain_title_ad_needs_audible_sound_table) {
    struct Step {
        TitleKind kind;
        bool active;
        bool audible;
        uint64_t at;
        AdState expect;
    };
    struct Case {
        const char* name;
        std::vector<Step> steps;
    };
    const TitleKind P = TitleKind::IdleOrAd;
    const std::vector<Case> cases = {
        // The user paused: plain title, session still active, no sound. Never an ad.
        {"paused session stays idle",
         {{P, true, false, 0, AdState::Idle}, {P, true, false, 2500, AdState::Idle}, {P, true, false, 60000, AdState::Idle}}},
        // A real ad: active and audible, Ad after the debounce (the machine starts without Spotify).
        {"audible ad after debounce",
         {{P, true, true, 0, AdState::NoSpotify}, {P, true, true, 1999, AdState::NoSpotify}, {P, true, true, 2000, AdState::Ad}}},
        // We muted the ad: the meter reads silence but the title and session did not change.
        {"muted ad stays ad",
         {{P, true, true, 0, AdState::NoSpotify},
          {P, true, true, 2000, AdState::Ad},
          {P, true, false, 2500, AdState::Ad},
          {P, true, false, 90000, AdState::Ad}}},
        // The muted ad ends: session inactive, then the debounce to Idle.
        {"muted ad leaves when the session ends",
         {{P, true, true, 0, AdState::NoSpotify},
          {P, true, true, 2000, AdState::Ad},
          {P, true, false, 3000, AdState::Ad},
          {P, false, false, 4000, AdState::Ad},
          {P, false, false, 4600, AdState::Idle}}},
        // Sound that only starts later: the debounce counts from the first audible poll.
        {"silent start then sound",
         {{P, true, false, 0, AdState::Idle},
          {P, true, true, 1000, AdState::Idle},
          {P, true, true, 2999, AdState::Idle},
          {P, true, true, 3000, AdState::Ad}}},
        // A quiet sample while the wait runs (speech gap) does not cancel it.
        {"silent sample during the wait",
         {{P, true, true, 0, AdState::NoSpotify}, {P, true, false, 1000, AdState::NoSpotify}, {P, true, true, 2000, AdState::Ad}}},
        {"silent samples to the end of the wait",
         {{P, true, true, 0, AdState::NoSpotify}, {P, true, false, 1000, AdState::NoSpotify}, {P, true, false, 2000, AdState::Ad}}},
        // The session ending cancels the wait as before.
        {"session ends during the wait",
         {{P, true, true, 0, AdState::NoSpotify}, {P, false, false, 1000, AdState::Idle}, {P, true, false, 2000, AdState::Idle}}},
        // A song title (" - ") is Music at once, whatever the meter says.
        {"song title after ad",
         {{P, true, true, 0, AdState::NoSpotify}, {P, true, true, 2000, AdState::Ad}, {TitleKind::Music, true, false, 2100, AdState::Music}}},
        {"song title on a paused session", {{P, true, false, 0, AdState::Idle}, {TitleKind::Music, true, false, 10, AdState::Music}}},
    };
    for (const Case& c : cases) {
        AdStateMachine m;
        for (const Step& s : c.steps) {
            AdState got = m.Update(s.kind, s.active, s.at, s.audible);
            if (got != s.expect) std::printf("case %s at %llu\n", c.name, static_cast<unsigned long long>(s.at));
            CHECK(got == s.expect);
        }
    }
}

TEST(ad_session_guard_ends_ad_without_sound) {
    AdSessionGuard g;
    // Entering Ad with a silent session is left alone.
    CHECK(!g.Update(false, true, false, 0));
    CHECK(!g.Update(true, true, false, 100));
    CHECK(!g.Update(true, true, false, 100 + kAdSessionGoneMs - 1));
    CHECK(g.Update(true, true, false, 100 + kAdSessionGoneMs));
    // Stays over after the machine left Ad, until the session plays again.
    CHECK(g.Update(false, true, false, 100 + kAdSessionGoneMs + 1000));
    CHECK(!g.Update(false, true, true, 100 + kAdSessionGoneMs + 2000));
    // Sound in between restarts the wait.
    CHECK(!g.Update(true, true, false, 20000));
    CHECK(!g.Update(true, true, true, 20000 + kAdSessionGoneMs));
    CHECK(!g.Update(true, true, false, 20000 + kAdSessionGoneMs + 1));
    // A song title resets it.
    CHECK(!g.Update(true, false, false, 40000));
}

