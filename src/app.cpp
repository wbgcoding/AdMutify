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
#include "media_session.h"
#include "settings_window.h"
#include "spotify_watch.h"
#include "stats.h"
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
constexpr UINT_PTR kTimerRestoreRetry = 5;
// --trace only: records Spotify's window set, so popups and overlays (surveys, banners) can be told apart later.
constexpr UINT_PTR kTimerTraceWindows = 6;
constexpr UINT kTraceWindowsMs = 1000;
constexpr uint64_t kTraceCallbacksMs = 60000;
// One-shot: re-checks once the media session had its settle time after a window title change.
constexpr UINT_PTR kTimerMediaSettle = 7;
// While in Ad: checks that Spotify's window and sound are still there (see AdSessionGuard).
constexpr UINT_PTR kTimerAdCheck = 8;
constexpr UINT kAdCheckMs = 2000;
constexpr UINT kRestoreRetryMs = 2000;
constexpr int kMaxRestoreRetries = 15;
constexpr UINT kMediaPollMs = 500;
constexpr uint64_t kApplyRetryMs = 1000;
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
    AdSessionGuard adGuard;
    bool applied = false;
    Mode appliedMode = Mode::Mute;
    int appliedPercent = 0;
    Reason appliedReason = Reason::None;
    MediaDetector media;
    MediaSession mediaSession;
    std::wstring lastSeenTitle;
    uint64_t titleChangedAt = 0;
    uint64_t lastCallbacksLog = 0;
    MediaTrack lastTraceTrack;
    AdMusicPlayer music;
    bool mediaTimerRunning = false;
    std::wstring lastMediaExe;
    // "Pause Spotify": we paused it for the running video / this video was already dealt with.
    bool pausedByUs = false;
    bool pauseHandled = false;
    uint64_t pausedAt = 0;
    bool reevaluating = false;
    bool reevaluateAgain = false;
    bool probeRunning = false;
    bool adGone = false;
    int probes = 0;
    // Records of sessions whose restore failed are retried on a slow timer until they resolve or give up.
    bool restoreRetryRunning = false;
    int restoreRetries = 0;
    // Last retry of an apply that touched nothing; the retry runs at most once per kApplyRetryMs.
    uint64_t applyRetryAt = 0;
    UINT taskbarCreated = 0;
    std::wstring status;
    std::wstring lastTraceTitle;
    std::wstring lastTraceWindows;
    bool closing = false;
    // Windows is signing out: nothing may start playing any more.
    bool closingEndSession = false;
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
        g.status = g.appliedMode == Mode::Lower ? FormatPercent(StrId::StatusLowered, g.appliedPercent) : Str(StrId::StatusMuted);
        icon = TrayIconKind::Muted;
    } else if (g.pausedByUs) {
        g.status = Str(StrId::StatusMediaPaused);
        icon = TrayIconKind::Muted;
    } else {
        g.status = Str(StrId::StatusActive);
    }
    g.tray.Update(icon, TooltipText(g.status.c_str()));
    SettingsWindowUpdateStatus();
}

void DoApply(Mode mode, int percent, Reason reason) {
    // Init is idempotent: it only does work when an earlier start failed to set up Core Audio.
    g.audio.Init(g.hwnd);
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
    if (g.audio.PendingCount() > 0 && !g.closing && !g.restoreRetryRunning) {
        g.restoreRetryRunning = true;
        g.restoreRetries = 0;
        SetTimer(g.hwnd, kTimerRestoreRetry, kRestoreRetryMs, nullptr);
    }
}

void StopRestoreRetry() {
    KillTimer(g.hwnd, kTimerRestoreRetry);
    g.restoreRetryRunning = false;
}

void OnRestoreRetry() {
    if (g.closing) {
        StopRestoreRetry();
        return;
    }
    const int before = g.audio.PendingCount();
    g.audio.WatchSessions();
    const int after = g.audio.PendingCount();
    ++g.restoreRetries;
    if (after == 0) {
        Log("restore_retry n=%d", g.restoreRetries);
        StopRestoreRetry();
    } else if (g.restoreRetries >= kMaxRestoreRetries) {
        Log("restore_retry n=%d gave_up pending=%d", g.restoreRetries, after);
        StopRestoreRetry();
    } else if (after < before) {
        Log("restore_retry n=%d pending=%d", g.restoreRetries, after);
    }
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
bool SendPlayPause(HWND target = nullptr) {
    if (!target) target = g.watch.MainWindow();
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
            const HWND spotify = g.watch.MainWindow();
            const bool sent = SendPlayPause(spotify);
            // The send pumps messages: a nested Shutdown must not be followed by state changes. It ran before the
            // pause was ours, so it did not give it back; unless Windows is signing out, that happens here.
            if (g.closing) {
                if (sent && !g.closingEndSession) Log(SendPlayPause(spotify) ? "media resume" : "media resume_failed");
                return;
            }
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
    const std::wstring& windowTitle = g.watch.Title();
    if (windowTitle != g.lastSeenTitle) {
        g.lastSeenTitle = windowTitle;
        g.titleChangedAt = now;
    }
    const TitleKind titleKind = ClassifyTitle(windowTitle);
    // The media session is a second signal: it unmasks ads titled like a song and holds a running ad mute until
    // it has caught up with a new window title. Without a session this changes nothing.
    const MediaTrack track = g.mediaSession.Snapshot();
    TitleKind kind = MergeMediaTrack(titleKind, g.machine.Current(), track, windowTitle, now - g.titleChangedAt);
    // The session is only asked while an ad title is up; a silent player behind it means the ad has ended.
    const bool inAd = g.machine.Current() == AdState::Ad;
    const bool adTitle = kind == TitleKind::Ad;
    const bool adSessionActive = adTitle && (inAd || g.adGone) ? g.audio.IsSpotifyAudioActive() : true;
    const bool adGone = g.adGuard.Update(inAd, adTitle, adSessionActive, now);
    if (adGone != g.adGone) {
        g.adGone = adGone;
        Log(adGone ? "ad_silent" : "ad_silent_end");
    }
    if (adGone) kind = TitleKind::IdleOrAd;
    // Either verdict may wait for the session to catch up with a new title; a one-shot timer ends the wait.
    const bool heldForSession = titleKind != kind && (titleKind == TitleKind::Music || titleKind == TitleKind::Ad) &&
                                !TrackMatchesTitle(track, windowTitle);
    if (heldForSession) {
        const uint64_t since = now - g.titleChangedAt;
        const uint64_t left = since < kMediaSettleMs ? kMediaSettleMs - since : 0;
        SetTimer(g.hwnd, kTimerMediaSettle, static_cast<UINT>(left + 20), nullptr);
    } else {
        KillTimer(g.hwnd, kTimerMediaSettle);
    }
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
    if (after == AdState::Ad) SetTimer(g.hwnd, kTimerAdCheck, kAdCheckMs, nullptr);
    else KillTimer(g.hwnd, kTimerAdCheck);
    HandlePause(after, now);
    // The play/pause send pumps messages: Windows may have ended the session meanwhile (Shutdown ran nested).
    if (g.closing) return;
    // An ad wins over video playback.
    Reason want = Reason::None;
    Mode wantMode = AudioModeFor(g.settings.mode);
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
    const bool adWasApplied = g.applied && g.appliedReason == Reason::Ad;
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
    } else if (!g.audio.HasAppliedSessions() && now - g.applyRetryAt >= kApplyRetryMs) {
        // The first apply touched nothing (write failed, session not up yet): try again, logged only on success.
        // A new session also triggers an apply, so the retry only needs a slow pace.
        g.applyRetryAt = now;
        int n = g.audio.Apply(wantMode, wantPercent);
        if (n > 0) Log("apply_retry sessions=%d", n);
    }
    if (g.applied && g.appliedReason != want && want != Reason::None) {
        Log("reason %s -> %s", ReasonName(g.appliedReason), ReasonName(want));
        g.appliedReason = want;
    }
    // One count per ad, at the moment its reaction first applies (also when it takes over from a video reaction).
    if (g.applied && CountsAsNewAdMute(adWasApplied, want == Reason::Ad)) {
        StatsCountAdMute();
        SettingsWindowUpdateCounter();
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

    // Own music plays while an ad is muted in the music mode; video reactions stay quiet.
    g.music.SetActive(MusicShouldPlay(g.settings.mode, g.applied && g.appliedReason == Reason::Ad));

    // At most once a minute: a long trace run must not rotate the interesting lines out of the log.
    if (TraceEnabled() && now - g.lastCallbacksLog >= kTraceCallbacksMs) {
        g.lastCallbacksLog = now;
        Log("trace callbacks=%lu", g.watch.CallbackCount());
    }
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
    g.closingEndSession = endSession;
    KillTimer(g.hwnd, kTimerExit);
    KillTimer(g.hwnd, kTimerProbe);
    KillTimer(g.hwnd, kTimerMedia);
    KillTimer(g.hwnd, kTimerMediaSettle);
    KillTimer(g.hwnd, kTimerAdCheck);
    g.mediaTimerRunning = false;
    StopRestoreRetry();
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
    // Only after the restore: stopping the session may wait, and at logoff Windows can end the process meanwhile.
    g.mediaSession.Stop();
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
        case IDM_TRAY_HIDE: {
            // Hidden for this run and at the next start with Windows; opening AdMutify again brings it back.
            g.tray.Hide();
            Log("hide_icon reason=menu");
            Settings s = g.settings;
            s.hideTray = true;
            CommitSettings(s);
            break;
        }
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
            } else {
                if (!g.tray.Visible()) {
                    g.tray.Show();
                    Log("show_icon reason=second_instance");
                }
                // Opening AdMutify again is also the way back to the settings while the icon is hidden.
                OpenSettingsWindow();
            }
            return 0;
        case WM_APP_AUDIO_STATE: Reevaluate(); return 0;
        case WM_APP_MEDIA_SESSION:
            if (g.closing) return 0;
            if (TraceEnabled()) {
                const MediaTrack t = g.mediaSession.Snapshot();
                if (!(t == g.lastTraceTrack)) {
                    g.lastTraceTrack = t;
                    // Titles stay out of the normal log; only an explicit --trace run records them.
                    Log("trace media title=\"%s\" artist=\"%s\" album=\"%s\" track=%u next=%d prev=%d",
                        WideToUtf8(t.title).c_str(), WideToUtf8(t.artist).c_str(), WideToUtf8(t.album).c_str(),
                        t.trackNumber, t.nextEnabled ? 1 : 0, t.prevEnabled ? 1 : 0);
                }
            }
            Reevaluate();
            return 0;
        case WM_APP_MUSIC: g.music.OnEvent(wParam, lParam); return 0;
        case WM_APP_SESSION_CREATED:
            if (g.closing) return 0;
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
            } else if (wParam == kTimerAdCheck) {
                g.watch.RescanIfStale();
                Reevaluate();
            } else if (wParam == kTimerMediaSettle) {
                KillTimer(hwnd, kTimerMediaSettle);
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
            } else if (wParam == kTimerRestoreRetry) {
                OnRestoreRetry();
            } else if (wParam == kTimerTraceWindows) {
                std::wstring windows = SpotifyWatch::DescribeTargetWindows();
                if (windows != g.lastTraceWindows) {
                    g.lastTraceWindows = std::move(windows);
                    Log("trace windows %s", WideToUtf8(g.lastTraceWindows).c_str());
                }
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

const std::wstring& CurrentStatus() { return g.status; }

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
            if (SettingsNewerVersion()) {
                // The file turned out to be from a newer version: it is left alone, so tell the user once.
                Log("settings_read_newer");
                MessageBoxW(owner, Str(StrId::ErrSettingsNewer), L"AdMutify", MB_OK | MB_ICONWARNING);
            } else {
                Log("settings_read_recovered");
            }
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
    if (old.hideTray && !s.hideTray && g.trayReady && !g.tray.Visible()) {
        g.tray.Show();
        Log("show_icon reason=settings");
    }
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
    const unsigned long mutexErr = mutex ? 0 : GetLastError();
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // The running instance may still be creating its window.
        HWND other = FindWindowW(ClassName(), nullptr);
        for (int i = 0; !other && i < 20; ++i) {
            Sleep(100);
            other = FindWindowW(ClassName(), nullptr);
        }
        if (other) PostMessageW(other, WM_APP_SHOW, 0, 0);
        CloseHandle(mutex);
        return 0;
    }

    // Lets an all-users setup see a copy running in another user's session (the single-instance mutex is per
    // session). No ownership or single-instance meaning; a failure is ignored. A portable copy does not use the
    // installed files, so it must not block the setup.
    HANDLE runningMutex = IsPortable() ? nullptr : CreateMutexW(nullptr, FALSE, L"Global\\AdMutify.Running");

    // Only the instance that stays gets a crash handler and a log folder.
    InstallCrashHandler();
    ReserveStackGuarantee();
    if (!mutex) Log("single_instance_unavailable err=%lu", mutexErr);

    LoadResult loaded = LoadOrCreateSettings();
    g.settings = loaded.settings;
    StatsLoad();
    SetLanguage(g.settings.language);
    if (g.settings.trace) SetTraceEnabled(true);

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
    if (TraceEnabled()) SetTimer(g.hwnd, kTimerTraceWindows, kTraceWindowsMs, nullptr);
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
    g.mediaSession.Start(g.hwnd, WM_APP_MEDIA_SESSION);
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
    if (runningMutex) CloseHandle(runningMutex);
    return static_cast<int>(msg.wParam);
}
