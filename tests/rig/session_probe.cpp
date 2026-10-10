// Prints every audio session of every active render device: process name, state, mute, volume.
// Usage: session_probe.exe [filter]   (filter = case-insensitive substring of the process name)
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "ole32.lib")

template <class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

static std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

static std::wstring ProcessName(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"?";
    wchar_t buf[MAX_PATH]; DWORD n = MAX_PATH;
    std::wstring r = QueryFullProcessImageNameW(h, 0, buf, &n) ? buf : L"?";
    CloseHandle(h);
    size_t s = r.find_last_of(L'\\');
    return s == std::wstring::npos ? r : r.substr(s + 1);
}

static int ProbeOnce(const std::wstring& filter);
static float g_setVolume = -1.0f;
static int g_setMute = -1;

// Usage: session_probe.exe [filter] [samples] [interval_ms] [set_volume] [set_mute]
// set_volume (0..1, negative = keep) and set_mute (0/1) are applied once, on the first sample, the way a user would move the mixer slider.
int wmain(int argc, wchar_t** argv) {
    std::wstring filter = argc > 1 ? Lower(argv[1]) : L"";
    int samples = argc > 2 ? _wtoi(argv[2]) : 1;
    int interval = argc > 3 ? _wtoi(argv[3]) : 500;
    float setVolume = argc > 4 ? static_cast<float>(_wtof(argv[4])) : -1.0f;
    int setMute = argc > 5 ? _wtoi(argv[5]) : -1;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    int rc = 0;
    for (int s = 0; s < std::max(1, samples); ++s) {
        SYSTEMTIME t; GetLocalTime(&t);
        wprintf(L"t=%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        g_setVolume = s == 0 ? setVolume : -1.0f;
        g_setMute = s == 0 ? setMute : -1;
        rc = ProbeOnce(filter);
        fflush(stdout);
        if (s + 1 < samples) Sleep(interval);
    }
    CoUninitialize();
    return rc;
}

static int ProbeOnce(const std::wstring& filter) {
    Com<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return 3;
    Com<IMMDeviceCollection> devs;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs))) return 4;
    UINT count = 0; devs->GetCount(&count);
    int found = 0;
    for (UINT i = 0; i < count; ++i) {
        Com<IMMDevice> dev; if (FAILED(devs->Item(i, &dev))) continue;
        Com<IAudioSessionManager2> mgr;
        if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr))) continue;
        Com<IAudioSessionEnumerator> se; if (FAILED(mgr->GetSessionEnumerator(&se))) continue;
        int n = 0; se->GetCount(&n);
        for (int k = 0; k < n; ++k) {
            Com<IAudioSessionControl> c; if (FAILED(se->GetSession(k, &c))) continue;
            Com<IAudioSessionControl2> c2; if (FAILED(c->QueryInterface(IID_PPV_ARGS(&c2)))) continue;
            DWORD pid = 0; c2->GetProcessId(&pid);
            std::wstring name = pid ? ProcessName(pid) : L"system";
            if (!filter.empty() && Lower(name).find(filter) == std::wstring::npos) continue;
            AudioSessionState st = AudioSessionStateInactive; c->GetState(&st);
            Com<ISimpleAudioVolume> vol; float v = -1; BOOL m = FALSE;
            if (SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&vol)))) {
                if (g_setVolume >= 0.0f) vol->SetMasterVolume(g_setVolume, nullptr);
                if (g_setMute >= 0) vol->SetMute(g_setMute ? TRUE : FALSE, nullptr);
                vol->GetMasterVolume(&v);
                vol->GetMute(&m);
            }
            wprintf(L"[dev=%u pid=%lu name=%ls state=%d mute=%d volume=%.3f] ", i, pid, name.c_str(), (int)st, (int)m, v);
            ++found;
        }
    }
    wprintf(L"devices=%u sessions=%d\n", count, found);
    return 0;
}
