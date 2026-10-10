// Test stand-in for the Spotify desktop app: a window of class Chrome_WidgetWin_1 whose title follows a script,
// optionally with a quiet tone so that a real audio session exists. Built as Spotify.exe.
//
// Usage: Spotify.exe [script.txt] [--once] [--tone]
//   Script lines: <duration_ms>|<title>   (# starts a comment). Each title stays for its duration.
//   --once   play the script one time and keep the last title; default is to loop.
//   WM_COPYDATA: dwData 1 = set title (UTF-16 text), dwData 2 = quit.
//   WM_APPCOMMAND APPCOMMAND_MEDIA_PLAY_PAUSE toggles pause: paused shows the title "Spotify Free" and the tone
//   keeps its audio session but writes silence; playing again restores the scripted title and the tone.

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct Step {
    UINT durationMs;
    std::wstring title;
};

constexpr UINT_PTR kStepTimer = 1;
constexpr ULONG_PTR kCmdSetTitle = 1;
constexpr ULONG_PTR kCmdQuit = 2;

std::vector<Step> g_steps;
size_t g_index = 0;
bool g_once = false;
std::wstring g_logPath;
std::wstring g_scriptTitle;
std::atomic<bool> g_paused{false};

void Log(const char* text) {
    HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char line[256];
    int n = snprintf(line, sizeof(line), "%s\r\n", text);
    DWORD written = 0;
    WriteFile(h, line, static_cast<DWORD>(n), &written, nullptr);
    CloseHandle(h);
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool LoadScript(const wchar_t* path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string text;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) text.append(buf, got);
    CloseHandle(h);

    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line.erase(0, 3);
        if (line.empty() || line[0] == '#') continue;
        size_t bar = line.find('|');
        if (bar == std::string::npos) continue;
        long ms = std::strtol(line.substr(0, bar).c_str(), nullptr, 10);
        if (ms <= 0) continue;
        g_steps.push_back({static_cast<UINT>(ms), Widen(line.substr(bar + 1))});
    }
    return !g_steps.empty();
}

// The window shows the scripted title while playing and the plain idle title while paused.
void ShowTitle(HWND hwnd) { SetWindowTextW(hwnd, g_paused ? L"Spotify Free" : g_scriptTitle.c_str()); }

void ShowStep(HWND hwnd) {
    const Step& s = g_steps[g_index];
    g_scriptTitle = s.title;
    ShowTitle(hwnd);
    SetTimer(hwnd, kStepTimer, s.durationMs, nullptr);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER:
            if (wParam == kStepTimer) {
                KillTimer(hwnd, kStepTimer);
                if (g_index + 1 < g_steps.size()) {
                    ++g_index;
                } else if (!g_once) {
                    g_index = 0;
                } else {
                    return 0;
                }
                ShowStep(hwnd);
            }
            return 0;
        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
            if (cds->dwData == kCmdSetTitle && cds->lpData) {
                KillTimer(hwnd, kStepTimer);
                std::wstring title(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
                while (!title.empty() && title.back() == L'\0') title.pop_back();
                g_scriptTitle = title;
                ShowTitle(hwnd);
            } else if (cds->dwData == kCmdQuit) {
                DestroyWindow(hwnd);
            }
            return TRUE;
        }
        case WM_APPCOMMAND:
            if (GET_APPCOMMAND_LPARAM(lParam) == APPCOMMAND_MEDIA_PLAY_PAUSE) {
                g_paused = !g_paused;
                Log(g_paused ? "paused" : "playing");
                ShowTitle(hwnd);
                return TRUE;
            }
            break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Plays a quiet sine through the default render device so a real audio session exists.
class Tone {
public:
    void Start() { thread_ = std::thread([this] { Run(); }); }
    void Stop() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }

private:
    void Run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            ComPtr<IMMDeviceEnumerator> enumerator;
            ComPtr<IMMDevice> device;
            if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
                FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
                Log("no audio endpoint");
                CoUninitialize();
                return;
            }
            ComPtr<IAudioClient> client;
            WAVEFORMATEX* format = nullptr;
            if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()))) ||
                FAILED(client->GetMixFormat(&format)) ||
                FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, format, nullptr))) {
                Log("audio client setup failed");
                if (format) CoTaskMemFree(format);
                CoUninitialize();
                return;
            }
            UINT32 bufferFrames = 0;
            client->GetBufferSize(&bufferFrames);
            ComPtr<IAudioRenderClient> render;
            if (FAILED(client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render.GetAddressOf())))) {
                Log("render client failed");
                CoTaskMemFree(format);
                CoUninitialize();
                return;
            }
            Log("tone started");
            client->Start();

            const double step = 2.0 * 3.14159265358979323846 * 440.0 / format->nSamplesPerSec;
            const float amplitude = 0.05f;
            double phase = 0.0;
            while (!stop_) {
                Sleep(20);
                UINT32 padding = 0;
                if (FAILED(client->GetCurrentPadding(&padding))) break;
                UINT32 frames = bufferFrames - padding;
                if (frames == 0) continue;
                BYTE* data = nullptr;
                if (FAILED(render->GetBuffer(frames, &data))) break;
                for (UINT32 i = 0; i < frames; ++i) {
                    float v = g_paused ? 0.0f : amplitude * static_cast<float>(std::sin(phase));
                    phase += step;
                    for (WORD c = 0; c < format->nChannels; ++c) {
                        if (format->wBitsPerSample == 32) {
                            reinterpret_cast<float*>(data)[i * format->nChannels + c] = v;
                        } else if (format->wBitsPerSample == 16) {
                            reinterpret_cast<short*>(data)[i * format->nChannels + c] = static_cast<short>(v * 32767.0f);
                        }
                    }
                }
                render->ReleaseBuffer(frames, 0);
            }
            client->Stop();
            CoTaskMemFree(format);
        }
        CoUninitialize();
    }

    std::thread thread_;
    std::atomic<bool> stop_{false};
};

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    wchar_t exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    g_logPath.assign(exe, n);
    size_t slash = g_logPath.find_last_of(L'\\');
    g_logPath = (slash == std::wstring::npos ? L"" : g_logPath.substr(0, slash + 1)) + L"fake_spotify.log";

    std::wstring script;
    bool tone = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--once") g_once = true;
        else if (a == L"--tone") tone = true;
        else if (script.empty()) script = a;
    }
    if (argv) LocalFree(argv);

    if (script.empty() || !LoadScript(script.c_str())) g_steps.push_back({1000, L"Spotify Free"});
    bool scripted = !script.empty() && g_steps.size() > 1;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"Chrome_WidgetWin_1";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, g_steps[0].title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 120,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    g_scriptTitle = g_steps[0].title;
    if (scripted) ShowStep(hwnd);
    Log("started");

    Tone player;
    if (tone) player.Start();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (tone) player.Stop();
    return 0;
}
