#include "audio_control.h"

#include <audiopolicy.h>
#include <cmath>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>

#include "app_messages.h"
#include "crash.h"
#include "media_detector.h"
#include "spotify_watch.h"
#include "util.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kMaxCrashSlots = 16;
// Content detection: a session is audible above this peak and master volume.
constexpr float kContentPeak = 0.02f;
constexpr float kContentVolume = 0.01f;
// Capture endpoints that cannot be opened (microphone privacy off) are not retried every poll.
constexpr ULONGLONG kCaptureRetryMs = 10000;

std::string EscapeField(std::wstring_view w) {
    std::string utf8 = WideToUtf8(w);
    std::string out;
    for (unsigned char c : utf8) {
        if (c == '%' || c == '|' || c == '\r' || c == '\n') {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::wstring UnescapeField(std::string_view s) {
    std::string utf8;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            unsigned v = 0;
            auto r = std::from_chars(s.data() + i + 1, s.data() + i + 3, v, 16);
            if (r.ec == std::errc()) {
                utf8 += static_cast<char>(v);
                i += 2;
                continue;
            }
        }
        utf8 += s[i];
    }
    return Utf8ToWide(utf8);
}

class SessionEvents final : public IAudioSessionEvents {
public:
    explicit SessionEvents(HWND hwnd) : hwnd_(hwnd) {}

    std::atomic<bool> userChanged{false};
    // What AdMutify itself set while an ad is active; a foreign change only counts when it
    // touches the property the current mode controls (mute in mute mode, volume in lower mode).
    std::atomic<int> appliedMode{-1};
    std::atomic<bool> expectedMute{false};
    std::atomic<float> expectedVolume{1.0f};

    // keepUserFlag: a user change already seen stays on record (a level change on a live session).
    void Expect(Mode mode, bool mute, float volume, bool keepUserFlag = false) {
        expectedMute = mute;
        expectedVolume = volume;
        if (!keepUserFlag) userChanged = false;
        appliedMode = static_cast<int>(mode);
    }
    void Clear() {
        appliedMode = -1;
        userChanged = false;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionEvents)) {
            *ppv = static_cast<IAudioSessionEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP OnDisplayNameChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnIconPathChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnSimpleVolumeChanged(float volume, BOOL mute, LPCGUID context) override {
        if (context && IsEqualGUID(*context, kAdMutifyEventContext)) return S_OK;
        int mode = appliedMode;
        if (mode < 0) return S_OK;
        if (UserOverrode(static_cast<Mode>(mode), expectedMute, expectedVolume, mute != FALSE, volume))
            userChanged = true;
        return S_OK;
    }
    STDMETHODIMP OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnStateChanged(AudioSessionState) override {
        PostMessageW(hwnd_, WM_APP_AUDIO_STATE, 0, 0);
        return S_OK;
    }
    STDMETHODIMP OnSessionDisconnected(AudioSessionDisconnectReason) override {
        PostMessageW(hwnd_, WM_APP_AUDIO_STATE, 0, 0);
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_{1};
    HWND hwnd_;
};

class SessionNotify final : public IAudioSessionNotification {
public:
    explicit SessionNotify(HWND hwnd) : hwnd_(hwnd) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionNotification)) {
            *ppv = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP OnSessionCreated(IAudioSessionControl*) override {
        PostMessageW(hwnd_, WM_APP_SESSION_CREATED, 0, 0);
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_{1};
    HWND hwnd_;
};

class DeviceNotify final : public IMMNotificationClient {
public:
    DeviceNotify(HWND hwnd, std::atomic<bool>* devicesDirty) : hwnd_(hwnd), dirty_(devicesDirty) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return Poke(); }
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return Poke(); }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override {
        *dirty_ = true;
        return S_OK;
    }
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return Poke(); }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    HRESULT Poke() {
        *dirty_ = true;
        PostMessageW(hwnd_, WM_APP_SESSION_CREATED, 0, 0);
        return S_OK;
    }
    std::atomic<ULONG> ref_{1};
    HWND hwnd_;
    std::atomic<bool>* dirty_;
};

struct DeviceRef {
    std::wstring id;
    ComPtr<IAudioSessionManager2> mgr;
};

struct SessionRef {
    DWORD pid = 0;
    std::wstring instanceId;
    std::wstring sessionId;
    ComPtr<IAudioSessionControl> ctrl;
    ComPtr<ISimpleAudioVolume> vol;
};

struct Entry {
    std::wstring deviceId;
    std::wstring instanceId;
    std::wstring sessionId;
    ComPtr<IAudioSessionControl> ctrl;
    ComPtr<ISimpleAudioVolume> vol;
    ComPtr<IAudioSessionEvents> evIface;
    SessionEvents* ev = nullptr;
    // The session callbacks are registered; only then there is something to unregister.
    bool registered = false;
    bool applied = false;
    Mode appliedMode = Mode::Mute;
    BOOL oldMute = FALSE;
    float oldVolume = 1.0f;
    float appliedVolume = 1.0f;
    int slot = -1;
    uint64_t appliedAt = 0;
    // The user changed this session during the current reaction: Apply skips it until the next full Restore.
    bool overridden = false;
};

enum class Cur { Same, Changed, Unreadable };

// Compares the property the mode controls with what AdMutify applied.
Cur CompareApplied(ISimpleAudioVolume* v, Mode mode, float appliedVolume) {
    if (mode == Mode::Mute) {
        BOOL m = FALSE;
        if (FAILED(v->GetMute(&m))) return Cur::Unreadable;
        return m == TRUE ? Cur::Same : Cur::Changed;
    }
    float vol = 0.0f;
    if (FAILED(v->GetMasterVolume(&vol))) return Cur::Unreadable;
    return std::fabs(vol - appliedVolume) <= 0.001f ? Cur::Same : Cur::Changed;
}

// Puts back only the property the mode controls.
bool PutOld(ISimpleAudioVolume* v, Mode mode, bool oldMute, float oldVolume) {
    if (mode == Mode::Mute) return SUCCEEDED(v->SetMute(oldMute ? TRUE : FALSE, &kAdMutifyEventContext));
    return SUCCEEDED(v->SetMasterVolume(oldVolume, &kAdMutifyEventContext));
}

constexpr size_t kMaxPending = 16;

uint64_t NowUnix() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // 100 ns ticks since 1601 to seconds since 1970.
    return u.QuadPart / 10000000ULL - 11644473600ULL;
}

// The crash thread reads a slot without locks while the main thread fills it: every field is written first and
// vol is published last (release), the reader checks vol first (acquire).
struct CrashSlot {
    ISimpleAudioVolume* vol = nullptr;
    BOOL mute = FALSE;
    float volume = 1.0f;
    // Lower mode: the level AdMutify set; the slot restores only while the session still reads that.
    float appliedVolume = 1.0f;
    // The level before the last ChangeLevel: it is what the session still reads while a level change is in flight.
    float previousVolume = 1.0f;
    Mode mode = Mode::Mute;
    SessionEvents* ev = nullptr;
};

AudioControl* g_active = nullptr;

}  // namespace

struct AudioControl::Impl {
    HWND hwnd = nullptr;
    bool comInited = false;
    bool mtaInited = false;
    CO_MTA_USAGE_COOKIE mtaCookie = nullptr;
    ComPtr<IMMDeviceEnumerator> enumerator;
    std::vector<std::unique_ptr<Entry>> entries;
    ComPtr<IAudioSessionNotification> notify;
    ComPtr<IMMNotificationClient> deviceNotify;
    std::vector<DeviceRef> notifiedDevices;
    std::vector<SavedSession> pending;
    CrashSlot slots[kMaxCrashSlots];
    bool crashSlotsFull = false;
    bool registerFailedLogged = false;
    bool stateWriteFailing = false;
    wchar_t statePath[MAX_PATH * 2] = {};
    // Activating a session manager per endpoint is slow, so the list is kept until a device change
    // (set from the notification thread) or a failed read marks it stale.
    std::atomic<bool> devicesDirty{true};
    std::vector<DeviceRef> devices;
    // Capture endpoints follow the render list: any rebuild of it marks them stale too.
    bool captureDirty = true;
    ULONGLONG captureRetryAt = 0;
    std::vector<DeviceRef> captureDevices;

    // Exe names by PID for the 500 ms content poll, so it does not OpenProcess every session each time. A PID that
    // no poll asked for any more is dropped (PIDs get reused). Names that could not be read are not kept.
    std::vector<std::pair<DWORD, std::wstring>> exeCache;
    std::vector<DWORD> exeSeen;

    std::wstring CachedExeName(DWORD pid) {
        exeSeen.push_back(pid);
        for (auto& c : exeCache)
            if (c.first == pid) return c.second;
        std::wstring name = ExeNameOfPid(pid);
        if (!name.empty()) exeCache.emplace_back(pid, name);
        return name;
    }

    void EndExePoll() {
        for (auto it = exeCache.begin(); it != exeCache.end();) {
            bool seen = false;
            for (DWORD p : exeSeen)
                if (p == it->first) seen = true;
            it = seen ? it + 1 : exeCache.erase(it);
        }
        exeSeen.clear();
    }

    const std::vector<DeviceRef>& Devices() {
        if (devicesDirty.exchange(false)) {
            bool complete = true;
            devices = EnumDevices(eRender, complete);
            if (!complete) devicesDirty = true;
            captureDirty = true;
        }
        return devices;
    }

    const std::vector<DeviceRef>& CaptureDevices() {
        if (captureDirty && GetTickCount64() >= captureRetryAt) {
            bool complete = true;
            captureDevices = EnumDevices(eCapture, complete);
            captureDirty = !complete;
            if (!complete) captureRetryAt = GetTickCount64() + kCaptureRetryMs;
        }
        return captureDevices;
    }

    std::vector<DeviceRef> EnumDevices(EDataFlow flow, bool& complete) {
        std::vector<DeviceRef> out;
        if (!enumerator) return out;
        ComPtr<IMMDeviceCollection> coll;
        if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll))) {
            complete = false;
            return out;
        }
        UINT n = 0;
        coll->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            ComPtr<IMMDevice> dev;
            LPWSTR id = nullptr;
            if (FAILED(coll->Item(i, &dev)) || FAILED(dev->GetId(&id)) || !id) {
                complete = false;
                continue;
            }
            DeviceRef d;
            d.id = id;
            CoTaskMemFree(id);
            if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(d.mgr.GetAddressOf())))) {
                complete = false;
                continue;
            }
            out.push_back(std::move(d));
        }
        return out;
    }

    // capture: the device comes from the capture list; a failure there must not rebuild the render list.
    // withIds false: the poll only needs pid, control and volume, so the id strings are not fetched.
    std::vector<SessionRef> SessionsOf(const DeviceRef& dev, bool spotifyOnly, bool capture = false, bool withIds = true) {
        std::vector<SessionRef> out;
        ComPtr<IAudioSessionEnumerator> se;
        if (FAILED(dev.mgr->GetSessionEnumerator(&se))) {
            if (capture) {
                captureDirty = true;
                captureRetryAt = GetTickCount64() + kCaptureRetryMs;
            } else {
                devicesDirty = true;
            }
            return out;
        }
        int count = 0;
        se->GetCount(&count);
        for (int j = 0; j < count; ++j) {
            ComPtr<IAudioSessionControl> c;
            if (FAILED(se->GetSession(j, &c))) continue;
            ComPtr<IAudioSessionControl2> c2;
            if (FAILED(c.As(&c2))) continue;
            DWORD pid = 0;
            if (FAILED(c2->GetProcessId(&pid))) continue;
            if (spotifyOnly && !IsTargetProcess(pid)) continue;
            SessionRef s;
            s.pid = pid;
            if (withIds) {
                LPWSTR inst = nullptr;
                if (FAILED(c2->GetSessionInstanceIdentifier(&inst)) || !inst) continue;
                s.instanceId = inst;
                CoTaskMemFree(inst);
                LPWSTR sid = nullptr;
                if (SUCCEEDED(c2->GetSessionIdentifier(&sid)) && sid) {
                    s.sessionId = sid;
                    CoTaskMemFree(sid);
                }
            }
            s.ctrl = c;
            if (FAILED(c.As(&s.vol))) continue;
            out.push_back(std::move(s));
        }
        return out;
    }

    Entry* GetOrAdd(const std::wstring& deviceId, SessionRef& s) {
        for (auto& e : entries)
            if (e->deviceId == deviceId && e->instanceId == s.instanceId) return e.get();
        auto e = std::make_unique<Entry>();
        e->deviceId = deviceId;
        e->instanceId = s.instanceId;
        e->sessionId = s.sessionId;
        e->ctrl = s.ctrl;
        e->vol = s.vol;
        e->ev = new SessionEvents(hwnd);
        e->evIface.Attach(e->ev);
        HRESULT hr = s.ctrl->RegisterAudioSessionNotification(e->ev);
        e->registered = SUCCEEDED(hr);
        if (FAILED(hr) && !registerFailedLogged) {
            registerFailedLogged = true;
            Log("session notification failed hr=0x%08lx", static_cast<unsigned long>(hr));
        }
        entries.push_back(std::move(e));
        return entries.back().get();
    }

    void ReleaseSlot(Entry& e) {
        if (e.slot < 0) return;
        CrashSlot& s = slots[e.slot];
        ISimpleAudioVolume* vol = s.vol;
        // Unpublish first so the crash thread never sees a half-reset slot.
        std::atomic_ref<ISimpleAudioVolume*>(s.vol).store(nullptr, std::memory_order_release);
        s.ev = nullptr;
        if (vol) vol->Release();
        e.slot = -1;
    }

    void ReleaseEntry(Entry& e) {
        ReleaseSlot(e);
        if (e.ctrl && e.registered) e.ctrl->UnregisterAudioSessionNotification(e.evIface.Get());
        e.registered = false;
    }

    static SavedSession Saved(const Entry& e) {
        SavedSession s;
        s.deviceId = e.deviceId;
        s.instanceId = e.instanceId;
        s.sessionId = e.sessionId;
        s.mode = e.appliedMode;
        s.oldMute = e.oldMute != FALSE;
        s.oldVolume = e.oldVolume;
        s.appliedVolume = e.appliedVolume;
        s.savedAt = e.appliedAt;
        return s;
    }

    void SetStatePath(const std::wstring& path) {
        if (path.size() < sizeof(statePath) / sizeof(statePath[0])) wcscpy_s(statePath, path.c_str());
        else statePath[0] = 0;
    }

    // Writes applied entries plus pending records; no records left means no file. A failed write is logged once per
    // streak; the caller goes on (the crash slots still cover a crash), only a kill would lose the record.
    bool WriteState() {
        std::vector<SavedSession> list;
        for (auto& e : entries)
            if (e->applied) list.push_back(Saved(*e));
        for (const SavedSession& p : pending) list.push_back(p);
        std::wstring path = StatePath();
        SetStatePath(path);
        bool ok = true;
        unsigned long err = 0;
        if (list.empty()) DeleteFileW(path.c_str());
        else ok = WriteFileAtomic(path, FormatStateFile(list), &err);
        if (!ok && !stateWriteFailing) Log("state_write_failed err=%lu", err);
        stateWriteFailing = !ok;
        return ok;
    }

    void AddPending(const SavedSession& rec) {
        for (auto it = pending.begin(); it != pending.end(); ++it) {
            if (it->deviceId == rec.deviceId && it->instanceId == rec.instanceId && it->sessionId == rec.sessionId) {
                pending.erase(it);
                break;
            }
        }
        pending.push_back(rec);
        while (pending.size() > kMaxPending) pending.erase(pending.begin());
    }

    bool IsAppliedEntry(const std::wstring& deviceId, const std::wstring& instanceId) const {
        for (auto& e : entries)
            if (e->applied && e->deviceId == deviceId && e->instanceId == instanceId) return true;
        return false;
    }

    // Restores the sessions behind pending records that are alive again. A record is dropped once its
    // session was seen: restored when still in the state AdMutify left it, untouched when the user changed it.
    int ResolvePending(bool forceWrite = false) {
        int restored = 0;
        bool changed = forceWrite;
        if (!pending.empty()) {
            std::vector<bool> done(pending.size(), false);
            const uint64_t now = NowUnix();
            for (auto& dev : Devices()) {
                for (auto& s : SessionsOf(dev, false)) {
                    if (IsAppliedEntry(dev.id, s.instanceId)) continue;
                    for (size_t i = 0; i < pending.size(); ++i) {
                        const SavedSession& p = pending[i];
                        if (p.deviceId != dev.id) continue;
                        // The instance id is unique to one session; the session id repeats across restarts, so it only
                        // counts while the record is young.
                        bool match = p.instanceId == s.instanceId ||
                                     (!p.sessionId.empty() && p.sessionId == s.sessionId && RecordFresh(p.savedAt, now));
                        if (!match) continue;
                        Cur c = CompareApplied(s.vol.Get(), p.mode, p.appliedVolume);
                        if (c == Cur::Unreadable) continue;
                        if (c == Cur::Same) {
                            if (!PutOld(s.vol.Get(), p.mode, p.oldMute, p.oldVolume)) continue;
                            ++restored;
                        }
                        done[i] = true;
                    }
                }
            }
            std::vector<SavedSession> keep;
            for (size_t i = 0; i < pending.size(); ++i)
                if (!done[i] && RecordFresh(pending[i].savedAt, now)) keep.push_back(std::move(pending[i]));
            if (keep.size() != done.size()) changed = true;
            pending = std::move(keep);
        }
        if (changed) WriteState();
        return restored;
    }

    // A session opened during a reaction (new stream, process restart) can inherit the applied mute or level.
    // Its live values are then not the user's: take them from a sibling that was applied before this call.
    void InheritOld(Entry& e, Mode mode, const std::vector<Entry*>& fresh) {
        if (e.sessionId.empty()) return;
        const bool liveMute = e.oldMute != FALSE;
        bool m = false;
        float v = 1.0f;
        for (auto& o : entries) {
            if (o.get() == &e || !o->applied || o->appliedMode != mode) continue;
            if (o->deviceId != e.deviceId || o->sessionId != e.sessionId) continue;
            if (std::find(fresh.begin(), fresh.end(), o.get()) != fresh.end()) continue;
            if (InheritedOld(mode, Saved(*o), liveMute, e.oldVolume, m, v)) {
                e.oldMute = m ? TRUE : FALSE;
                e.oldVolume = v;
                return;
            }
        }
        const uint64_t now = NowUnix();
        for (const SavedSession& p : pending) {
            if (p.deviceId != e.deviceId || p.sessionId != e.sessionId || !RecordFresh(p.savedAt, now)) continue;
            if (InheritedOld(mode, p, liveMute, e.oldVolume, m, v)) {
                e.oldMute = m ? TRUE : FALSE;
                e.oldVolume = v;
                return;
            }
        }
    }

    // ChangeLevel: the slot accepts both levels while the write is in flight.
    void SetSlotLevel(const Entry& e, float previous, float now) {
        if (e.slot < 0) return;
        slots[e.slot].previousVolume = previous;
        slots[e.slot].appliedVolume = now;
    }

    void AddCrashSlot(Entry& e) {
        for (int i = 0; i < kMaxCrashSlots; ++i) {
            if (slots[i].vol) continue;
            e.vol->AddRef();
            slots[i].mute = e.oldMute;
            slots[i].volume = e.oldVolume;
            slots[i].appliedVolume = e.appliedVolume;
            slots[i].previousVolume = e.appliedVolume;
            slots[i].ev = e.ev;
            slots[i].mode = e.appliedMode;
            std::atomic_ref<ISimpleAudioVolume*>(slots[i].vol).store(e.vol.Get(), std::memory_order_release);
            e.slot = i;
            return;
        }
        crashSlotsFull = true;
    }

    void RegisterDeviceNotifications() {
        if (!notify) return;
        const std::vector<DeviceRef>& current = Devices();
        // A device that went away, or came back with a new manager, loses its old registration first.
        for (auto it = notifiedDevices.begin(); it != notifiedDevices.end();) {
            bool alive = false;
            for (auto& dev : current)
                if (dev.id == it->id && dev.mgr.Get() == it->mgr.Get()) alive = true;
            if (alive) {
                ++it;
            } else {
                it->mgr->UnregisterSessionNotification(notify.Get());
                it = notifiedDevices.erase(it);
            }
        }
        for (auto& dev : current) {
            bool known = false;
            for (auto& n : notifiedDevices)
                if (n.id == dev.id) known = true;
            if (known) continue;
            // Core Audio delivers OnSessionCreated only after the enumerator was requested on the manager.
            ComPtr<IAudioSessionEnumerator> tmp;
            dev.mgr->GetSessionEnumerator(&tmp);
            if (SUCCEEDED(dev.mgr->RegisterSessionNotification(notify.Get()))) notifiedDevices.push_back(dev);
        }
    }

    // Puts the saved values back. keepOverrides (mode switch inside one reaction): a session the user changed stays
    // marked so Apply leaves it alone until a full restore ends the reaction.
    int RestoreEntries(int* userChanged, bool keepOverrides) {
        if (CrashInProgress()) return 0;
        if (!keepOverrides)
            for (auto& e : entries) e->overridden = false;
        int restored = 0;
        int changed = 0;
        for (auto& e : entries) {
            if (!e->applied) continue;
            bool skip = e->ev->userChanged;
            // The change callback can arrive late, so read the property again before writing.
            if (!skip && CompareApplied(e->vol.Get(), e->appliedMode, e->appliedVolume) == Cur::Changed) skip = true;
            if (skip) {
                ++changed;
                if (keepOverrides) e->overridden = true;
            } else {
                bool ok = PutOld(e->vol.Get(), e->appliedMode, e->oldMute != FALSE, e->oldVolume);
                AudioSessionState st = AudioSessionStateInactive;
                bool expired = !e->ctrl || FAILED(e->ctrl->GetState(&st)) || st == AudioSessionStateExpired;
                if (ok && !expired) ++restored;
                else AddPending(Impl::Saved(*e));
            }
            e->applied = false;
            e->ev->Clear();
            ReleaseSlot(*e);
        }
        crashSlotsFull = false;
        if (userChanged) *userChanged = changed;
        WriteState();
        Prune();
        // A session that expired while restoring may already be back (Spotify restarted in between).
        if (!keepOverrides) ResolvePending();
        return restored;
    }

    void Prune() {
        for (auto it = entries.begin(); it != entries.end();) {
            AudioSessionState st = AudioSessionStateInactive;
            bool gone = true;
            if ((*it)->ctrl && SUCCEEDED((*it)->ctrl->GetState(&st))) gone = st == AudioSessionStateExpired;
            if (gone && !(*it)->applied) {
                ReleaseEntry(**it);
                it = entries.erase(it);
            } else {
                ++it;
            }
        }
    }
};

bool UserOverrode(Mode mode, bool expectedMute, float expectedVolume, bool newMute, float newVolume) {
    if (mode == Mode::Mute) return newMute != expectedMute;
    return std::fabs(newVolume - expectedVolume) > 0.001f;
}

bool RecordFresh(uint64_t savedAt, uint64_t nowUnix) {
    // A clock set back leaves savedAt in the future: that still counts as young.
    return savedAt != 0 && (savedAt >= nowUnix || nowUnix - savedAt <= kPendingMaxAgeSeconds);
}

bool InheritedOld(Mode mode, const SavedSession& sibling, bool liveMute, float liveVolume, bool& oldMute, float& oldVolume) {
    if (sibling.mode != mode) return false;
    bool same = mode == Mode::Mute ? liveMute : std::fabs(liveVolume - sibling.appliedVolume) <= 0.001f;
    if (!same) return false;
    oldMute = sibling.oldMute;
    oldVolume = sibling.oldVolume;
    return true;
}

float LoweredVolume(float oldVolume, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return oldVolume * static_cast<float>(percent) / 100.0f;
}

std::string FormatStateFile(const std::vector<SavedSession>& sessions) {
    std::string out = "version=2\r\n";
    for (const SavedSession& s : sessions) {
        char oldVol[32];
        char appliedVol[32];
        std::snprintf(oldVol, sizeof(oldVol), "%.9g", static_cast<double>(s.oldVolume));
        std::snprintf(appliedVol, sizeof(appliedVol), "%.9g", static_cast<double>(s.appliedVolume));
        out += "session=" + EscapeField(s.deviceId) + "|" + EscapeField(s.instanceId) + "|" + EscapeField(s.sessionId) + "|" +
               (s.mode == Mode::Mute ? "m" : "l") + "|" + (s.oldMute ? "1" : "0") + "|" + oldVol + "|" + appliedVol + "|" +
               std::to_string(s.savedAt) + "\r\n";
    }
    return out;
}

namespace {

bool ParseVolume(std::string_view f, float& v) {
    if (f.empty()) return false;
    float x = 0.0f;
    auto r = std::from_chars(f.data(), f.data() + f.size(), x);
    if (r.ec != std::errc() || r.ptr != f.data() + f.size()) return false;
    if (!std::isfinite(x) || x < 0.0f || x > 1.0f) return false;
    v = x;
    return true;
}

}  // namespace

bool ParseStateFile(std::string_view text, std::vector<SavedSession>& out) {
    out.clear();
    size_t pos = 0;
    bool sawVersion = false;
    bool versionOk = true;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.rfind("version=", 0) == 0) {
            int v = 0;
            std::string_view num = line.substr(8);
            auto r = std::from_chars(num.data(), num.data() + num.size(), v);
            sawVersion = true;
            if (r.ec != std::errc() || r.ptr != num.data() + num.size() || v != 2) versionOk = false;
            continue;
        }
        if (line.rfind("session=", 0) != 0) continue;
        std::string_view rest = line.substr(8);
        std::string_view parts[8];
        int n = 0;
        size_t start = 0;
        while (n < 8) {
            size_t bar = rest.find('|', start);
            if (bar == std::string_view::npos) {
                parts[n++] = rest.substr(start);
                break;
            }
            parts[n++] = rest.substr(start, bar - start);
            start = bar + 1;
        }
        // The eighth field (saved time) is missing in files from before it existed: 0 = unknown.
        if (n != 7 && n != 8) continue;
        if (parts[3] != "m" && parts[3] != "l") continue;
        if ((parts[4] != "0" && parts[4] != "1")) continue;
        SavedSession s;
        if (!ParseVolume(parts[5], s.oldVolume) || !ParseVolume(parts[6], s.appliedVolume)) continue;
        s.deviceId = UnescapeField(parts[0]);
        s.instanceId = UnescapeField(parts[1]);
        s.sessionId = UnescapeField(parts[2]);
        s.mode = parts[3] == "m" ? Mode::Mute : Mode::Lower;
        s.oldMute = parts[4] == "1";
        if (n == 8) {
            auto r = std::from_chars(parts[7].data(), parts[7].data() + parts[7].size(), s.savedAt);
            if (parts[7].empty() || r.ec != std::errc() || r.ptr != parts[7].data() + parts[7].size()) continue;
        }
        out.push_back(std::move(s));
    }
    if (!sawVersion || !versionOk) {
        out.clear();
        return false;
    }
    return true;
}

AudioControl::AudioControl() : impl_(std::make_unique<Impl>()) {}

AudioControl::~AudioControl() { Shutdown(); }

bool AudioControl::Init(HWND notifyWindow) {
    if (impl_->enumerator) return true;
    impl_->hwnd = notifyWindow;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    impl_->comInited = SUCCEEDED(hr);
    if (CoIncrementMTAUsage(&impl_->mtaCookie) == S_OK) impl_->mtaInited = true;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&impl_->enumerator))))
        return false;
    impl_->notify.Attach(new SessionNotify(notifyWindow));
    impl_->deviceNotify.Attach(new DeviceNotify(notifyWindow, &impl_->devicesDirty));
    if (FAILED(impl_->enumerator->RegisterEndpointNotificationCallback(impl_->deviceNotify.Get())))
        impl_->deviceNotify.Reset();
    // New sessions are announced from the start, so a Spotify that starts hidden later is noticed.
    impl_->RegisterDeviceNotifications();
    g_active = this;
    return true;
}

void AudioControl::Shutdown() {
    if (!impl_) return;
    if (g_active == this) g_active = nullptr;
    UnwatchSessions();
    for (auto& n : impl_->notifiedDevices)
        if (impl_->notify) n.mgr->UnregisterSessionNotification(impl_->notify.Get());
    impl_->notifiedDevices.clear();
    impl_->devices.clear();
    impl_->devicesDirty = true;
    // Everything holding a COM pointer goes before COM is torn down below.
    impl_->captureDevices.clear();
    impl_->captureDirty = true;
    impl_->exeCache.clear();
    impl_->notify.Reset();
    if (impl_->enumerator && impl_->deviceNotify)
        impl_->enumerator->UnregisterEndpointNotificationCallback(impl_->deviceNotify.Get());
    impl_->deviceNotify.Reset();
    impl_->enumerator.Reset();
    if (impl_->mtaInited) CoDecrementMTAUsage(impl_->mtaCookie);
    impl_->mtaInited = false;
    if (impl_->comInited) CoUninitialize();
    impl_->comInited = false;
}

int AudioControl::Apply(Mode mode, int percent) {
    if (CrashInProgress()) return 0;
    // A restarted app can come back with the mute AdMutify left behind: undo that before reading old values.
    impl_->ResolvePending();
    std::vector<Entry*> fresh;
    for (auto& dev : impl_->Devices()) {
        for (auto& s : impl_->SessionsOf(dev, true)) {
            Entry* e = impl_->GetOrAdd(dev.id, s);
            if (e->applied || e->overridden) continue;
            if (FAILED(e->vol->GetMute(&e->oldMute)) || FAILED(e->vol->GetMasterVolume(&e->oldVolume))) continue;
            impl_->InheritOld(*e, mode, fresh);
            e->applied = true;
            e->appliedAt = NowUnix();
            e->appliedMode = mode;
            e->appliedVolume = mode == Mode::Mute ? e->oldVolume : LoweredVolume(e->oldVolume, percent);
            if (mode == Mode::Mute) e->ev->Expect(mode, true, e->oldVolume);
            else e->ev->Expect(mode, e->oldMute != FALSE, e->appliedVolume);
            fresh.push_back(e);
        }
    }
    if (fresh.empty()) return 0;

    impl_->WriteState();
    int written = 0;
    bool failed = false;
    for (Entry* e : fresh) {
        impl_->AddCrashSlot(*e);
        HRESULT hr = mode == Mode::Mute ? e->vol->SetMute(TRUE, &kAdMutifyEventContext)
                                        : e->vol->SetMasterVolume(LoweredVolume(e->oldVolume, percent), &kAdMutifyEventContext);
        if (SUCCEEDED(hr)) {
            ++written;
            continue;
        }
        // Nothing changed on that session, so there is nothing to give back later.
        impl_->ReleaseSlot(*e);
        e->applied = false;
        e->ev->Clear();
        failed = true;
    }
    if (failed) impl_->WriteState();
    return written;
}

int AudioControl::Restore(int* userChanged) { return impl_->RestoreEntries(userChanged, false); }

int AudioControl::SwitchMode(Mode mode, int percent) {
    if (CrashInProgress()) return 0;
    impl_->RestoreEntries(nullptr, true);
    return Apply(mode, percent);
}

int AudioControl::ChangeLevel(int percent) {
    if (CrashInProgress()) return 0;
    int changed = 0;
    struct Item {
        Entry* e;
        float previous;
    };
    std::vector<Item> items;
    for (auto& e : impl_->entries) {
        if (!e->applied || e->appliedMode != Mode::Lower || e->ev->userChanged) continue;
        if (CompareApplied(e->vol.Get(), Mode::Lower, e->appliedVolume) == Cur::Changed) continue;
        items.push_back({e.get(), e->appliedVolume});
    }
    if (items.empty()) return 0;
    // The state file knows the new level before the volume changes, like in Apply: a kill in between must not
    // look like a user change at the next start.
    for (Item& it : items) {
        it.e->appliedVolume = LoweredVolume(it.e->oldVolume, percent);
        impl_->SetSlotLevel(*it.e, it.previous, it.e->appliedVolume);
    }
    impl_->WriteState();
    bool failed = false;
    for (Item& it : items) {
        Entry* e = it.e;
        // Expected before the write so the change callback it triggers is not taken for the user.
        e->ev->Expect(Mode::Lower, e->oldMute != FALSE, e->appliedVolume, true);
        // The user moved the slider since the check above: their value stays, nothing is written.
        bool wrote = !e->ev->userChanged && SUCCEEDED(e->vol->SetMasterVolume(e->appliedVolume, &kAdMutifyEventContext));
        if (wrote && !e->ev->userChanged) {
            ++changed;
        } else if (wrote) {
            // A late callback of a change the user made before our write: if our value is what the session reads
            // now, our write won and the flag is stale, so the restore stays correct.
            float now = 0.0f;
            if (SUCCEEDED(e->vol->GetMasterVolume(&now)) && std::fabs(now - e->appliedVolume) <= 0.001f) {
                e->ev->userChanged = false;
                ++changed;
            }
        } else {
            // The session still sits at the old level: the entry keeps describing that.
            e->appliedVolume = it.previous;
            impl_->SetSlotLevel(*e, it.previous, it.previous);
            e->ev->Expect(Mode::Lower, e->oldMute != FALSE, it.previous, true);
            failed = true;
        }
    }
    if (failed) impl_->WriteState();
    return changed;
}

int AudioControl::RestoreFromStateFile(bool* fileIgnored) {
    if (fileIgnored) *fileIgnored = false;
    std::string text;
    if (!ReadFileUtf8(StatePath(), text)) return 0;
    std::vector<SavedSession> saved;
    if (!ParseStateFile(text, saved)) {
        if (fileIgnored) *fileIgnored = true;
        DeleteFileW(StatePath().c_str());
        return 0;
    }
    for (const SavedSession& sv : saved) impl_->AddPending(sv);
    return impl_->ResolvePending(true);
}

int AudioControl::PendingCount() const { return static_cast<int>(impl_->pending.size()); }

bool AudioControl::IsSpotifyAudioActive() {
    // Sessions already being watched answer a positive case without enumerating the devices.
    for (auto& e : impl_->entries) {
        AudioSessionState st = AudioSessionStateInactive;
        if (e->ctrl && SUCCEEDED(e->ctrl->GetState(&st)) && st == AudioSessionStateActive) return true;
    }
    for (auto& dev : impl_->Devices()) {
        for (auto& s : impl_->SessionsOf(dev, true)) {
            AudioSessionState st = AudioSessionStateInactive;
            if (SUCCEEDED(s.ctrl->GetState(&st)) && st == AudioSessionStateActive) return true;
        }
    }
    return false;
}

bool AudioControl::IsSpotifyAudible() {
    // Watched sessions are asked directly, without enumerating the devices.
    if (!impl_->entries.empty()) {
        for (auto& e : impl_->entries) {
            AudioSessionState st = AudioSessionStateInactive;
            if (!e->ctrl || FAILED(e->ctrl->GetState(&st)) || st != AudioSessionStateActive) continue;
            ComPtr<IAudioMeterInformation> meter;
            float peak = 0.0f;
            if (SUCCEEDED(e->ctrl.As(&meter)) && SUCCEEDED(meter->GetPeakValue(&peak)) && peak > 0.001f) return true;
        }
        return false;
    }
    for (auto& dev : impl_->Devices()) {
        for (auto& s : impl_->SessionsOf(dev, true, false, false)) {
            AudioSessionState st = AudioSessionStateInactive;
            if (FAILED(s.ctrl->GetState(&st)) || st != AudioSessionStateActive) continue;
            ComPtr<IAudioMeterInformation> meter;
            float peak = 0.0f;
            if (SUCCEEDED(s.ctrl.As(&meter)) && SUCCEEDED(meter->GetPeakValue(&peak)) && peak > 0.001f) return true;
        }
    }
    return false;
}

bool AudioControl::IsContentSoundPlaying(std::wstring* exeName) {
    if (!impl_->enumerator) return false;
    struct PollEnd {
        Impl* impl;
        ~PollEnd() { impl->EndExePoll(); }
    } pollEnd{impl_.get()};
    const DWORD self = GetCurrentProcessId();
    std::vector<std::wstring> candidates;
    for (auto& dev : impl_->Devices()) {
        for (auto& s : impl_->SessionsOf(dev, false, false, false)) {
            if (s.pid == 0 || s.pid == self) continue;
            ComPtr<IAudioSessionControl2> c2;
            if (FAILED(s.ctrl.As(&c2)) || c2->IsSystemSoundsSession() == S_OK) continue;
            AudioSessionState st = AudioSessionStateInactive;
            if (FAILED(s.ctrl->GetState(&st)) || st != AudioSessionStateActive) continue;
            ComPtr<IAudioMeterInformation> meter;
            float peak = 0.0f;
            if (FAILED(s.ctrl.As(&meter)) || FAILED(meter->GetPeakValue(&peak)) || peak <= kContentPeak) continue;
            BOOL muted = FALSE;
            float vol = 0.0f;
            if (FAILED(s.vol->GetMute(&muted)) || muted || FAILED(s.vol->GetMasterVolume(&vol)) || vol <= kContentVolume) continue;
            std::wstring exe = impl_->CachedExeName(s.pid);
            if (exe.empty() || !IsContentApp(exe) || IsTargetProcess(s.pid)) continue;
            bool known = false;
            for (const std::wstring& c : candidates)
                if (EqualsNoCase(c, exe)) known = true;
            if (!known) candidates.push_back(std::move(exe));
        }
    }
    if (candidates.empty()) return false;

    // A call keeps the microphone open in the same app: such an app does not count.
    for (auto& dev : impl_->CaptureDevices()) {
        // Nothing left to rule out: the remaining capture devices are not enumerated.
        if (candidates.empty()) break;
        for (auto& s : impl_->SessionsOf(dev, false, true, false)) {
            if (candidates.empty()) break;
            AudioSessionState st = AudioSessionStateInactive;
            if (s.pid == 0 || FAILED(s.ctrl->GetState(&st)) || st != AudioSessionStateActive) continue;
            std::wstring exe = impl_->CachedExeName(s.pid);
            for (auto it = candidates.begin(); it != candidates.end();) {
                if (EqualsNoCase(*it, exe)) it = candidates.erase(it);
                else ++it;
            }
        }
    }
    if (candidates.empty()) return false;
    if (exeName) *exeName = candidates.front();
    return true;
}

bool AudioControl::HasAppliedSessions() const {
    for (auto& e : impl_->entries)
        if (e->applied) return true;
    return false;
}

void AudioControl::WatchSessions() {
    impl_->Prune();
    impl_->ResolvePending();
    impl_->RegisterDeviceNotifications();
    for (auto& dev : impl_->Devices())
        for (auto& s : impl_->SessionsOf(dev, true)) impl_->GetOrAdd(dev.id, s);
}

void AudioControl::UnwatchSessions() {
    for (auto& e : impl_->entries) impl_->ReleaseEntry(*e);
    impl_->entries.clear();
}

bool AudioControl::RestoreUnsafe() {
    bool allRestored = true;
    for (int i = 0; i < kMaxCrashSlots; ++i) {
        CrashSlot& s = impl_->slots[i];
        ISimpleAudioVolume* vol = std::atomic_ref<ISimpleAudioVolume*>(s.vol).load(std::memory_order_acquire);
        if (!vol) continue;
        if (s.ev && s.ev->userChanged) continue;
        // Write only while the session still reads what AdMutify applied: a late user change has no callback yet.
        Cur cur = CompareApplied(vol, s.mode, s.appliedVolume);
        if (cur == Cur::Changed && s.mode == Mode::Lower) cur = CompareApplied(vol, s.mode, s.previousVolume);
        if (cur == Cur::Changed) continue;
        HRESULT hr = s.mode == Mode::Mute ? vol->SetMute(s.mute, &kAdMutifyEventContext)
                                          : vol->SetMasterVolume(s.volume, &kAdMutifyEventContext);
        if (FAILED(hr)) allRestored = false;
    }
    // The crash handler never rewrites files: pending records stay on disk for the next start, and so does
    // the record of a session that could not be restored here.
    if (allRestored && impl_->statePath[0] && !impl_->crashSlotsFull && impl_->pending.empty()) DeleteFileW(impl_->statePath);
    return allRestored && !impl_->crashSlotsFull && impl_->pending.empty();
}

bool RestoreAudioUnsafe() { return g_active ? g_active->RestoreUnsafe() : true; }
