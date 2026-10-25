#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "settings.h"

// Pure parts
// True for the audio formats the Windows media engine plays: mp3, m4a, aac, wma, wav, flac, mp2 and webm everywhere;
// ogg, oga, opus and mka need the Web Media Extensions that ship with Windows (missing on N editions).
bool IsMusicFile(std::wstring_view name);
// The same formats as an open dialog filter ("*.mp3;*.m4a;...") and as a list for messages ("MP3, M4A, ...").
std::wstring MusicFilterSpec();
std::wstring MusicTypeList();
// Explorer-like name order (case folded, digit runs compared as numbers).
bool MusicNameLess(const std::wstring& a, const std::wstring& b);
// Playable files in a folder and its subfolders, in name order; stops at maxFiles.
std::vector<std::wstring> ScanMusicFolder(const std::wstring& folder, size_t maxFiles = 5000);

// The order the tracks are played in. Shuffle plays every track once per round in random order; a new round never
// starts with the track that ended the last one.
class Playlist {
public:
    void Reset(std::vector<std::wstring> files, bool shuffle, uint32_t seed);
    bool Empty() const { return order_.empty(); }
    size_t Size() const { return order_.size(); }
    const std::wstring& Current() const;
    // Moves to the next track; true when a round just ended (the folder is worth scanning again).
    bool Next();
    // Drops the current track (it failed to play); the next one becomes current.
    void RemoveCurrent();

private:
    void Shuffle();
    std::vector<std::wstring> files_;
    std::vector<size_t> order_;
    size_t pos_ = 0;
    bool shuffle_ = false;
    uint32_t rng_ = 1;
};

// Plays the chosen music while an ad is muted or lowered. The player only exists during an ad: when the ad ends the
// music fades out, its file and position are kept and the next ad continues there.
class AdMusicPlayer {
public:
    AdMusicPlayer();
    ~AdMusicPlayer();
    AdMusicPlayer(const AdMusicPlayer&) = delete;
    AdMusicPlayer& operator=(const AdMusicPlayer&) = delete;

    // notifyWindow receives WM_APP_MUSIC; timerId is used on it for the fades.
    void Init(HWND notifyWindow, UINT_PTR timerId);
    // A changed choice or path starts over with a new track list; a changed volume applies at once.
    void Configure(AdMusic mode, const std::wstring& path, int volumePercent);
    // Fades in (continuing where the last ad stopped) or fades out; repeated calls with the same value do nothing.
    void SetActive(bool active);
    void OnEvent(WPARAM wParam, LPARAM lParam);
    void OnTimer();
    // Stops at once, without a fade.
    void Shutdown();

    // A track is open and playing (also while it fades out).
    bool IsPlaying() const;
    // Seconds into the current track: the playing position, or where the next ad continues.
    double Position() const;
    // Tracks left in the list; unplayable ones are dropped from it.
    size_t TrackCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
