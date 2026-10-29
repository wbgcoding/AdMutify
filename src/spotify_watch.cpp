#include "spotify_watch.h"

#include <iterator>
#include <vector>

#include "util.h"

namespace {

struct ProcEntry {
    FILETIME start;
    bool match;
};

// Only the UI thread asks (window events, session scans), so the cache needs no lock.
std::unordered_map<DWORD, ProcEntry> g_cache;
std::wstring g_targetName = L"Spotify.exe";
SpotifyWatch* g_instance = nullptr;

constexpr size_t kMaxCacheEntries = 256;
constexpr size_t kMaxRejected = 1024;
constexpr wchar_t kMainClassPrefix[] = L"Chrome_WidgetWin_";

bool SameFileTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

std::wstring WindowTitle(HWND hwnd) {
    wchar_t buf[512];
    int n = GetWindowTextW(hwnd, buf, static_cast<int>(std::size(buf)));
    return std::wstring(buf, n > 0 ? static_cast<size_t>(n) : 0);
}

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM param) {
    auto* list = reinterpret_cast<std::vector<HWND>*>(param);
    list->push_back(hwnd);
    return TRUE;
}

}  // namespace

bool IsSpotifyProcessName(std::wstring_view imagePath) {
    size_t slash = imagePath.find_last_of(L"\\/");
    std::wstring_view file = slash == std::wstring_view::npos ? imagePath : imagePath.substr(slash + 1);
    return EqualsNoCase(file, g_targetName);
}

void SetTargetProcessName(const wchar_t* name) {
    g_targetName = name;
    g_cache.clear();
}

// lookupFailed is set when the process could not be inspected (as opposed to inspected and found foreign).
static bool QueryTarget(DWORD pid, bool& lookupFailed) {
    lookupFailed = false;
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        lookupFailed = true;
        return false;
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    bool haveTimes = GetProcessTimes(h, &created, &exited, &kernel, &user) != 0;

    auto it = g_cache.find(pid);
    if (it != g_cache.end() && haveTimes && SameFileTime(it->second.start, created)) {
        CloseHandle(h);
        return it->second.match;
    }

    bool match = IsSpotifyProcessName(ImageFileName(h));
    CloseHandle(h);

    if (haveTimes) {
        if (g_cache.size() >= kMaxCacheEntries) g_cache.clear();
        g_cache[pid] = ProcEntry{created, match};
    }
    return match;
}

bool IsTargetProcess(DWORD pid) {
    bool failed = false;
    return QueryTarget(pid, failed);
}

SpotifyWatch::~SpotifyWatch() { Stop(); }

bool SpotifyWatch::IsMainCandidate(HWND hwnd) {
    // Class and owning process never change for a window, so a mismatch is remembered until its destroy event.
    if (rejected_.count(hwnd)) return false;
    if (!IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
    wchar_t cls[64];
    int n = GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    bool match = n >= static_cast<int>(std::size(kMainClassPrefix)) - 1 &&
                 wcsncmp(cls, kMainClassPrefix, std::size(kMainClassPrefix) - 1) == 0;
    bool lookupFailed = false;
    if (match) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        match = QueryTarget(pid, lookupFailed);
    }
    // A failed process lookup may be temporary, so only a real mismatch is remembered.
    if (!match && !lookupFailed) {
        if (rejected_.size() >= kMaxRejected) rejected_.clear();
        rejected_.insert(hwnd);
    }
    return match;
}

bool SpotifyWatch::Start(ChangeCallback onChange) {
    if (hookRange_) return true;
    onChange_ = std::move(onChange);
    g_instance = this;
    // Show and destroy events from every process find Spotify's windows; title changes are only
    // subscribed for Spotify's own process (SyncNameHooks), since they fire constantly system-wide.
    hookRange_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, nullptr, &HookProc, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!hookRange_) {
        Stop();
        return false;
    }
    Rescan(true);
    return true;
}

std::wstring SpotifyWatch::DescribeTargetWindows() {
    std::vector<HWND> all;
    EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&all));
    std::wstring out;
    for (HWND w : all) {
        if (!IsWindowVisible(w)) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (!IsTargetProcess(pid)) continue;
        wchar_t cls[64] = {};
        GetClassNameW(w, cls, static_cast<int>(std::size(cls)));
        RECT r{};
        GetWindowRect(w, &r);
        if (!out.empty()) out += L" | ";
        out += std::wstring(cls) + L" " + std::to_wstring(r.right - r.left) + L"x" + std::to_wstring(r.bottom - r.top) + L" \"" +
               WindowTitle(w) + L"\"";
    }
    return out;
}

void SpotifyWatch::RescanIfUnknown() {
    if (hookRange_ && !main_) Rescan(true);
}

void SpotifyWatch::RescanIfStale() {
    if (hookRange_ && main_ && !IsWindow(main_)) Rescan(true);
}

void SpotifyWatch::SyncNameHooks() {
    std::unordered_set<DWORD> pids;
    for (const auto& entry : lastChange_) {
        DWORD pid = 0;
        if (GetWindowThreadProcessId(entry.first, &pid) && pid) pids.insert(pid);
    }
    for (auto it = nameHooks_.begin(); it != nameHooks_.end();) {
        if (pids.count(it->first)) {
            ++it;
            continue;
        }
        UnhookWinEvent(it->second);
        it = nameHooks_.erase(it);
    }
    for (DWORD pid : pids) {
        if (nameHooks_.count(pid)) continue;
        HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, &HookProc, pid, 0,
                                             WINEVENT_OUTOFCONTEXT);
        if (hook) nameHooks_.emplace(pid, hook);
    }
}

void SpotifyWatch::Stop() {
    for (const auto& entry : nameHooks_) UnhookWinEvent(entry.second);
    nameHooks_.clear();
    if (hookRange_) UnhookWinEvent(hookRange_);
    hookRange_ = nullptr;
    lastChange_.clear();
    rejected_.clear();
    if (g_instance == this) g_instance = nullptr;
}

void CALLBACK SpotifyWatch::HookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd || !g_instance) return;
    g_instance->OnEvent(event, hwnd);
}

void SpotifyWatch::OnEvent(DWORD event, HWND hwnd) {
    ++callbacks_;
    if (event == EVENT_OBJECT_DESTROY) {
        // The handle is already invalid here, so only compare it.
        const bool tracked = lastChange_.erase(hwnd) != 0;
        rejected_.erase(hwnd);
        if (hwnd == main_) Rescan(true);
        else if (tracked) SyncNameHooks();
        return;
    }
    auto known = lastChange_.find(hwnd);
    const bool added = known == lastChange_.end();
    if (added) {
        if (!IsMainCandidate(hwnd)) return;
        known = lastChange_.emplace(hwnd, 0).first;
    }
    if (event == EVENT_OBJECT_NAMECHANGE) known->second = GetTickCount64();
    Rescan(false, added);
}

void SpotifyWatch::Rescan(bool full, bool windowsChanged) {
    // Drop stale handles, then pick the candidate with the newest title change.
    for (auto it = lastChange_.begin(); it != lastChange_.end();) {
        if (!IsWindow(it->first)) {
            it = lastChange_.erase(it);
            windowsChanged = true;
        } else {
            ++it;
        }
    }
    if (full) {
        // Initial state, or fallback after the main window went away.
        std::vector<HWND> all;
        EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&all));
        for (HWND w : all) {
            if (lastChange_.count(w)) continue;
            if (IsMainCandidate(w)) lastChange_.emplace(w, 0);
        }
    }

    HWND best = nullptr;
    ULONGLONG bestTick = 0;
    bool bestVisible = false;
    for (const auto& [w, tick] : lastChange_) {
        if (WindowTitle(w).empty()) continue;
        // On equal ticks a visible window wins over a hidden one, then the current main window.
        const bool visible = IsWindowVisible(w) != FALSE;
        if (!best || tick > bestTick ||
            (tick == bestTick && (visible > bestVisible || (visible == bestVisible && w == main_)))) {
            best = w;
            bestTick = tick;
            bestVisible = visible;
        }
    }
    // The per-process title hooks only change with the window set; a title change alone keeps them.
    if (full || windowsChanged) SyncNameHooks();
    Publish(best);
}

void SpotifyWatch::Publish(HWND best) {
    std::wstring title = best ? WindowTitle(best) : std::wstring();
    bool changed = best != main_ || title != title_;
    main_ = best;
    title_ = title;
    if (changed && onChange_) onChange_(title_);
}
