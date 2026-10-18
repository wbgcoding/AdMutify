#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// True when the file name part of an image path matches the target process name (case-insensitive).
bool IsSpotifyProcessName(std::wstring_view imagePath);
// Default target is Spotify.exe; tests and the rig can change it.
void SetTargetProcessName(const wchar_t* name);
// True when the process with this id is the target program (cached per PID and start time).
bool IsTargetProcess(DWORD pid);

class SpotifyWatch {
public:
    using ChangeCallback = std::function<void(const std::wstring& title)>;

    ~SpotifyWatch();
    // Installs the event hooks and scans the existing windows once. Must run on a thread with a message loop.
    bool Start(ChangeCallback onChange);
    void Stop();
    // A Spotify that started hidden never shows a window event; call this on a hint (a new audio session).
    // Does a full scan only while no main window is known.
    void RescanIfUnknown();

    const std::wstring& Title() const { return title_; }
    HWND MainWindow() const { return main_; }
    // Number of hook callbacks seen (for the trace option).
    unsigned long CallbackCount() const { return callbacks_; }
    // Every visible top-level window of the target process with class, size and title, for the trace option.
    static std::wstring DescribeTargetWindows();

private:
    static void CALLBACK HookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    void OnEvent(DWORD event, HWND hwnd);
    bool IsMainCandidate(HWND hwnd);
    // windowsChanged: the caller added a window, so the title hooks are synced even on a partial rescan.
    void Rescan(bool full, bool windowsChanged = false);
    void Publish(HWND best);
    // One title-change hook per process that owns a candidate window.
    void SyncNameHooks();

    ChangeCallback onChange_;
    HWINEVENTHOOK hookRange_ = nullptr;
    HWND main_ = nullptr;
    std::wstring title_;
    unsigned long callbacks_ = 0;
    // Last title change per candidate window; the newest wins when several exist.
    std::unordered_map<HWND, ULONGLONG> lastChange_;
    // Windows already ruled out (other class or process), so their hook events cost one lookup.
    std::unordered_set<HWND> rejected_;
    std::unordered_map<DWORD, HWINEVENTHOOK> nameHooks_;
};
