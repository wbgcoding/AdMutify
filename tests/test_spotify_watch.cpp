#include "spotify_watch.h"
#include "test.h"

TEST(spotify_process_name_match) {
    CHECK(IsSpotifyProcessName(L"C:\\x\\SPOTIFY.EXE"));
    CHECK(IsSpotifyProcessName(L"Spotify.exe"));
    CHECK(IsSpotifyProcessName(L"C:/Users/x/AppData/Roaming/Spotify/Spotify.exe"));
    CHECK(!IsSpotifyProcessName(L"Spotify Helper.exe"));
    CHECK(!IsSpotifyProcessName(L"C:\\Spotify\\notspotify.exe"));
    CHECK(!IsSpotifyProcessName(L""));
}

TEST(spotify_target_name_override) {
    SetTargetProcessName(L"Other.exe");
    CHECK(IsSpotifyProcessName(L"C:\\a\\other.exe"));
    CHECK(!IsSpotifyProcessName(L"C:\\a\\Spotify.exe"));
    SetTargetProcessName(L"Spotify.exe");
    CHECK(IsSpotifyProcessName(L"C:\\a\\Spotify.exe"));
}

TEST(spotify_own_process_is_not_target) {
    CHECK(!IsTargetProcess(GetCurrentProcessId()));
    CHECK(!IsTargetProcess(0));
}
