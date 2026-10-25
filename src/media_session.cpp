#include "media_session.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <cwctype>
#include <mutex>
#include <thread>
#include <vector>

#include "crash.h"
#include "util.h"

namespace {

std::wstring Trim(std::wstring_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::iswspace(s[b])) ++b;
    while (e > b && std::iswspace(s[e - 1])) --e;
    return std::wstring(s.substr(b, e - b));
}

bool ContainsNoCase(std::wstring_view hay, std::wstring_view needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i)
        if (EqualsNoCase(hay.substr(i, needle.size()), needle)) return true;
    return false;
}

}  // namespace

bool IsAdTrack(const MediaTrack& t) {
    return t.valid && t.trackNumber == 0 && t.album.empty() && !t.nextEnabled && !t.prevEnabled;
}

bool TrackMatchesTitle(const MediaTrack& t, std::wstring_view windowTitle) {
    // A track without a title cannot vouch for any window title.
    if (!t.valid || t.title.empty()) return false;
    const std::wstring title = Trim(windowTitle);
    if (title == t.title) return true;
    return !t.artist.empty() && title == t.artist + L" - " + t.title;
}

TitleKind MergeMediaTrack(TitleKind kind, AdState current, const MediaTrack& t, std::wstring_view windowTitle,
                          uint64_t msSinceTitleChange) {
    if (!t.valid) return kind;
    const bool matches = TrackMatchesTitle(t, windowTitle);
    if (matches && IsAdTrack(t)) return TitleKind::Ad;
    if (kind == TitleKind::Music && current == AdState::Ad && !matches && msSinceTitleChange < kMediaSettleMs)
        return TitleKind::Ad;
    return kind;
}

namespace wmc = winrt::Windows::Media::Control;

struct MediaSession::Impl {
    struct Watched {
        wmc::GlobalSystemMediaTransportControlsSession session{nullptr};
        winrt::event_token props{};
        winrt::event_token playback{};
    };

    HWND notify = nullptr;
    UINT message = 0;
    std::thread thread;
    HANDLE wake = nullptr;
    HANDLE stop = nullptr;
    mutable std::mutex mutex;
    MediaTrack snapshot;
    bool failureLogged = false;

    Impl() {
        wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    ~Impl() {
        if (wake) CloseHandle(wake);
        if (stop) CloseHandle(stop);
    }

    void LogFailure(int32_t hr) {
        if (failureLogged) return;
        failureLogged = true;
        Log("media_session_unavailable hr=0x%08lx", static_cast<unsigned long>(hr));
    }

    void Publish(const MediaTrack& t) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (snapshot == t) return;
            snapshot = t;
        }
        PostMessageW(notify, message, 0, 0);
    }

    static bool IsSpotify(const wmc::GlobalSystemMediaTransportControlsSession& s) {
        return ContainsNoCase(std::wstring_view(s.SourceAppUserModelId()), L"spotify");
    }

    static MediaTrack Read(const wmc::GlobalSystemMediaTransportControlsSession& s) {
        MediaTrack t;
        // Both calls may hand back null while a session is going away; calling into null would crash.
        auto props = s.TryGetMediaPropertiesAsync().get();
        auto info = s.GetPlaybackInfo();
        wmc::GlobalSystemMediaTransportControlsSessionPlaybackControls controls{nullptr};
        if (info) controls = info.Controls();
        if (!props || !controls) return t;
        t.title = props.Title().c_str();
        t.artist = props.Artist().c_str();
        t.album = props.AlbumTitle().c_str();
        t.trackNumber = static_cast<uint32_t>(props.TrackNumber());
        t.nextEnabled = controls.IsNextEnabled();
        t.prevEnabled = controls.IsPreviousEnabled();
        t.valid = true;
        return t;
    }

    static void Unwatch(Watched& w) {
        try {
            if (w.session) {
                w.session.MediaPropertiesChanged(w.props);
                w.session.PlaybackInfoChanged(w.playback);
            }
        } catch (...) {
        }
        w.session = nullptr;
    }

    void Refresh(const wmc::GlobalSystemMediaTransportControlsSessionManager& manager, std::vector<Watched>& watched) {
        const HANDLE wakeLocal = wake;
        std::vector<wmc::GlobalSystemMediaTransportControlsSession> spotify;
        for (auto const& s : manager.GetSessions())
            if (IsSpotify(s)) spotify.push_back(s);

        // Drop watches of sessions that are gone, add the new ones.
        for (size_t i = watched.size(); i-- > 0;) {
            bool still = false;
            for (auto const& s : spotify)
                if (s == watched[i].session) still = true;
            if (!still) {
                Unwatch(watched[i]);
                watched.erase(watched.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        for (auto const& s : spotify) {
            bool known = false;
            for (auto const& w : watched)
                if (w.session == s) known = true;
            if (known) continue;
            Watched w;
            w.session = s;
            w.props = s.MediaPropertiesChanged([wakeLocal](auto&&, auto&&) { SetEvent(wakeLocal); });
            w.playback = s.PlaybackInfoChanged([wakeLocal](auto&&, auto&&) { SetEvent(wakeLocal); });
            watched.push_back(std::move(w));
        }

        if (spotify.empty()) {
            Publish(MediaTrack{});
            return;
        }
        // Prefer the system's current session when it is Spotify's.
        wmc::GlobalSystemMediaTransportControlsSession pick = spotify.front();
        if (auto cur = manager.GetCurrentSession(); cur && IsSpotify(cur)) pick = cur;
        Publish(Read(pick));
    }

    void Run() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error& e) {
            LogFailure(e.code());
            return;
        } catch (...) {
            LogFailure(static_cast<int32_t>(E_FAIL));
            return;
        }
        std::vector<Watched> watched;
        wmc::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
        winrt::event_token sessionsToken{};
        const HANDLE wakeLocal = wake;
        try {
            manager = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            sessionsToken = manager.SessionsChanged([wakeLocal](auto&&, auto&&) { SetEvent(wakeLocal); });
        } catch (const winrt::hresult_error& e) {
            LogFailure(e.code());
            manager = nullptr;
        } catch (const std::exception&) {
            LogFailure(static_cast<int32_t>(E_FAIL));
            manager = nullptr;
        }

        const HANDLE handles[2] = {stop, wake};
        bool first = true;
        while (manager) {
            if (!first) {
                DWORD w = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                if (w != WAIT_OBJECT_0 + 1) break;
            }
            first = false;
            try {
                Refresh(manager, watched);
            } catch (const winrt::hresult_error& e) {
                LogFailure(e.code());
                Publish(MediaTrack{});
            } catch (const std::exception&) {
                LogFailure(static_cast<int32_t>(E_FAIL));
                Publish(MediaTrack{});
            }
        }

        for (Watched& w : watched) Unwatch(w);
        watched.clear();
        if (manager) {
            try {
                manager.SessionsChanged(sessionsToken);
            } catch (...) {
            }
        }
        manager = nullptr;
        winrt::uninit_apartment();
    }
};

MediaSession::MediaSession() : impl_(std::make_unique<Impl>()) {}

MediaSession::~MediaSession() { Stop(); }

void MediaSession::Start(HWND notify, UINT message) {
    if (impl_->thread.joinable() || !impl_->wake || !impl_->stop) return;
    impl_->notify = notify;
    impl_->message = message;
    ResetEvent(impl_->stop);
    Impl* impl = impl_.get();
    impl->thread = std::thread([impl] {
        try {
            impl->Run();
        } catch (...) {
        }
    });
}

void MediaSession::Stop() {
    if (!impl_->thread.joinable()) return;
    SetEvent(impl_->stop);
    // A media session call can hang inside the system; shutdown must still go on to restore the volume. A thread
    // that does not end in time is left behind together with its state, which lives until the process ends.
    if (WaitForSingleObject(impl_->thread.native_handle(), kStopWaitMs) != WAIT_OBJECT_0) {
        Log("media_session_stop_timeout");
        impl_->thread.detach();
        static_cast<void>(impl_.release());
        impl_ = std::make_unique<Impl>();
        return;
    }
    impl_->thread.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot = MediaTrack{};
}

MediaTrack MediaSession::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}
