#include "app.h"

#include <shellapi.h>
#include <windowsx.h>

#include <cstdio>

#include "ad_detector.h"
#include "ad_music.h"
#include "app_messages.h"
#include "audio_control.h"
#include "crash.h"
#include "media_detector.h"
#include "settings_window.h"
#include "spotify_watch.h"
#include "strings.h"
#include "theme.h"
#include "tray.h"
#include "util.h"
#include "version.h"

namespace {

constexpr UINT_PTR kTimerExit = 1;
constexpr UINT_PTR kTimerProbe = 2;
constexpr UINT_PTR kTimerMedia = 3;
constexpr UINT_PTR kTimerMusic = 4;
constexpr UINT kMediaPollMs = 500;
constexpr int kMaxProbes = 5;
enum class Reason { None, Ad, Media };

struct App {
    HWND hwnd = nullptr;
    AppOptions options;
    Settings settings;
    Tray tray;
    AudioControl audio;
    SpotifyWatch watch;
    AdStateMachine machine;
    bool applied = false;
    Mode appliedMode = Mode::Mute;
    int appliedPercent = 0;
    Reason appliedReason = Reason::None;
    MediaDetector media;
    AdMusicPlayer music;
    bool mediaTimerRunning = false;
    std::wstring lastMediaExe;
    // "Pause Spotify": we paused it for the running video / this video was already dealt with.
    bool pausedByUs = false;
    bool pauseHandled = false;
    uint64_t pausedAt = 0;
    // Spotify's session was seen inactive after our pause: from then on an active session is not our own pause.
    bool reevaluating = false;
    bool reevaluateAgain = false;
    bool probeRunning = false;
    int probes = 0;
    UINT taskbarCreated = 0;
    std::wstring status;
    std::wstring lastTraceTitle;
    bool closing = false;
    bool inMemory = false;
    // The tray exists; a second start before that only leaves a request for the tray icon.
    bool trayReady = false;
    bool showRequested = false;
};

App g;

const wchar_t* MutexName() {
    return g.options.secondInstance ? L"Local\\AdMutify.SingleInstance.2" : L"Local\\AdMutify.SingleInstance";
}

const wchar_t* ClassName() { return g.options.secondInstance ? L"AdMutify.Msg.2" : L"AdMutify.Msg"; }

const char* ModeName(Mode m) { return m == Mode::Lower ? "lower" : "mute"; }

const char* ReasonName(Reason r) { return r == Reason::Media ? "media" : "ad"; }

void UpdateTray() {
    TrayIconKind icon = TrayIconKind::On;
    if (!g.settings.enabled) {
        g.status = Str(StrId::StatusDisabled);
        icon = TrayIconKind::Off;
    } else if (g.machine.Current() == AdState::NoSpotify) {
        g.status = Str(StrId::StatusNoSpotify);
    } else if (g.applied && g.appliedReason == Reason::Media) {
        g.status = g.appliedMode == Mode::Lower ? FormatPercent(StrId::StatusMediaLowered, g.appliedPercent) : Str(StrId::StatusMediaMuted);
        icon = TrayIconKind::Muted;
    } else if (g.machine.Current() == AdState::Ad && g.applied) {
        g.status = g.settings.mode == Mode::Lower ? FormatPercent(StrId::StatusLowered, g.settings.lowerPercent) : Str(StrId::StatusMuted);
        icon = TrayIconKind::Muted;
    } else if (g.pausedByUs) {
        g.status = Str(StrId::StatusMediaPaused);
        icon = TrayIconKind::Muted;
    } else {
        g.status = Str(StrId::StatusActive);
    }
    g.tray.Update(icon, TooltipText(g.status.c_str()));
}

void DoApply(Mode mode, int percent, Reason reason) {
    int n = g.audio.Apply(mode, percent);
    g.applied = true;
    g.appliedMode = mode;
    g.appliedPercent = percent;
    g.appliedReason = reason;
    Log("apply mode=%s percent=%d sessions=%d reason=%s", ModeName(mode), percent, n, ReasonName(reason));
}

void DoRestore() {
    int userChanged = 0;
    int n = g.audio.Restore(&userChanged);
    Log("restore sessions=%d user_changed=%d pending=%d reason=%s", n, userChanged, g.audio.PendingCount(),
        ReasonName(g.appliedReason));
    g.applied = false;
    g.appliedReason = Reason::None;
}

// The media poll runs only while it can matter; stopping it also forgets any half-built detection.
void UpdateMediaTimer(AdState state) {
    bool want = g.settings.enabled && g.settings.media != MediaMode::Off && state != AdState::NoSpotify;
    if (want && !g.mediaTimerRunning) {
        SetTimer(g.hwnd, kTimerMedia, kMediaPollMs, nullptr);
        g.mediaTimerRunning = true;
    } else if (!want && g.mediaTimerRunning) {
        KillTimer(g.hwnd, kTimerMedia);
        g.mediaTimerRunning = false;
    }
    if (!want) {
        g.media.Reset();
        g.lastMediaExe.clear();
    }
}

// Toggles Spotify's playback like a media key would.
bool SendPlayPause() {
    HWND target = g.watch.MainWindow();
    if (!target) return false;
    DWORD_PTR result = 0;
    SetLastError(0);
    if (SendMessageTimeoutW(target, WM_APPCOMMAND, reinterpret_cast<WPARAM>(g.hwnd),
                            MAKELPARAM(0, APPCOMMAND_MEDIA_PLAY_PAUSE), SMTO_ABORTIFHUNG, 500, &result) != 0)
        return true;
    // A timeout leaves the command in Spotify's queue: it is processed later, so it counts as sent.
    return GetLastError() == ERROR_TIMEOUT;
}

// The "Pause Spotify" video choice. Without the choice (or with AdMutify disabled) nothing is wanted, so a pause
// we hold is given back like at the end of a video.
void HandlePause(AdState state, uint64_t now) {
    const bool want = g.settings.enabled && g.settings.media == MediaMode::Pause;
    const bool mediaActive = want && g.media.Active();
    PauseAction action = DecidePause(mediaActive, state, g.pausedByUs, g.pauseHandled);
    if (action == PauseAction::Forget && state == AdState::Music && now - g.pausedAt < kPauseSettleMs) action = PauseAction::None;
    switch (action) {
        case PauseAction::Pause: {
            g.pauseHandled = true;
            const bool sent = SendPlayPause();
            // The send pumps messages: a nested Shutdown must not be followed by state changes.
            if (g.closing) return;
            if (sent) {
                g.pausedByUs = true;
                g.pausedAt = now;
                SetCrashResumeWindow(g.watch.MainWindow());
                Log("media pause");
            } else {
                Log("media pause_failed");
            }
            break;
        }
        case PauseAction::Resume: {
            g.pausedByUs = false;
            SetCrashResumeWindow(nullptr);
            const bool sent = SendPlayPause();
            if (g.closing) return;
            Log(sent ? "media resume" : "media resume_failed");
            break;
        }
        case PauseAction::Forget:
            g.pausedByUs = false;
            SetCrashResumeWindow(nullptr);
            Log("media forget");
            break;
        case PauseAction::None: break;
    }
    if (!mediaActive) g.pauseHandled = false;
}

void ReevaluateOnce() {
    uint64_t now = GetTickCount64();
    TitleKind kind = ClassifyTitle(g.watch.Title());
    if (TraceEnabled() && g.watch.Title() != g.lastTraceTitle) {
        // Titles stay out of the normal log; only an explicit --trace run records them.
        g.lastTraceTitle = g.watch.Title();
        Log("trace title kind=%d \"%s\"", static_cast<int>(kind), WideToUtf8(g.lastTraceTitle).c_str());
    }
    // A paused player keeps its session active without sound, so a plain title only means an ad while Spotify is
    // audible. Once in Ad the meter is not asked (our own mute silences it) unless we hold a pause.
    bool audioActive = kind == TitleKind::IdleOrAd && g.audio.IsSpotifyAudioActive();
    bool audible = true;
    // A session we changed reads silent (the meter sits behind the session mute and volume), so with anything
    // applied the meter is not used: an ad behind a video reaction still counts as sound.
    if (audioActive && !g.applied && (g.pausedByUs || g.machine.Current() != AdState::Ad)) {
        audible = g.audio.IsSpotifyAudible();
        // While we hold a pause the plain title is expected and a silent active session is not an ad.
        if (SuppressAdAudio(g.pausedByUs, audible)) audioActive = false;
    }

    AdState before = g.machine.Current();
    AdState after = g.machine.Update(kind, audioActive, now, audible);
    if (after != before) Log("state %s -> %s", AdStateName(before), AdStateName(after));

    UpdateMediaTimer(after);
    HandlePause(after, now);
    // The play/pause send pumps messages: Windows may have ended the session meanwhile (Shutdown ran nested).
    if (g.closing) return;
    // An ad wins over video playback.
    Reason want = Reason::None;
    Mode wantMode = g.settings.mode;
    int wantPercent = g.settings.lowerPercent;
    if (g.settings.enabled) {
        if (after == AdState::Ad) {
            want = Reason::Ad;
        } else if ((g.settings.media == MediaMode::Lower || g.settings.media == MediaMode::Mute) && g.media.Active()) {
            want = Reason::Media;
            wantMode = g.settings.media == MediaMode::Mute ? Mode::Mute : Mode::Lower;
            wantPercent = g.settings.mediaPercent;
        }
    }
    if (want == Reason::None) {
        if (g.applied) DoRestore();
    } else if (!g.applied) {
        DoApply(wantMode, wantPercent, want);
    } else if (g.appliedMode != wantMode) {
        // Sessions the user overrode during this reaction stay untouched across the switch.
        int n = g.audio.SwitchMode(wantMode, wantPercent);
        g.applied = true;
        g.appliedMode = wantMode;
        g.appliedPercent = wantPercent;
        g.appliedReason = want;
        Log("switch mode=%s percent=%d sessions=%d reason=%s", ModeName(wantMode), wantPercent, n, ReasonName(want));
    } else if (wantMode == Mode::Lower && g.appliedPercent != wantPercent) {
        int n = g.audio.ChangeLevel(wantPercent);
        g.appliedPercent = wantPercent;
        Log("level percent=%d sessions=%d", g.appliedPercent, n);
    }
    if (g.applied && g.appliedReason != want && want != Reason::None) {
        Log("reason %s -> %s", ReasonName(g.appliedReason), ReasonName(want));
        g.appliedReason = want;
    }

    if (before == AdState::NoSpotify && after != AdState::NoSpotify) g.audio.WatchSessions();
    if (before != AdState::NoSpotify && after == AdState::NoSpotify) g.audio.UnwatchSessions();

    uint64_t pending = g.machine.PendingMs(now);
    if (pending > 0) SetTimer(g.hwnd, kTimerExit, static_cast<UINT>(pending), nullptr);
    else KillTimer(g.hwnd, kTimerExit);

    if (kind == TitleKind::IdleOrAd) {
        if (!g.probeRunning && g.probes < kMaxProbes) {
            SetTimer(g.hwnd, kTimerProbe, 1000, nullptr);
            g.probeRunning = true;
        }
    } else {
        KillTimer(g.hwnd, kTimerProbe);
        g.probeRunning = false;
        g.probes = 0;
    }

    // Own music plays while an ad is muted or lowered; video reactions stay quiet.
    g.music.SetActive(g.applied && g.appliedReason == Reason::Ad);

    if (TraceEnabled()) Log("trace callbacks=%lu", g.watch.CallbackCount());
    UpdateTray();
}

// A call that arrives while another one runs (the play/pause send pumps messages) is not lost: the running
// call goes round once more when it is done.
void Reevaluate() {
    if (g.closing) return;
    if (g.reevaluating) {
        g.reevaluateAgain = true;
        return;
    }
    g.reevaluating = true;
    struct Done {
        ~Done() { g.reevaluating = false; }
    } done;
    for (int pass = 0; pass < 3 && !g.closing; ++pass) {
        g.reevaluateAgain = false;
        ReevaluateOnce();
        if (!g.reevaluateAgain) break;
    }
    // A request that came in during the last pass runs from the message loop.
    if (g.reevaluateAgain && !g.closing) PostMessageW(g.hwnd, WM_APP_AUDIO_STATE, 0, 0);
}

// endSession: Windows is signing out or shutting down; music must not start during that, so no resume toggle.
void Shutdown(bool endSession = false) {
    if (g.closing) return;
    g.closing = true;
    KillTimer(g.hwnd, kTimerExit);
    KillTimer(g.hwnd, kTimerProbe);
    KillTimer(g.hwnd, kTimerMedia);
    g.mediaTimerRunning = false;
    // Spotify must not stay paused because of AdMutify.
    // Still showing the song title shortly after our pause means the pause has not shown in the title yet.
    const AdState current = g.machine.Current();
    const bool settling = current == AdState::Music && GetTickCount64() - g.pausedAt < kPauseSettleMs;
    if (g.pausedByUs && !endSession && (current == AdState::Idle || settling)) {
        Log(SendPlayPause() ? "media resume" : "media resume_failed");
    }
    g.pausedByUs = false;
    SetCrashResumeWindow(nullptr);
    if (g.applied) DoRestore();
    g.music.Shutdown();
    g.tray.Hide();
    g.watch.Stop();
    if (TraceEnabled()) Log("trace callbacks=%lu", g.watch.CallbackCount());
    g.audio.Shutdown();
    Log("exit");
}

void ShowTrayMenu(POINT pt) {
    UINT cmd = g.tray.ShowMenu(pt, g.settings.enabled, g.status);
    switch (cmd) {
        case IDM_TRAY_ENABLED: {
            Settings s = g.settings;
            s.enabled = !s.enabled;
            CommitSettings(s);
            break;
        }
        case IDM_TRAY_SETTINGS: OpenSettingsWindow(); break;
        case IDM_TRAY_EXIT: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
        default: break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g.taskbarCreated && g.taskbarCreated != 0) {
        g.tray.OnTaskbarCreated();
        return 0;
    }
    switch (msg) {
        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT: {
                    POINT pt{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
                    ShowTrayMenu(pt);
                    break;
                }
                default: break;
            }
            return 0;
        case WM_APP_SHOW:
            if (!g.trayReady) {
                g.showRequested = true;
            } else if (!g.tray.Visible()) {
                g.tray.Show();
                Log("show_icon reason=second_instance");
            }
            return 0;
        case WM_APP_AUDIO_STATE: Reevaluate(); return 0;
        case WM_APP_MUSIC: g.music.OnEvent(wParam, lParam); return 0;
        case WM_APP_SESSION_CREATED:
            g.watch.RescanIfUnknown();
            g.audio.WatchSessions();
            if (g.applied) {
                int n = g.audio.Apply(g.appliedMode, g.appliedPercent);
                if (n > 0)
                    Log("apply mode=%s percent=%d sessions=%d reason=%s", ModeName(g.appliedMode), g.appliedPercent, n,
                        ReasonName(g.appliedReason));
            }
            Reevaluate();
            return 0;
        case WM_TIMER:
            if (wParam == kTimerExit) {
                KillTimer(hwnd, kTimerExit);
                Reevaluate();
            } else if (wParam == kTimerMedia) {
                bool before = g.media.Active();
                std::wstring exe;
                g.media.Update(g.audio.IsContentSoundPlaying(TraceEnabled() ? &exe : nullptr), GetTickCount64());
                if (g.media.Active() != before) Log("media %s", g.media.Active() ? "on" : "off");
                // Process names stay out of the normal log; only an explicit --trace run records them.
                if (TraceEnabled() && exe != g.lastMediaExe) {
                    g.lastMediaExe = exe;
                    Log("trace media_exe \"%s\"", WideToUtf8(exe).c_str());
                }
                Reevaluate();
            } else if (wParam == kTimerMusic) {
                g.music.OnTimer();
            } else if (wParam == kTimerProbe) {
                if (++g.probes >= kMaxProbes) {
                    KillTimer(hwnd, kTimerProbe);
                    g.probeRunning = false;
                }
                Reevaluate();
            }
            return 0;
        case WM_MEASUREITEM:
            if (TrayMenuMeasure(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) return TRUE;
            break;
        case WM_DRAWITEM:
            if (TrayMenuDraw(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam))) return TRUE;
            break;
        case WM_DPICHANGED: g.tray.ReloadIcons(); return 0;
        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETHIGHCONTRAST ||
                (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0))
                RefreshThemeCache();
            break;
        case WM_SYSCOLORCHANGE: RefreshThemeCache(); break;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION:
            if (wParam) Shutdown(true);
            return 0;
        case WM_CLOSE:
            Shutdown();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

const Settings& CurrentSettings() { return g.settings; }

static void ShowSaveError(HWND owner, unsigned long error, const std::wstring& path) {
    // Sized by the path: long paths are allowed, and a fixed buffer would trip the CRT's invalid-parameter exit.
    std::wstring tech(path.size() + 128, L'\0');
    _snwprintf_s(tech.data(), tech.size(), _TRUNCATE, Str(StrId::ErrTechnical), error, path.c_str());
    tech.resize(wcslen(tech.c_str()));
    std::wstring text = std::wstring(Str(StrId::ErrSettingsSave)) + L"\n\n" + tech;
    MessageBoxW(owner, text.c_str(), L"AdMutify", MB_OK | MB_ICONWARNING);
}

#ifdef ADMUTIFY_REVIEW
void AppUseInMemorySettings(const Settings& settings) {
    g.settings = settings;
    g.inMemory = true;
    SetLanguage(settings.language);
}
#endif

namespace {

void ApplyCommit(const Settings& requested) {
    Settings old = g.settings;
    Settings s = requested;
    HWND owner = SettingsWindowHandle() ? SettingsWindowHandle() : g.hwnd;
    unsigned long err = 0;
    if (!g.inMemory && SettingsReadFailed()) {
        // The file was unreadable at start: try again now. When it reads, its values are the base and only what
        // the user just changed is applied on top; otherwise the change stays in memory and the user is told.
        Settings fromFile;
        if (RetrySettingsRead(fromFile, &err)) {
            s = MergeSettingsChange(fromFile, old, requested);
            Log("settings_read_recovered");
        } else {
            ShowSaveError(owner, err, SettingsPath());
        }
    }
    g.settings = s;
    SetLanguage(s.language);
    if (g.inMemory) return;

    // The box is modal to the settings window when it is open, so it cannot start another change meanwhile.
    if (!SaveSettings(s, &err)) ShowSaveError(owner, err, SettingsPath());

    if (s.enabled != old.enabled) Log("enabled=%d", s.enabled ? 1 : 0);
    if (s.darkMenus != old.darkMenus) ApplyMenuAppMode(s.darkMenus ? 1 : 3);
    g.music.Configure(s.adMusic, s.adMusicPath, s.adMusicVolume);
    // Reevaluate re-applies a changed mode or level and drops a reaction that is no longer wanted.
    Reevaluate();
}

}  // namespace

void CommitSettings(const Settings& s) {
    // The tray menu can still change settings while the save error box is open: such a change waits
    // until the running one is done, so a stale value never overwrites a newer one.
    static bool busy = false;
    static bool hasQueued = false;
    static Settings queued;
    if (busy) {
        queued = s;
        hasQueued = true;
        return;
    }
    busy = true;
    ApplyCommit(s);
    while (hasQueued) {
        hasQueued = false;
        ApplyCommit(queued);
    }
    busy = false;
}

int RunApp(HINSTANCE inst, const AppOptions& options) {
    g.options = options;

    if (options.trace) SetTraceEnabled(true);

    HANDLE mutex = CreateMutexW(nullptr, FALSE, MutexName());
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(ClassName(), nullptr);
        if (other) PostMessageW(other, WM_APP_SHOW, 0, 0);
        CloseHandle(mutex);
        return 0;
    }

    // Only the instance that stays gets a crash handler and a log folder.
    InstallCrashHandler();
    ReserveStackGuarantee();

    LoadResult loaded = LoadOrCreateSettings();
    g.settings = loaded.settings;
    SetLanguage(g.settings.language);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = ClassName();
    if (!RegisterClassExW(&wc)) return 1;
    g.hwnd = CreateWindowExW(0, ClassName(), L"AdMutify", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g.hwnd) return 1;
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    Log("start version=%s portable=%d hide_tray=%d", ADMUTIFY_VERSION_STR, IsPortable() ? 1 : 0, g.settings.hideTray ? 1 : 0);
    if (loaded.readFailed) Log("settings_read_failed err=%lu", loaded.readError);

    // A report left over from a run that crashed again before showing it comes first, before anything can crash.
    CrashStartupBegin();

    g.audio.Init(g.hwnd);
    g.music.Init(g.hwnd, kTimerMusic);
    g.music.Configure(g.settings.adMusic, g.settings.adMusicPath, g.settings.adMusicVolume);
    bool stateIgnored = false;
    int recovered = g.audio.RestoreFromStateFile(&stateIgnored);
    if (stateIgnored) Log("state_file_ignored");
    if (recovered > 0) Log("restore sessions=%d user_changed=0", recovered);
    if (g.audio.PendingCount() > 0) Log("restore_pending count=%d", g.audio.PendingCount());

    if (g.settings.darkMenus) ApplyMenuAppMode(1);

    g.tray.Init(g.hwnd, inst);
    g.trayReady = true;
    UpdateTray();
    // "Start without tray icon" applies to the Windows autostart only; a manual start always shows the icon.
    if (!g.settings.hideTray || !options.autostart) {
        g.tray.Show();
        Log("show_icon reason=start");
    } else if (g.showRequested) {
        g.tray.Show();
        Log("show_icon reason=second_instance");
    }

    g.watch.Start([](const std::wstring&) { Reevaluate(); });
    Reevaluate();

    // The message window exists and a leftover mute is already undone: the dialogs may wait for the user now.
    ShowCrashDialogIfNeeded(g.audio.PendingCount());
    if (loaded.newerVersion) MessageBoxW(nullptr, Str(StrId::ErrSettingsNewer), L"AdMutify", MB_OK | MB_ICONWARNING);
    if (loaded.saveFailed) ShowSaveError(nullptr, loaded.saveError, SettingsPath());

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!SettingsWindowPreTranslate(&msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (mutex) CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
