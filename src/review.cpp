#include "review.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "app.h"
#include "autostart.h"
#include "crash.h"
#include "settings.h"
#include "settings_window.h"
#include "strings.h"
#include "theme.h"
#include "tray.h"
#include "util.h"

using Microsoft::WRL::ComPtr;

namespace {

const wchar_t* const kSurfaces[] = {L"menu", L"menu-ad", L"menu-off", L"menu-lang", L"settings", L"settings-lower", L"settings-media", L"settings-music"};

void Print(const std::string& text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!out || out == INVALID_HANDLE_VALUE)
            out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (!out || out == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

bool SavePng(HBITMAP bmp, const std::wstring& path) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IWICBitmap> source;
    if (FAILED(factory->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &source))) return false;
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(encoder->CreateNewFrame(&frame, &props)) || FAILED(frame->Initialize(props.Get()))) return false;
    UINT w = 0, h = 0;
    source->GetSize(&w, &h);
    frame->SetSize(w, h);
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    frame->SetPixelFormat(&format);
    if (FAILED(frame->WriteSource(source.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) return false;
    return true;
}

// Captures one window by asking it to render itself.
bool CaptureWindow(HWND hwnd, const std::wstring& path) {
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return false;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HDC windowDc = GetWindowDC(hwnd);
    if (!windowDc) {
        Log("capture: GetWindowDC failed (%lu)", GetLastError());
        return false;
    }
    HDC mem = CreateCompatibleDC(windowDc);
    HBITMAP bmp = mem ? CreateCompatibleBitmap(windowDc, w, h) : nullptr;
    bool ok = false;
    if (!mem || !bmp) {
        Log("capture: no bitmap for %dx%d (%lu)", w, h, GetLastError());
    } else {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = PrintWindow(hwnd, mem, 0x2 /* PW_RENDERFULLCONTENT */) != FALSE;
        SelectObject(mem, old);
        if (!ok) Log("capture: PrintWindow failed (%lu)", GetLastError());
        else if (!(ok = SavePng(bmp, path))) Log("capture: PNG encode failed");
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    ReleaseDC(hwnd, windowDc);
    return ok;
}

void PumpFor(DWORD ms) {
    ULONGLONG end = GetTickCount64() + ms;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        ULONGLONG now = GetTickCount64();
        if (now >= end) break;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(end - now), QS_ALLINPUT);
    }
}

// The first popup menu window of this process; another program's open menu must not be photographed.
HWND FindOwnMenuWindow() {
    HWND found = nullptr;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            wchar_t cls[16] = L"";
            GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
            if (wcscmp(cls, L"#32768") != 0) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId()) return TRUE;
            *reinterpret_cast<HWND*>(lp) = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

struct MenuShot {
    std::wstring path;
    int tries = 0;
    bool saved = false;
};
MenuShot g_menuShot;
UINT g_menuHoldMs = 600;

LRESULT CALLBACK MenuOwnerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MEASUREITEM && TrayMenuMeasure(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) return TRUE;
    if (msg == WM_DRAWITEM && TrayMenuDraw(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam))) return TRUE;
    if (msg == WM_TIMER && wParam == 1) {
        HWND menu = FindOwnMenuWindow();
        if (menu) {
            g_menuShot.saved = CaptureWindow(menu, g_menuShot.path);
            KillTimer(hwnd, 1);
            EndMenu();
        } else if (++g_menuShot.tries >= 8) {
            KillTimer(hwnd, 1);
            EndMenu();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShootMenu(const std::wstring& id, const std::wstring& path) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &MenuOwnerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AdMutify.Review";
        RegisterClassExW(&wc);
        registered = true;
    }
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"AdMutify.Review", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    if (!owner) return false;
    ShowWindow(owner, SW_SHOWNOACTIVATE);

    bool enabled = id != L"menu-off";
    const wchar_t* status = id == L"menu-ad" ? Str(StrId::StatusMuted)
                            : id == L"menu-off" ? Str(StrId::StatusDisabled)
                                                : Str(StrId::StatusActive);
    const wchar_t* languages[kLanguageCount];
    languages[0] = Str(StrId::LangSystem);
    for (int i = 1; i < kLanguageCount; ++i) languages[i] = LanguageNativeName(static_cast<Language>(i));
    HMENU menu = id == L"menu-lang" ? BuildChoiceMenu(languages, kLanguageCount, 1, 0, 0) : BuildTrayMenu(enabled, status);
    g_menuShot = MenuShot{path, 0, false};
    SetTimer(owner, 1, g_menuHoldMs, nullptr);
    SetForegroundWindow(owner);
    TrackTrayMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, 300, 300, owner);
    DestroyTrayMenu(menu);
    DestroyWindow(owner);
    return g_menuShot.saved;
}

bool ShootSettings(const std::wstring& path) {
    OpenSettingsWindow();
    PumpFor(600);
    HWND hwnd = SettingsWindowHandle();
    if (!hwnd) return false;
    bool ok = CaptureWindow(hwnd, path);
    DestroyWindow(hwnd);
    PumpFor(100);
    return ok;
}

bool Shoot(const std::wstring& id, Language language, bool dark, const std::wstring& outDir) {
    SetThemeOverride(dark ? 2 : 1);
    Settings s;
    s.language = language;
    s.mode = id == L"settings-lower" ? Mode::Lower : Mode::Mute;
    s.media = id == L"settings-media" ? MediaMode::Lower : MediaMode::Off;
    s.mediaPercent = id == L"settings-media" ? 45 : 20;
    if (id == L"settings-music") {
        s.adMusic = AdMusic::Shuffle;
        s.adMusicPath = L"D:\\Music\\Break music";
        s.adMusicVolume = 60;
    }
    AppUseInMemorySettings(s);
    SetLanguage(s.language);

    std::wstring name = id + L"_" + Utf8ToWide(LanguageCode(language)) + L"_" + (dark ? L"dark" : L"light") + L".png";
    std::wstring path = outDir + L"\\" + name;
    if (id.rfind(L"menu", 0) == 0) {
        ApplyMenuAppMode(dark ? 2 : 3);
        return ShootMenu(id, path);
    }
    return ShootSettings(path);
}

struct LayoutSnap {
    RECT window{};
    std::vector<RECT> children;
};

BOOL CALLBACK CollectChild(HWND child, LPARAM param) {
    auto* snap = reinterpret_cast<std::pair<HWND, LayoutSnap*>*>(param);
    if (GetParent(child) != snap->first) return TRUE;
    RECT r;
    GetWindowRect(child, &r);
    MapWindowPoints(nullptr, snap->first, reinterpret_cast<POINT*>(&r), 2);
    if (!IsWindowVisible(child)) r = RECT{-1, -1, -1, -1};
    snap->second->children.push_back(r);
    return TRUE;
}

LayoutSnap TakeSnap(HWND hwnd) {
    LayoutSnap snap;
    GetWindowRect(hwnd, &snap.window);
    std::pair<HWND, LayoutSnap*> param{hwnd, &snap};
    EnumChildWindows(hwnd, &CollectChild, reinterpret_cast<LPARAM>(&param));
    return snap;
}

std::string RectText(const RECT& r) {
    char buf[96];
    snprintf(buf, sizeof(buf), "(%ld,%ld %ldx%ld)", r.left, r.top, r.right - r.left, r.bottom - r.top);
    return buf;
}

// Simulates dragging the settings window onto a monitor with other scaling (100 % or 150 %) and back. The window must
// take the position Windows suggests (no jump back, which would flip the DPI again) and lay out exactly as before.
int RunDpiTest(const std::wstring& outDir) {
    SetThemeOverride(1);
    Settings s;
    s.language = Language::English;
    AppUseInMemorySettings(s);
    SetLanguage(s.language);
    OpenSettingsWindow();
    PumpFor(600);
    HWND hwnd = SettingsWindowHandle();
    if (!hwnd) return 2;
    int failures = 0;
    const LayoutSnap before = TakeSnap(hwnd);
    CaptureWindow(hwnd, outDir + L"\\dpi_start.png");

    const UINT d0 = GetDpiForWindow(hwnd);
    const UINT d1 = d0 == 96 ? 144 : 96;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const LONG w0 = before.window.right - before.window.left;
    const LONG h0 = before.window.bottom - before.window.top;
    // Half of the window already sits on the next monitor to the right, as at the moment Windows switches the DPI.
    RECT up{mi.rcWork.right - w0 / 2, mi.rcWork.top + 20, 0, 0};
    up.right = up.left + MulDiv(w0, d1, d0);
    up.bottom = up.top + MulDiv(h0, d1, d0);
    SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(d1, d1), reinterpret_cast<LPARAM>(&up));
    PumpFor(300);
    const LayoutSnap scaled = TakeSnap(hwnd);
    CaptureWindow(hwnd, outDir + L"\\dpi_other.png");
    if (scaled.window.left != up.left || scaled.window.top != up.top) {
        Print("moved after dpi change: suggested " + RectText(up) + " got " + RectText(scaled.window) + "\n");
        ++failures;
    }

    RECT down{scaled.window.left, scaled.window.top, 0, 0};
    down.right = down.left + MulDiv(scaled.window.right - scaled.window.left, d0, d1);
    down.bottom = down.top + MulDiv(scaled.window.bottom - scaled.window.top, d0, d1);
    SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(d0, d0), reinterpret_cast<LPARAM>(&down));
    PumpFor(300);
    const LayoutSnap after = TakeSnap(hwnd);
    CaptureWindow(hwnd, outDir + L"\\dpi_back.png");
    if (after.window.left != down.left || after.window.top != down.top) {
        Print("moved after change back: suggested" + RectText(down) + " got " + RectText(after.window) + "\n");
        ++failures;
    }
    if (after.window.right - after.window.left != w0 || after.window.bottom - after.window.top != h0) {
        Print("size after round trip " + RectText(after.window) + " was " + RectText(before.window) + "\n");
        ++failures;
    }
    for (size_t i = 0; i < before.children.size() && i < after.children.size(); ++i) {
        if (!EqualRect(&before.children[i], &after.children[i])) {
            Print("child " + std::to_string(i) + " " + RectText(before.children[i]) + " -> " + RectText(after.children[i]) + "\n");
            ++failures;
        }
    }
    if (before.children.size() != after.children.size()) ++failures;
    DestroyWindow(hwnd);
    PumpFor(100);
    Print(failures ? "dpi test failed\n" : "dpi test ok\n");
    return failures ? 1 : 0;
}

}  // namespace

bool RunReviewCommand(int argc, wchar_t** argv, int& exitCode) {
    std::wstring shotId;
    std::wstring lang;
    std::wstring theme;
    std::wstring out = L".";
    bool ids = false;
    bool shot = false;
    bool crash = false;
    bool dpiTest = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring_view a = argv[i];
        if (a == L"--review" && i + 1 < argc && std::wstring_view(argv[i + 1]) == L"ids") {
            ids = true;
            ++i;
        } else if (a == L"--shot" && i + 1 < argc) {
            shot = true;
            shotId = argv[++i];
        } else if (a == L"--lang" && i + 1 < argc) {
            lang = argv[++i];
        } else if (a == L"--theme" && i + 1 < argc) {
            theme = argv[++i];
        } else if (a == L"--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (a == L"--hold" && i + 1 < argc) {
            // Keeps the menu open longer so a screen grab of the desktop (VM) shows the real window shape.
            g_menuHoldMs = static_cast<UINT>(std::wcstoul(argv[++i], nullptr, 10));
        } else if (a == L"--dpi-test") {
            dpiTest = true;
        } else if (a == L"--crash-test") {
            crash = true;
        }
    }

    if (crash) {
        InstallCrashHandler();
        ReserveStackGuarantee();
        Log("crash-test");
        *static_cast<volatile int*>(nullptr) = 1;
        exitCode = 3;
        return true;
    }

    if (ids) {
        std::string text;
        for (const wchar_t* s : kSurfaces) text += WideToUtf8(s) + "\n";
        Print(text);
        exitCode = 0;
        return true;
    }

    if (dpiTest) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        SetAutostartKeyOverride(L"Software\\AdMutifyReview\\Run");
        EnsureDirectory(out);
        exitCode = RunDpiTest(out);
        return true;
    }

    if (!shot) return false;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetAutostartKeyOverride(L"Software\\AdMutifyReview\\Run");
    EnsureDirectory(out);

    std::vector<std::wstring> surfaces;
    if (shotId == L"all") {
        for (const wchar_t* s : kSurfaces) surfaces.push_back(s);
    } else {
        bool known = false;
        for (const wchar_t* s : kSurfaces)
            if (shotId == s) known = true;
        if (!known) {
            Print("unknown surface id\n");
            exitCode = 2;
            return true;
        }
        surfaces.push_back(shotId);
    }

    // "--lang all" runs every language; a code runs that one; no value keeps the two original ones (en, de).
    std::vector<Language> languages;
    if (lang == L"all") {
        for (int i = 1; i < kLanguageCount; ++i) languages.push_back(static_cast<Language>(i));
    } else if (lang.empty()) {
        languages = {Language::English, Language::German};
    } else {
        Language one = LanguageFromCode(WideToUtf8(lang));
        if (one == Language::System) {
            Print("unknown language code\n");
            exitCode = 2;
            return true;
        }
        languages.push_back(one);
    }

    int failures = 0;
    for (Language language : languages) {
        for (bool dark : {false, true}) {
            if ((theme == L"dark" && !dark) || (theme == L"light" && dark)) continue;
            for (const std::wstring& id : surfaces) {
                bool ok = Shoot(id, language, dark, out);
                if (!ok) {
                    ++failures;
                    Print("failed: " + WideToUtf8(id) + "\n");
                }
            }
        }
    }
    exitCode = failures ? 1 : 0;
    return true;
}
