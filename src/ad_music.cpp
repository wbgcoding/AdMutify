#include "ad_music.h"

#include <audiosessiontypes.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <shlwapi.h>

#include <algorithm>

#include "app_messages.h"
#include "crash.h"
#include "util.h"

bool IsMusicFile(std::wstring_view name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring_view::npos || name.find_first_of(L"\\/", dot) != std::wstring_view::npos) return false;
    const std::wstring_view ext = name.substr(dot);
    for (const wchar_t* known : {L".mp3", L".m4a", L".aac", L".wma", L".wav", L".flac"})
        if (EqualsNoCase(ext, known)) return true;
    return false;
}

bool MusicNameLess(const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; }

std::vector<std::wstring> ScanMusicFolder(const std::wstring& folder, size_t maxFiles) {
    std::vector<std::wstring> files;
    if (folder.empty()) return files;
    // Depth-limited walk; linked folders are skipped so a link loop cannot run forever.
    struct Dir {
        std::wstring path;
        int depth;
    };
    std::vector<Dir> pending{{folder, 0}};
    while (!pending.empty() && files.size() < maxFiles) {
        Dir dir = std::move(pending.back());
        pending.pop_back();
        std::wstring base = dir.path;
        if (base.back() != L'\\' && base.back() != L'/') base += L'\\';
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((base + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring_view name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && dir.depth < 16)
                    pending.push_back({base + fd.cFileName, dir.depth + 1});
            } else if (IsMusicFile(name) && files.size() < maxFiles) {
                files.push_back(base + fd.cFileName);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(files.begin(), files.end(), MusicNameLess);
    return files;
}

// ---------------------------------------------------------------------------------------------------------------
// Playlist

void Playlist::Reset(std::vector<std::wstring> files, bool shuffle, uint32_t seed) {
    files_ = std::move(files);
    order_.resize(files_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    pos_ = 0;
    shuffle_ = shuffle;
    rng_ = seed ? seed : 1;
    if (shuffle_) Shuffle();
}

const std::wstring& Playlist::Current() const {
    static const std::wstring kNone;
    return order_.empty() ? kNone : files_[order_[pos_]];
}

bool Playlist::Next() {
    if (order_.empty()) return false;
    if (++pos_ < order_.size()) return false;
    pos_ = 0;
    if (shuffle_ && order_.size() > 1) {
        const size_t last = order_.back();
        Shuffle();
        if (order_[0] == last) std::swap(order_[0], order_[1 + rng_ % (order_.size() - 1)]);
    }
    return true;
}

void Playlist::RemoveCurrent() {
    if (order_.empty()) return;
    order_.erase(order_.begin() + static_cast<ptrdiff_t>(pos_));
    if (pos_ >= order_.size()) pos_ = 0;
}

void Playlist::Shuffle() {
    for (size_t i = order_.size(); i > 1; --i) {
        // xorshift32: small and good enough to mix a track list.
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        std::swap(order_[i - 1], order_[rng_ % i]);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Player

namespace {

constexpr ULONGLONG kFadeInMs = 500;
constexpr ULONGLONG kFadeOutMs = 800;
constexpr UINT kFadeTickMs = 30;
// Unplayable files in a row before the player gives up for this ad.
constexpr int kMaxErrors = 10;

// Runs on a Media Foundation worker thread: it only forwards the events the player needs to the message window.
class EngineNotify final : public IMFMediaEngineNotify {
public:
    EngineNotify(HWND hwnd, UINT generation) : hwnd_(hwnd), generation_(generation) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }

    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD) override {
        switch (event) {
            case MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE:
                SetEvent(reinterpret_cast<HANDLE>(param1));
                break;
            case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
            case MF_MEDIA_ENGINE_EVENT_ENDED:
            case MF_MEDIA_ENGINE_EVENT_ERROR:
                PostMessageW(hwnd_, WM_APP_MUSIC, MAKEWPARAM(event, static_cast<WORD>(param1)), static_cast<LPARAM>(generation_));
                break;
            default: break;
        }
        return S_OK;
    }

private:
    LONG ref_ = 1;
    HWND hwnd_;
    UINT generation_;
};

}  // namespace

struct AdMusicPlayer::Impl {
    HWND hwnd = nullptr;
    UINT_PTR timerId = 0;
    AdMusic mode = AdMusic::Off;
    std::wstring path;
    int volume = 50;

    Playlist list;
    bool listLoaded = false;
    // Seconds into the current track where the next ad continues.
    double resumeAt = 0;

    bool mfStarted = false;
    IMFMediaEngine* engine = nullptr;
    // Events of an engine that was let go carry an older number and are dropped.
    UINT generation = 0;
    bool loaded = false;
    bool active = false;
    double fade = 0;
    ULONGLONG lastTick = 0;
    bool timerRunning = false;
    int errors = 0;

    void StartTimer() {
        lastTick = GetTickCount64();
        if (!timerRunning && hwnd) timerRunning = SetTimer(hwnd, timerId, kFadeTickMs, nullptr) != 0;
    }

    void StopTimer() {
        if (timerRunning) KillTimer(hwnd, timerId);
        timerRunning = false;
    }

    void ApplyVolume() {
        if (engine) engine->SetVolume(volume / 100.0 * fade);
    }

    bool EnsureList() {
        if (listLoaded) return !list.Empty();
        std::vector<std::wstring> files;
        if (mode == AdMusic::File) {
            const DWORD attr = GetFileAttributesW(path.c_str());
            if (!path.empty() && attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(path);
        } else {
            files = ScanMusicFolder(path);
        }
        if (files.empty()) {
            // Not remembered: the next ad looks again (the drive may be back by then).
            Log("music none");
            return false;
        }
        list.Reset(std::move(files), mode == AdMusic::Shuffle, static_cast<uint32_t>(GetTickCount64()) ^ GetCurrentProcessId());
        listLoaded = true;
        resumeAt = 0;
        return true;
    }

    bool CreateEngine() {
        if (!mfStarted) {
            const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
            if (FAILED(hr)) {
                Log("music engine_failed hr=0x%08lx", static_cast<unsigned long>(hr));
                return false;
            }
            mfStarted = true;
        }
        IMFMediaEngineClassFactory* factory = nullptr;
        IMFAttributes* attrs = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attrs, 2);
        EngineNotify* notify = nullptr;
        if (SUCCEEDED(hr)) {
            notify = new EngineNotify(hwnd, ++generation);
            hr = attrs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
        }
        if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_MEDIA_ENGINE_AUDIO_CATEGORY, AudioCategory_Media);
        if (SUCCEEDED(hr)) hr = factory->CreateInstance(MF_MEDIA_ENGINE_AUDIOONLY, attrs, &engine);
        if (notify) notify->Release();
        if (attrs) attrs->Release();
        if (factory) factory->Release();
        if (FAILED(hr)) {
            engine = nullptr;
            Log("music engine_failed hr=0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }
        return true;
    }

    void Load() {
        loaded = false;
        const std::wstring& file = list.Current();
        if (TraceEnabled()) Log("trace music \"%s\"", WideToUtf8(file).c_str());
        BSTR url = SysAllocStringLen(file.data(), static_cast<UINT>(file.size()));
        HRESULT hr = url ? engine->SetSource(url) : E_OUTOFMEMORY;
        SysFreeString(url);
        // A single file repeats; in a folder the end moves on to the next track.
        if (SUCCEEDED(hr)) hr = engine->SetLoop(mode == AdMusic::File ? TRUE : FALSE);
        if (SUCCEEDED(hr)) hr = engine->Load();
        if (FAILED(hr)) {
            Log("music load_failed hr=0x%08lx", static_cast<unsigned long>(hr));
            Fail();
        }
    }

    // Lets the engine go; with keepPosition the current spot is where the next ad continues.
    void ReleaseEngine(bool keepPosition) {
        StopTimer();
        if (engine) {
            if (keepPosition && loaded) resumeAt = engine->GetCurrentTime();
            engine->Pause();
            engine->Shutdown();
            engine->Release();
            engine = nullptr;
            ++generation;
        }
        loaded = false;
        fade = 0;
    }

    // The current track cannot be played: drop it and try the next one, up to a limit.
    void Fail() {
        resumeAt = 0;
        list.RemoveCurrent();
        if (list.Empty()) listLoaded = false;
        if (list.Empty() || ++errors >= kMaxErrors) {
            ReleaseEngine(false);
            return;
        }
        Load();
    }

    void Begin() {
        if (mode == AdMusic::Off || !hwnd) return;
        if (engine) {
            // Fading out when the next ad came: fade back in from where the level is.
            if (loaded) {
                engine->Play();
                StartTimer();
            }
            return;
        }
        if (!EnsureList()) return;
        errors = 0;
        if (!CreateEngine()) return;
        fade = 0;
        Load();
    }

    void End() {
        if (!engine) return;
        if (!loaded) {
            // Still opening the file: nothing is audible yet.
            ReleaseEngine(true);
            Log("music stop");
            return;
        }
        StartTimer();
    }
};

AdMusicPlayer::AdMusicPlayer() : impl_(std::make_unique<Impl>()) {}

AdMusicPlayer::~AdMusicPlayer() { Shutdown(); }

void AdMusicPlayer::Init(HWND notifyWindow, UINT_PTR timerId) {
    impl_->hwnd = notifyWindow;
    impl_->timerId = timerId;
}

void AdMusicPlayer::Configure(AdMusic mode, const std::wstring& path, int volumePercent) {
    Impl& m = *impl_;
    m.volume = std::clamp(volumePercent, 0, 100);
    if (mode != m.mode || path != m.path) {
        const bool wasPlaying = m.engine != nullptr;
        m.ReleaseEngine(false);
        m.mode = mode;
        m.path = path;
        m.listLoaded = false;
        m.list = Playlist();
        m.resumeAt = 0;
        if (wasPlaying) Log("music stop");
        if (m.active) m.Begin();
        return;
    }
    m.ApplyVolume();
}

void AdMusicPlayer::SetActive(bool active) {
    Impl& m = *impl_;
    if (active == m.active) return;
    m.active = active;
    if (active) m.Begin();
    else m.End();
}

void AdMusicPlayer::OnEvent(WPARAM wParam, LPARAM lParam) {
    Impl& m = *impl_;
    if (!m.engine || static_cast<UINT>(lParam) != m.generation) return;
    switch (LOWORD(wParam)) {
        case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA: {
            if (m.resumeAt > 0) {
                const double duration = m.engine->GetDuration();
                // Close to the end there is nothing left worth hearing: the track starts over.
                if (duration > 0 && m.resumeAt < duration - 1.0) m.engine->SetCurrentTime(m.resumeAt);
                m.resumeAt = 0;
            }
            m.loaded = true;
            m.errors = 0;
            if (!m.active) {
                // The ad ended while the file was opening.
                m.ReleaseEngine(true);
                break;
            }
            m.ApplyVolume();
            m.engine->Play();
            m.StartTimer();
            Log("music play");
            break;
        }
        case MF_MEDIA_ENGINE_EVENT_ENDED:
        {
            m.resumeAt = 0;
            const std::wstring last = m.list.Current();
            if (m.list.Next() && m.mode != AdMusic::File) {
                // A round is over: files added to the folder meanwhile join the next one.
                std::vector<std::wstring> files = ScanMusicFolder(m.path);
                if (!files.empty()) {
                    m.list.Reset(std::move(files), m.mode == AdMusic::Shuffle, static_cast<uint32_t>(GetTickCount64()));
                    if (m.list.Size() > 1 && m.list.Current() == last) m.list.Next();
                }
            }
            Log("music next");
            m.Load();
            break;
        }
        case MF_MEDIA_ENGINE_EVENT_ERROR: {
            HRESULT detail = S_OK;
            IMFMediaError* error = nullptr;
            if (SUCCEEDED(m.engine->GetError(&error)) && error) {
                detail = error->GetExtendedErrorCode();
                error->Release();
            }
            Log("music error code=%u hr=0x%08lx", static_cast<unsigned>(HIWORD(wParam)), static_cast<unsigned long>(detail));
            m.Fail();
            break;
        }
        default: break;
    }
}

void AdMusicPlayer::OnTimer() {
    Impl& m = *impl_;
    const ULONGLONG now = GetTickCount64();
    const double dt = static_cast<double>(now - m.lastTick);
    m.lastTick = now;
    if (!m.engine || !m.loaded) {
        m.StopTimer();
        return;
    }
    if (m.active) {
        m.fade = std::min(1.0, m.fade + dt / kFadeInMs);
        m.ApplyVolume();
        if (m.fade >= 1.0) m.StopTimer();
    } else {
        m.fade = std::max(0.0, m.fade - dt / kFadeOutMs);
        m.ApplyVolume();
        if (m.fade <= 0.0) {
            m.ReleaseEngine(true);
            Log("music stop");
        }
    }
}

void AdMusicPlayer::Shutdown() {
    Impl& m = *impl_;
    m.active = false;
    m.ReleaseEngine(false);
    if (m.mfStarted) {
        MFShutdown();
        m.mfStarted = false;
    }
}

bool AdMusicPlayer::IsPlaying() const { return impl_->engine && impl_->loaded; }

double AdMusicPlayer::Position() const {
    return impl_->engine && impl_->loaded ? impl_->engine->GetCurrentTime() : impl_->resumeAt;
}

size_t AdMusicPlayer::TrackCount() const { return impl_->list.Size(); }
