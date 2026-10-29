#include "settings_window.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>

namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "ad_music.h"
#include "app.h"
#include "autostart.h"
#include "stats.h"
#include "strings.h"
#include "theme.h"
#include "tray.h"
#include "version.h"

namespace {

constexpr wchar_t kClassName[] = L"AdMutify.Settings";
constexpr UINT_PTR kResetTimer = 1;
constexpr UINT kResetArmMs = 4000;
constexpr DWORD kCaptionColor = 35;
constexpr COLORREF kDefaultCaptionColor = 0xFFFFFFFF;
constexpr UINT kMsgOpenCombo = WM_APP + 20;

struct Win {
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HFONT headFont = nullptr;
    HFONT bigFont = nullptr;
    HWND tip = nullptr;
    HBRUSH bg = nullptr;
    HBRUSH card = nullptr;
    UINT dpi = 96;
    ULONG_PTR gdiplus = 0;
    int hotId = 0;
    // The reset button waits for a second click.
    bool resetArmed = false;
    // Control id of the box whose menu is open, 0 when none.
    int menuOpenId = 0;
    // A click on the box that closed its menu must not open it again.
    ULONGLONG ignoreClickUntil = 0;
    int ignoreClickId = 0;
    RECT cards[4]{};
    bool loading = false;
    bool classRegistered = false;
};

Win w;

HWND Ctl(int id) { return GetDlgItem(w.hwnd, id); }

int Scale(int px) { return MulDiv(px, static_cast<int>(w.dpi), 96); }

// The closed choice boxes (video reaction, language, ad music): control id and the strings of its entries.
constexpr size_t kComboMaxItems = kLanguageCount;
struct ComboDef {
    int id;
    size_t count;
    StrId items[kComboMaxItems];
};
// The language box lists "System" (translated) and then each language by its own name; its entry order is the order of
// the Language enum, so the selection index and the enum value are the same number (the two helpers below are the only
// place that relies on it).
constexpr ComboDef kCombos[] = {
    {IDC_MEDIA, 4, {StrId::MediaOff, StrId::MediaLower, StrId::MediaMute, StrId::MediaPause}},
    {IDC_LANGUAGE, kLanguageCount, {StrId::LangSystem}},
    {IDC_MUSIC, 3, {StrId::MusicFile, StrId::MusicShuffle, StrId::MusicOrder}},
};

int LanguageIndex(Language language) { return static_cast<int>(language); }

Language LanguageAt(int index) {
    return index >= 0 && index < kLanguageCount ? static_cast<Language>(index) : Language::System;
}

const wchar_t* ComboItemText(const ComboDef& def, size_t i) {
    if (def.id == IDC_LANGUAGE) return i == 0 ? Str(StrId::LangSystem) : LanguageNativeName(LanguageAt(static_cast<int>(i)));
    return Str(def.items[i]);
}

// The sliders (ad level, video level, music volume): trackbar, value label, the setting each one edits and its top.
struct SliderDef {
    int track;
    int value;
    int Settings::*field;
    int max;
};
constexpr SliderDef kSliders[] = {
    {IDC_PERCENT, IDC_PERCENT_VALUE, &Settings::lowerPercent, 90},
    {IDC_MEDIA_PERCENT, IDC_MEDIA_PERCENT_VALUE, &Settings::mediaPercent, 90},
    {IDC_MUSIC_VOLUME, IDC_MUSIC_VOLUME_VALUE, &Settings::adMusicVolume, 100},
};

// The slider a trackbar, value label or (video and music slider) left label belongs to; null for other controls.
const SliderDef* SliderFor(int id) {
    for (const SliderDef& s : kSliders)
        if (s.track == id || s.value == id) return &s;
    if (id == IDC_MEDIA_PERCENT_LABEL) return &kSliders[1];
    return id == IDC_MUSIC_VOLUME_LABEL ? &kSliders[2] : nullptr;
}

const ComboDef* ComboFor(int id) {
    for (const ComboDef& c : kCombos)
        if (c.id == id) return &c;
    return nullptr;
}

// Fills the box from its definition, keeping the selection (texts change with the language).
void FillCombo(const ComboDef& def) {
    HWND combo = Ctl(def.id);
    int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < def.count; ++i) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ComboItemText(def, i)));
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

// Tooltip hints: control id and text. Each is added once and its text refreshed on a language change.
struct TipDef {
    int id;
    StrId text;
};
constexpr TipDef kTips[] = {
    {IDC_HIDETRAY, StrId::OptHideTrayHint},
    {IDC_MEDIA_LABEL, StrId::MediaHint},
    {IDC_MODE_MUSIC, StrId::MusicHint},
};

LRESULT CALLBACK FlatButtonProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK ComboProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR);

HWND Make(const wchar_t* cls, int id, DWORD style) {
    return CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, w.hwnd,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

void Place(int id, int x, int y, int cx, int cy) {
    SetWindowPos(Ctl(id), nullptr, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

void SetText(int id, const wchar_t* text) { SetWindowTextW(Ctl(id), text); }

Gdiplus::Color ToColor(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }

void RoundedPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, float radius) {
    const float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

// Anti-aliased rounded rectangle; the outline sits on the pixel grid so it stays one pixel wide.
void FillRounded(HDC dc, const RECT& rc, float radius, COLORREF fill, COLORREF border, float borderWidth = 1.0f) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float inset = borderWidth / 2;
    Gdiplus::RectF r(rc.left + inset, rc.top + inset, rc.right - rc.left - borderWidth, rc.bottom - rc.top - borderWidth);
    Gdiplus::GraphicsPath path;
    RoundedPath(path, r, radius);
    Gdiplus::SolidBrush brush(ToColor(fill));
    g.FillPath(&brush, &path);
    Gdiplus::Pen pen(ToColor(border), borderWidth);
    g.DrawPath(&pen, &path);
}

void StrokeRounded(HDC dc, const RECT& rc, float radius, COLORREF color, float width) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float inset = width / 2;
    Gdiplus::RectF r(rc.left + inset, rc.top + inset, rc.right - rc.left - width, rc.bottom - rc.top - width);
    Gdiplus::GraphicsPath path;
    RoundedPath(path, r, radius);
    Gdiplus::Pen pen(ToColor(color), width);
    g.DrawPath(&pen, &path);
}

void SetComboHeights();

void MakeFonts() {
    if (w.font) DeleteObject(w.font);
    if (w.headFont) DeleteObject(w.headFont);
    if (w.bigFont) DeleteObject(w.bigFont);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, w.dpi);
    w.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW head = ncm.lfMessageFont;
    head.lfWeight = FW_SEMIBOLD;
    w.headFont = CreateFontIndirectW(&head);
    // The counter number: twice the message font, semibold.
    LOGFONTW big = head;
    big.lfHeight *= 2;
    big.lfWidth = 0;
    w.bigFont = CreateFontIndirectW(&big);
    for (int id = IDC_AUTOSTART; id <= IDC_MODE_MUSIC; ++id)
        if (HWND c = Ctl(id))
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(id == IDC_GROUP_ADS ? w.headFont : id == IDC_ADS_COUNT ? w.bigFont : w.font), TRUE);
    SendMessageW(Ctl(IDC_ADS_CAPTION), WM_SETFONT, reinterpret_cast<WPARAM>(w.font), TRUE);
    SendMessageW(Ctl(IDC_STATUS), WM_SETFONT, reinterpret_cast<WPARAM>(w.font), TRUE);
    SendMessageW(Ctl(IDC_ENABLED), WM_SETFONT, reinterpret_cast<WPARAM>(w.font), TRUE);
    if (w.tip) {
        SendMessageW(w.tip, WM_SETFONT, reinterpret_cast<WPARAM>(w.font), TRUE);
        SendMessageW(w.tip, TTM_SETMAXTIPWIDTH, 0, Scale(320));
    }
    // A new font resets the field height of the boxes.
    SetComboHeights();
}

// The video and language boxes stand alone in their cards and get a taller field; the music source keeps the
// standard height of its row. The box drops the height again when it is moved, re-themed or gets a font, so the
// wanted height is kept as a property and ComboProc puts it back.
constexpr wchar_t kComboHeightProp[] = L"AdMutifyComboHeight";
// Off in the most compact layout, where every pixel of height counts.
bool g_tallCombos = true;

void SetComboHeights() {
    for (int id : {IDC_MEDIA, IDC_LANGUAGE}) {
        HWND c = Ctl(id);
        if (!c) continue;
        TEXTMETRICW tm{};
        HDC dc = GetDC(c);
        HGDIOBJ old = SelectObject(dc, w.font);
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(c, dc);
        // Without the taller field the box gets its standard height back (font height plus two).
        const int height = tm.tmHeight + (g_tallCombos ? Scale(10) : 2);
        SetPropW(c, kComboHeightProp, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(height)));
        SendMessageW(c, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), height);
    }
}

void CreateControls() {
    // Header: the on/off button, right of it the status, the counter and its caption.
    SetWindowSubclass(Make(L"BUTTON", IDC_ENABLED, BS_OWNERDRAW | WS_TABSTOP), FlatButtonProc, 1, 0);
    Make(L"STATIC", IDC_STATUS, SS_CENTER | SS_CENTERIMAGE | SS_ENDELLIPSIS | SS_NOPREFIX);
    Make(L"STATIC", IDC_ADS_COUNT, SS_CENTER | SS_NOPREFIX);
    Make(L"STATIC", IDC_ADS_CAPTION, SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX);
    Make(L"STATIC", IDC_GROUP_ADS, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX);
    Make(L"BUTTON", IDC_MODE_MUTE, BS_AUTORADIOBUTTON | BS_MULTILINE | WS_GROUP | WS_TABSTOP);
    Make(L"BUTTON", IDC_MODE_LOWER, BS_AUTORADIOBUTTON);
    Make(L"BUTTON", IDC_MODE_MUSIC, BS_AUTORADIOBUTTON | BS_MULTILINE);
    Make(TRACKBAR_CLASSW, IDC_PERCENT, TBS_HORZ | TBS_NOTICKS | WS_GROUP | WS_TABSTOP);
    Make(L"STATIC", IDC_PERCENT_VALUE, SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX);
    Make(L"STATIC", IDC_MUSIC_LABEL, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX);
    SetWindowSubclass(Make(L"COMBOBOX", IDC_MUSIC, CBS_DROPDOWNLIST | WS_VSCROLL | WS_GROUP | WS_TABSTOP), ComboProc, 1, 0);
    // SS_NOTIFY for the tooltip with the full path; long paths are shortened in the middle.
    Make(L"STATIC", IDC_MUSIC_PATH, SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY | SS_NOPREFIX | SS_PATHELLIPSIS);
    SetWindowSubclass(Make(L"BUTTON", IDC_MUSIC_PICK, BS_OWNERDRAW | WS_GROUP | WS_TABSTOP), FlatButtonProc, 1, 0);
    Make(L"STATIC", IDC_MUSIC_VOLUME_LABEL, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX);
    Make(TRACKBAR_CLASSW, IDC_MUSIC_VOLUME, TBS_HORZ | TBS_NOTICKS | WS_GROUP | WS_TABSTOP);
    Make(L"STATIC", IDC_MUSIC_VOLUME_VALUE, SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX);
    // SS_NOTIFY lets the label receive the mouse for its tooltip.
    Make(L"STATIC", IDC_MEDIA_LABEL, SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY | SS_NOPREFIX);
    SetWindowSubclass(Make(L"COMBOBOX", IDC_MEDIA, CBS_DROPDOWNLIST | WS_VSCROLL | WS_GROUP | WS_TABSTOP), ComboProc, 1, 0);
    Make(L"STATIC", IDC_MEDIA_PERCENT_LABEL, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX);
    Make(TRACKBAR_CLASSW, IDC_MEDIA_PERCENT, TBS_HORZ | TBS_NOTICKS | WS_GROUP | WS_TABSTOP);
    Make(L"STATIC", IDC_MEDIA_PERCENT_VALUE, SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX);
    Make(L"STATIC", IDC_LANGUAGE_LABEL, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX);
    SetWindowSubclass(Make(L"COMBOBOX", IDC_LANGUAGE, CBS_DROPDOWNLIST | WS_VSCROLL | WS_GROUP | WS_TABSTOP),
                      ComboProc, 1, 0);
    Make(L"BUTTON", IDC_AUTOSTART, BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP);
    Make(L"BUTTON", IDC_HIDETRAY, BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP);
    // Selectable for copying, but no tab stop: it has no label, so keyboard focus there would read as an unnamed field.
    Make(L"EDIT", IDC_VERSION, ES_READONLY | ES_CENTER | ES_AUTOHSCROLL);
    SetWindowSubclass(Make(L"BUTTON", IDC_RESET, BS_OWNERDRAW | WS_TABSTOP), FlatButtonProc, 1, 0);
    SetWindowSubclass(Make(L"BUTTON", IDC_CLOSE, BS_OWNERDRAW | WS_TABSTOP), FlatButtonProc, 1, 0);

    for (const SliderDef& slider : kSliders) {
        HWND trk = Ctl(slider.track);
        SendMessageW(trk, TBM_SETRANGE, TRUE, MAKELONG(0, slider.max));
        SendMessageW(trk, TBM_SETLINESIZE, 0, 5);
        SendMessageW(trk, TBM_SETPAGESIZE, 0, 5);
    }

    w.tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, w.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (w.tip) {
        for (const TipDef& tip : kTips) {
            TTTOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = w.hwnd;
            ti.uId = reinterpret_cast<UINT_PTR>(Ctl(tip.id));
            ti.lpszText = const_cast<wchar_t*>(Str(tip.text));
            SendMessageW(w.tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        }
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = w.hwnd;
        ti.uId = reinterpret_cast<UINT_PTR>(Ctl(IDC_MUSIC_PATH));
        ti.lpszText = const_cast<wchar_t*>(L"");
        SendMessageW(w.tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

void UpdatePercentLabels() {
    for (const SliderDef& slider : kSliders) {
        int pct = static_cast<int>(SendMessageW(Ctl(slider.track), TBM_GETPOS, 0, 0));
        wchar_t buf[32];
        swprintf_s(buf, Str(StrId::PercentValue), pct);
        SetText(slider.value, buf);
    }
}

// A slider works only while its choice is active; its labels turn grey with it.
void UpdateSliderEnabled(const Settings& s) {
    EnableWindow(Ctl(IDC_PERCENT), s.mode == Mode::Lower);
    EnableWindow(Ctl(IDC_MEDIA_PERCENT), s.media == MediaMode::Lower);
    for (int id : {IDC_PERCENT_VALUE, IDC_MEDIA_PERCENT_LABEL, IDC_MEDIA_PERCENT_VALUE}) InvalidateRect(Ctl(id), nullptr, TRUE);
}

bool MusicIsFolder(AdMusic m) { return m == AdMusic::Shuffle || m == AdMusic::Order; }

// The chosen file or folder, or a grey note that none is chosen; the tooltip carries the full path.
void UpdateMusicPath() {
    const Settings& s = CurrentSettings();
    const bool none = s.adMusicPath.empty();
    SetText(IDC_MUSIC_PATH, none ? Str(MusicIsFolder(s.adMusic) ? StrId::MusicNoFolder : StrId::MusicNoFile) : s.adMusicPath.c_str());
    if (w.tip) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = w.hwnd;
        ti.uId = reinterpret_cast<UINT_PTR>(Ctl(IDC_MUSIC_PATH));
        ti.lpszText = const_cast<wchar_t*>(none ? L"" : s.adMusicPath.c_str());
        SendMessageW(w.tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    InvalidateRect(Ctl(IDC_MUSIC_PATH), nullptr, TRUE);
}

void UpdateCounterText() {
    // The number alone, with the thousands separator of the user's locale.
    wchar_t digits[32];
    swprintf_s(digits, L"%llu", static_cast<unsigned long long>(StatsAdsMuted()));
    wchar_t buf[96];
    NUMBERFMTW fmt{};
    fmt.NumDigits = 0;
    fmt.LeadingZero = 1;
    fmt.Grouping = 3;
    wchar_t decimal[8] = L".";
    wchar_t thousand[8] = L",";
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, decimal, static_cast<int>(std::size(decimal)));
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, thousand, static_cast<int>(std::size(thousand)));
    fmt.lpDecimalSep = decimal;
    fmt.lpThousandSep = thousand;
    fmt.NegativeOrder = 1;
    if (GetNumberFormatEx(LOCALE_NAME_USER_DEFAULT, 0, digits, &fmt, buf, static_cast<int>(std::size(buf))) == 0)
        wcscpy_s(buf, digits);
    SetText(IDC_ADS_COUNT, buf);
    InvalidateRect(Ctl(IDC_ADS_COUNT), nullptr, TRUE);
}

// Sets a text and repaints only when it changed: the status is pushed on every state check.
void SetTextIfChanged(int id, const wchar_t* text) {
    wchar_t current[256];
    GetWindowTextW(Ctl(id), current, static_cast<int>(std::size(current)));
    if (wcscmp(current, text) == 0) return;
    SetText(id, text);
    InvalidateRect(Ctl(id), nullptr, TRUE);
}

void UpdateStatusText() {
    const bool enabled = CurrentSettings().enabled;
    SetTextIfChanged(IDC_ENABLED, Str(enabled ? StrId::ToggleOn : StrId::ToggleOff));
    // Before the first state check (review shots) there is no status yet.
    SetTextIfChanged(IDC_STATUS, CurrentStatus().empty() ? Str(StrId::StatusActive) : CurrentStatus().c_str());
}

void ApplyTexts() {
    SetWindowTextW(w.hwnd, Str(StrId::SettingsTitle));
    UpdateCounterText();
    UpdateStatusText();
    SetText(IDC_ADS_CAPTION, Str(StrId::AdsMutedCaption));
    SetText(IDC_AUTOSTART, Str(StrId::OptAutostart));
    SetText(IDC_HIDETRAY, Str(StrId::OptHideTray));
    if (w.tip) {
        for (const TipDef& tip : kTips) {
            TTTOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.hwnd = w.hwnd;
            ti.uId = reinterpret_cast<UINT_PTR>(Ctl(tip.id));
            ti.lpszText = const_cast<wchar_t*>(Str(tip.text));
            SendMessageW(w.tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
        }
    }
    SetText(IDC_GROUP_ADS, Str(StrId::GroupAds));
    SetText(IDC_MODE_MUTE, Str(StrId::ModeMute));
    SetText(IDC_MODE_LOWER, Str(StrId::ModeLower));
    SetText(IDC_PERCENT, Str(StrId::ModeLower));
    SetText(IDC_MODE_MUSIC, Str(StrId::ModeMusic));
    SetText(IDC_MUSIC_LABEL, Str(StrId::MusicSource));
    SetText(IDC_MUSIC_PICK, Str(StrId::MusicPick));
    SetText(IDC_MUSIC_VOLUME, Str(StrId::MusicVolumeLabel));
    SetText(IDC_MUSIC_VOLUME_LABEL, Str(StrId::MusicVolumeLabel));
    UpdateMusicPath();
    SetText(IDC_MEDIA_LABEL, Str(StrId::MediaLabel));
    SetText(IDC_MEDIA_PERCENT, Str(StrId::MediaPercentLabel));
    SetText(IDC_MEDIA_PERCENT_LABEL, Str(StrId::MediaPercentLabel));
    SetText(IDC_LANGUAGE_LABEL, Str(StrId::LanguageLabel));
    for (const ComboDef& def : kCombos) FillCombo(def);
    SetText(IDC_RESET, Str(w.resetArmed ? StrId::ResetConfirmClick : StrId::ResetButton));
    SetText(IDC_CLOSE, Str(StrId::Close));
    wchar_t version[128];
    swprintf_s(version, L"AdMutify %hs \u00B7 " ADMUTIFY_COPYRIGHT_W, ADMUTIFY_VERSION_STR);
    SetText(IDC_VERSION, version);
    UpdatePercentLabels();
}

int TextWidth(HDC dc, HFONT font, int id) {
    wchar_t text[256];
    GetWindowTextW(Ctl(id), text, static_cast<int>(std::size(text)));
    HGDIOBJ old = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    SelectObject(dc, old);
    return size.cx;
}

// Number of lines a label takes when it may wrap inside `width` pixels.
int WrappedLines(HDC dc, HFONT font, int id, int width) {
    wchar_t text[256];
    GetWindowTextW(Ctl(id), text, static_cast<int>(std::size(text)));
    HGDIOBJ old = SelectObject(dc, font);
    RECT rc{0, 0, std::max(width, 1), 0};
    DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_LEFT);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return std::max(1, static_cast<int>((rc.bottom - rc.top + tm.tmHeight - 1) / std::max<LONG>(tm.tmHeight, 1)));
}

// Width of the widest entry of a choice box.
int WidestItem(HDC dc, HFONT font, const ComboDef& def) {
    HGDIOBJ old = SelectObject(dc, font);
    int widest = 0;
    for (size_t i = 0; i < def.count; ++i) {
        const wchar_t* text = ComboItemText(def, i);
        SIZE size{};
        GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
        widest = std::max(widest, static_cast<int>(size.cx));
    }
    SelectObject(dc, old);
    return widest;
}

// The music rows (source, path, volume) only exist while the music radio is selected.
bool MusicRowsShown() { return SendMessageW(Ctl(IDC_MODE_MUSIC), BM_GETCHECK, 0, 0) == BST_CHECKED; }

// The video level row only exists while the choice is "Lower Spotify".
bool MediaLevelShown() {
    return SendMessageW(Ctl(IDC_MEDIA), CB_GETCURSEL, 0, 0) == static_cast<LRESULT>(MediaMode::Lower);
}

enum class Placement { Center, KeepFit, Keep };

// Center: middle of the monitor under the cursor. KeepFit: same top-left, pulled back into the monitor the window is
// on. Keep: same top-left untouched, used after a DPI change where Windows already chose the spot; pulling the window
// back there would move it to the old monitor and flip the scaling again.
// density 0 is the normal layout; 1 and 2 tighten rows and gaps step by step while the window is taller than the
// monitor's work area (high scaling, large text, small screens).
constexpr int kMaxDensity = 2;
void Layout(Placement placement, int density = 0) {
    const bool center = placement == Placement::Center;
    // WM_SETREDRAW TRUE on a hidden window makes it visible (while still being created, before the theme is set).
    const bool pauseRedraw = IsWindowVisible(w.hwnd) != FALSE;
    if (pauseRedraw) SendMessageW(w.hwnd, WM_SETREDRAW, FALSE, 0);
    if (g_tallCombos != (density < kMaxDensity)) {
        g_tallCombos = density < kMaxDensity;
        SetComboHeights();
    }
    HDC dc = GetDC(w.hwnd);
    HGDIOBJ oldFont = SelectObject(dc, w.font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, oldFont);
    // The reset button keeps one width for both of its texts.
    int resetTextW = 0;
    {
        HGDIOBJ oldFont2 = SelectObject(dc, w.font);
        for (StrId id : {StrId::ResetButton, StrId::ResetConfirmClick}) {
            const wchar_t* text = Str(id);
            SIZE size{};
            GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
            resetTextW = std::max(resetTextW, static_cast<int>(size.cx));
        }
        SelectObject(dc, oldFont2);
    }
    const int lowerTextW = TextWidth(dc, w.font, IDC_MODE_LOWER);
    const int closeTextW = TextWidth(dc, w.font, IDC_CLOSE);
    const int labelTextW = TextWidth(dc, w.font, IDC_LANGUAGE_LABEL);
    const int mediaLabelW = TextWidth(dc, w.font, IDC_MEDIA_LABEL);
    const int mediaLevelLabelW = TextWidth(dc, w.font, IDC_MEDIA_PERCENT_LABEL);
    const int musicLabelW = TextWidth(dc, w.font, IDC_MUSIC_LABEL);
    const int musicVolumeLabelW = TextWidth(dc, w.font, IDC_MUSIC_VOLUME_LABEL);
    const int pickTextW = TextWidth(dc, w.font, IDC_MUSIC_PICK);

    const int u = tm.tmHeight;
    const int ave = tm.tmAveCharWidth;
    // Per density: row height, outer margin, card padding and the gaps between cards, in quarters or sixths of u.
    static constexpr int kRowQ[] = {8, 7, 6};
    static constexpr int kMarginQ[] = {5, 4, 3};
    static constexpr int kVpadSixths[] = {4, 3, 2};
    static constexpr int kCardGapQ[] = {3, 2, 1};
    static constexpr int kSectionGapQ[] = {4, 3, 2};
    const int rowH = u * kRowQ[density] / 4;
    const int margin = u * kMarginQ[density] / 4;
    const int pad = u;
    const int vpad = u * kVpadSixths[density] / 6;
    const int cardGap = u * kCardGapQ[density] / 4;
    const int sectionGap = u * kSectionGapQ[density] / 4;
    const int indent = GetSystemMetricsForDpi(SM_CXMENUCHECK, w.dpi) + u / 2;
    const int valW = 6 * ave;
    const int sliderMin = 10 * ave;
    const int pickW = std::max(12 * ave, pickTextW + 2 * u);

    // The window is as wide as the longest label that cannot wrap needs (checkbox labels with their glyph indent, the
    // "lower" radio label with the slider's minimum and value, each label with its choice box), but never narrower
    // than 60 and never wider than 84 average characters. Checkbox labels beyond the cap wrap onto further lines.
    const int mediaItemsW = WidestItem(dc, w.font, kCombos[0]);
    const int languageItemsW = WidestItem(dc, w.font, kCombos[1]);
    const int musicItemsW = WidestItem(dc, w.font, kCombos[2]);
    const int comboExtra = 3 * u;
    // The on/off switch: a rounded track with its state word below, centred beside the three header lines.
    int toggleTextW = 0;
    {
        HGDIOBJ oldFont3 = SelectObject(dc, w.font);
        for (StrId id : {StrId::ToggleOn, StrId::ToggleOff}) {
            const wchar_t* text = Str(id);
            SIZE size{};
            GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
            toggleTextW = std::max(toggleTextW, static_cast<int>(size.cx));
        }
        SelectObject(dc, oldFont3);
    }
    // The status line changes while the window is open, so the widest of its texts counts.
    int statusTextW = 0;
    {
        HGDIOBJ oldFont4 = SelectObject(dc, w.font);
        for (int i = static_cast<int>(StrId::StatusActive); i <= static_cast<int>(StrId::StatusDisabled); ++i) {
            const std::wstring text = FormatPercent(static_cast<StrId>(i), 90);
            SIZE size{};
            GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
            statusTextW = std::max(statusTextW, static_cast<int>(size.cx));
        }
        SelectObject(dc, oldFont4);
    }
    TEXTMETRICW bigTm{};
    {
        HGDIOBJ oldBig = SelectObject(dc, w.bigFont);
        GetTextMetricsW(dc, &bigTm);
        SelectObject(dc, oldBig);
    }
    const int headerH = u + bigTm.tmAscent + u / 6 + u;
    const int trackH = u * 5 / 4;
    const int toggleW = std::max(trackH * 2 + trackH / 4, toggleTextW) + Scale(8);
    const int toggleH = trackH + Scale(8) + u * 5 / 4;
    const int headerTextX = toggleW + u;
    int need = std::max({2 * pad + headerTextX + std::max({TextWidth(dc, w.bigFont, IDC_ADS_COUNT), TextWidth(dc, w.font, IDC_ADS_CAPTION),
                                                 statusTextW}) + u,
                         indent + std::max({TextWidth(dc, w.font, IDC_AUTOSTART), TextWidth(dc, w.font, IDC_HIDETRAY),
                                            TextWidth(dc, w.font, IDC_MODE_MUTE), TextWidth(dc, w.font, IDC_MODE_MUSIC)}) + u,
                         indent + lowerTextW + u + sliderMin + valW,
                         mediaLevelLabelW + u + sliderMin + valW,
                         indent + musicVolumeLabelW + u + sliderMin + valW,
                         indent + musicLabelW + u + musicItemsW + comboExtra,
                         mediaLabelW + u + mediaItemsW + comboExtra,
                         labelTextW + u + languageItemsW + comboExtra});
    const int frame = 2 * pad + 2 * margin;
    const int clientW = std::clamp(need + frame, 60 * ave, 84 * ave);
    const int cardW = clientW - 2 * margin;
    const int x0 = margin + pad;
    const int innerW = cardW - 2 * pad;
    // Row height of a label that may wrap: one line keeps the standard row.
    auto wrapRowH = [&](int id) {
        const int lines = WrappedLines(dc, w.font, id, innerW - indent - u);
        return std::max(rowH, lines * u + u * 2 / 3);
    };
    const int autostartH = wrapRowH(IDC_AUTOSTART);
    const int hideTrayH = wrapRowH(IDC_HIDETRAY);
    const int muteH = wrapRowH(IDC_MODE_MUTE);
    const int musicH = wrapRowH(IDC_MODE_MUSIC);
    ReleaseDC(w.hwnd, dc);
    int y = u / 3;

    // Header, no card: the on/off switch indented like the card content (its track lines up with the radio buttons);
    // the three lines (status, counter number, caption) are centred in the space right of it.
    {
        const int top = y;
        const int toggleX = x0 - (toggleW - (trackH * 2 + trackH / 4)) / 2;
        Place(IDC_ENABLED, toggleX, top + (headerH - toggleH) / 2, toggleW, toggleH);
        const int tx = toggleX + headerTextX;
        const int tw = margin + cardW - pad - tx;
        Place(IDC_STATUS, tx, y, tw, u);
        y += u;
        // Digits do not use the descent: the control ends at the baseline, so number and caption sit close together.
        // It starts below the status line; its internal leading is the gap to it.
        Place(IDC_ADS_COUNT, tx, y, tw, bigTm.tmAscent);
        y += bigTm.tmAscent + u / 6;
        Place(IDC_ADS_CAPTION, tx, y, tw, u);
        y = top + headerH + u / 2;
    }

    // Section heading and card 1: what happens during an ad.
    const int headH = density == kMaxDensity ? u * 5 / 4 : u * 3 / 2;
    Place(IDC_GROUP_ADS, margin + u / 4, y, cardW - u / 4, headH);
    y += headH + u / 3;
    w.cards[0] = {margin, y, margin + cardW, 0};
    y += vpad;
    // Slider rows line up with the ad slider; `off` indents the label of the music rows under their radio.
    const int lowerW = std::min(innerW - sliderMin - valW,
                                std::max({lowerTextW + indent + u, mediaLevelLabelW + u, musicVolumeLabelW + indent + u}));
    auto sliderRow = [&](int labelId, int trackId, int valueId, bool show, int off) {
        Place(labelId, x0 + off, y, lowerW - off, rowH);
        Place(trackId, x0 + lowerW, y, innerW - lowerW - valW, rowH);
        Place(valueId, x0 + innerW - valW, y, valW, rowH);
        for (int id : {labelId, trackId, valueId}) ShowWindow(Ctl(id), show ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (show) y += rowH;
    };
    // Label on the left, choice box on the right of the row at y; the box is centred on the row.
    auto placeCombo = [&](int off, int labelId, int comboId, int labelW, int itemsW, int rowHeight) {
        const int avail = innerW - off;
        const int comboW = std::max({avail * 2 / 5, std::min(avail - labelW - u, 22 * ave), std::min(itemsW + comboExtra, avail - labelW - u)});
        Place(labelId, x0 + off, y, avail - comboW - u / 2, rowHeight);
        Place(comboId, x0 + innerW - comboW, y, comboW, rowH * 5);
        RECT comboRc{};
        GetWindowRect(Ctl(comboId), &comboRc);
        Place(comboId, x0 + innerW - comboW, y + (rowHeight - (comboRc.bottom - comboRc.top)) / 2, comboW, rowH * 5);
    };
    Place(IDC_MODE_MUTE, x0, y, innerW, muteH);
    y += muteH;
    {
        // The level slider exists only while "lower" is chosen.
        const bool show = SendMessageW(Ctl(IDC_MODE_LOWER), BM_GETCHECK, 0, 0) == BST_CHECKED;
        Place(IDC_MODE_LOWER, x0, y, show ? lowerW : innerW, rowH);
        Place(IDC_PERCENT, x0 + lowerW, y, innerW - lowerW - valW, rowH);
        Place(IDC_PERCENT_VALUE, x0 + innerW - valW, y, valW, rowH);
        for (int id : {IDC_PERCENT, IDC_PERCENT_VALUE}) ShowWindow(Ctl(id), show ? SW_SHOWNOACTIVATE : SW_HIDE);
        y += rowH;
    }
    Place(IDC_MODE_MUSIC, x0, y, innerW, musicH);
    y += musicH;
    {
        // Source, path and volume exist only while the music radio is selected.
        const bool show = MusicRowsShown();
        placeCombo(indent, IDC_MUSIC_LABEL, IDC_MUSIC, musicLabelW, musicItemsW, rowH);
        for (int id : {IDC_MUSIC_LABEL, IDC_MUSIC}) ShowWindow(Ctl(id), show ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (show) y += rowH;
        const int buttonW = std::min(pickW, (innerW - indent) / 2);
        Place(IDC_MUSIC_PATH, x0 + indent, y, innerW - indent - buttonW - u / 2, rowH);
        Place(IDC_MUSIC_PICK, x0 + innerW - buttonW, y, buttonW, rowH);
        for (int id : {IDC_MUSIC_PATH, IDC_MUSIC_PICK}) ShowWindow(Ctl(id), show ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (show) y += rowH;
        sliderRow(IDC_MUSIC_VOLUME_LABEL, IDC_MUSIC_VOLUME, IDC_MUSIC_VOLUME_VALUE, show, indent);
    }
    y += vpad;
    w.cards[0].bottom = y;
    y += cardGap;

    // Cards 2 and 3: video reaction and language, each one label and one choice box; the video card adds a level
    // slider below.
    auto comboRow = [&](RECT& card, int labelId, int comboId, int labelW, int itemsW, bool level) {
        card = {margin, y, margin + cardW, 0};
        y += vpad;
        // The taller box keeps a small even gap to the card edge, also in the compact layout.
        placeCombo(0, labelId, comboId, labelW, itemsW, rowH);
        RECT comboRc{};
        GetWindowRect(Ctl(comboId), &comboRc);
        const int boxRowH = std::max(rowH, static_cast<int>(comboRc.bottom - comboRc.top) + u / 3);
        placeCombo(0, labelId, comboId, labelW, itemsW, boxRowH);
        y += boxRowH;
        if (level) sliderRow(IDC_MEDIA_PERCENT_LABEL, IDC_MEDIA_PERCENT, IDC_MEDIA_PERCENT_VALUE, MediaLevelShown(), 0);
        y += vpad;
        card.bottom = y;
    };
    comboRow(w.cards[1], IDC_MEDIA_LABEL, IDC_MEDIA, mediaLabelW, mediaItemsW, true);
    y += cardGap;
    comboRow(w.cards[2], IDC_LANGUAGE_LABEL, IDC_LANGUAGE, labelTextW, languageItemsW, false);
    y += cardGap;

    // Card 4: start options.
    w.cards[3] = {margin, y, margin + cardW, 0};
    y += vpad;
    Place(IDC_AUTOSTART, x0, y, innerW, autostartH);
    y += autostartH;
    Place(IDC_HIDETRAY, x0, y, innerW, hideTrayH);
    y += hideTrayH + vpad;
    w.cards[3].bottom = y;
    y += sectionGap;

    // Footer: version, reset link, close button.
    const int editH = density == kMaxDensity ? u * 5 / 4 : u * 3 / 2;
    Place(IDC_VERSION, margin, y, cardW, editH);
    y += editH + u / 2;
    const int closeW = std::max(12 * ave, closeTextW + 2 * u);
    const int closeH = rowH;
    Place(IDC_RESET, margin, y, std::max(12 * ave, resetTextW + 2 * u), closeH);
    Place(IDC_CLOSE, margin + cardW - closeW, y, closeW, closeH);
    y += closeH + margin;

    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(w.hwnd, GWL_STYLE));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(w.hwnd, GWL_EXSTYLE));
    RECT rc{0, 0, clientW, y};
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, w.dpi);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    RECT cur;
    GetWindowRect(w.hwnd, &cur);
    POINT anchor{cur.left, cur.top};
    if (center) GetCursorPos(&anchor);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(center ? MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST) : MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST),
                    &mi);
    const int workH = static_cast<int>(mi.rcWork.bottom - mi.rcWork.top);
    if (density < kMaxDensity && winH > workH) {
        if (pauseRedraw) SendMessageW(w.hwnd, WM_SETREDRAW, TRUE, 0);
        Layout(placement, density + 1);
        return;
    }
    int x, yy;
    if (center) {
        x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - winW) / 2;
        yy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - winH) / 2;
    } else {
        x = anchor.x;
        yy = anchor.y;
    }
    if (placement != Placement::Keep) {
        if (x + winW > mi.rcWork.right) x = mi.rcWork.right - winW;
        if (yy + winH > mi.rcWork.bottom) yy = mi.rcWork.bottom - winH;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (yy < mi.rcWork.top) yy = mi.rcWork.top;
    } else if (yy + winH > mi.rcWork.bottom) {
        // After a DPI change the window may grow past the bottom: move it up, but only while it stays on the same
        // monitor (a move onto another one would flip the scaling again).
        const int up = std::max(static_cast<int>(mi.rcWork.top), static_cast<int>(mi.rcWork.bottom) - winH);
        RECT moved{x, up, x + winW, up + winH};
        if (MonitorFromRect(&moved, MONITOR_DEFAULTTONEAREST) == MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST)) yy = up;
    }
    SetWindowPos(w.hwnd, nullptr, x, yy, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);
    if (pauseRedraw) SendMessageW(w.hwnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(w.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void LoadFromSettings() {
    w.loading = true;
    const Settings& s = CurrentSettings();
    SendMessageW(Ctl(IDC_AUTOSTART), BM_SETCHECK, IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Ctl(IDC_HIDETRAY), BM_SETCHECK, s.hideTray ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Ctl(IDC_MODE_MUTE), BM_SETCHECK, s.mode == Mode::Mute ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Ctl(IDC_MODE_LOWER), BM_SETCHECK, s.mode == Mode::Lower ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Ctl(IDC_MODE_MUSIC), BM_SETCHECK, s.mode == Mode::Music ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Ctl(IDC_PERCENT), TBM_SETPOS, TRUE, s.lowerPercent);
    SendMessageW(Ctl(IDC_MEDIA_PERCENT), TBM_SETPOS, TRUE, s.mediaPercent);
    SendMessageW(Ctl(IDC_LANGUAGE), CB_SETCURSEL, LanguageIndex(s.language), 0);
    SendMessageW(Ctl(IDC_MEDIA), CB_SETCURSEL, static_cast<WPARAM>(s.media), 0);
    SendMessageW(Ctl(IDC_MUSIC), CB_SETCURSEL, static_cast<WPARAM>(s.adMusic), 0);
    SendMessageW(Ctl(IDC_MUSIC_VOLUME), TBM_SETPOS, TRUE, s.adMusicVolume);
    UpdateMusicPath();
    UpdateSliderEnabled(s);
    UpdatePercentLabels();
    w.loading = false;
}

void RefreshTheme() {
    RefreshThemeCache();
    ApplyTitleBarTheme(w.hwnd);
    COLORREF caption = IsHighContrast() ? kDefaultCaptionColor : ThemeBackground();
    DwmSetWindowAttribute(w.hwnd, kCaptionColor, &caption, sizeof(caption));
    if (w.bg) DeleteObject(w.bg);
    if (w.card) DeleteObject(w.card);
    w.bg = CreateSolidBrush(ThemeBackground());
    w.card = CreateSolidBrush(ThemeCard());
    for (const ComboDef& def : kCombos) ApplyControlTheme(Ctl(def.id), L"DarkMode_CFD");
    SetComboHeights();
    if (w.tip) ApplyControlTheme(w.tip, L"DarkMode_Explorer");
    for (const SliderDef& slider : kSliders) ApplyControlTheme(Ctl(slider.track), L"DarkMode_Explorer");
    RedrawWindow(w.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void DisarmReset() {
    if (!w.resetArmed) return;
    w.resetArmed = false;
    if (!w.hwnd) return;
    KillTimer(w.hwnd, kResetTimer);
    SetText(IDC_RESET, Str(StrId::ResetButton));
    InvalidateRect(Ctl(IDC_RESET), nullptr, FALSE);
}

// The first click arms the button, a second one within a few seconds resets.
void OnReset() {
    if (!w.resetArmed) {
        w.resetArmed = true;
        SetText(IDC_RESET, Str(StrId::ResetConfirmClick));
        InvalidateRect(Ctl(IDC_RESET), nullptr, FALSE);
        SetTimer(w.hwnd, kResetTimer, kResetArmMs, nullptr);
        return;
    }
    DisarmReset();
    if (!SetAutostart(false)) MessageBoxW(w.hwnd, Str(StrId::ErrAutostart), Str(StrId::SettingsTitle), MB_OK | MB_ICONWARNING);
    CommitSettings(Settings{});
    ApplyTexts();
    LoadFromSettings();
    Layout(Placement::KeepFit);
}

// True when the path is what the choice needs: an existing file for File, an existing folder otherwise.
bool MusicPathFits(AdMusic mode, const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) == MusicIsFolder(mode);
}

// The Windows file or folder picker, opened at the current choice (or the Music folder). A folder without playable
// files is refused with a note. Returns the chosen path, empty when cancelled.
std::wstring PickMusicPath(AdMusic mode, const std::wstring& current) {
    const bool folder = MusicIsFolder(mode);
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST);
    dlg->SetOptions(opts);
    dlg->SetTitle(Str(folder ? StrId::MusicPickFolderTitle : StrId::MusicPickFileTitle));
    if (!folder) {
        const std::wstring spec = MusicFilterSpec();
        const COMDLG_FILTERSPEC types[] = {{Str(StrId::MusicFilterName), spec.c_str()}};
        dlg->SetFileTypes(static_cast<UINT>(std::size(types)), types);
    }
    // Start where the current choice is: its folder for a file, the folder itself for a folder.
    std::wstring start = current;
    if (!start.empty() && !(GetFileAttributesW(start.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) {
        const size_t slash = start.find_last_of(L"\\/");
        start = slash == std::wstring::npos ? std::wstring() : start.substr(0, slash);
    }
    IShellItem* item = nullptr;
    if (!start.empty() && GetFileAttributesW(start.c_str()) != INVALID_FILE_ATTRIBUTES &&
        SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        dlg->SetFolder(item);
        item->Release();
    } else if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_Music, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&item)))) {
        dlg->SetFolder(item);
        item->Release();
    }
    std::wstring result;
    for (;;) {
        if (FAILED(dlg->Show(w.hwnd))) break;
        IShellItem* chosen = nullptr;
        PWSTR path = nullptr;
        if (SUCCEEDED(dlg->GetResult(&chosen)) && SUCCEEDED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
            result = path;
        CoTaskMemFree(path);
        if (chosen) chosen->Release();
        if (!folder || result.empty() || !ScanMusicFolder(result, 1).empty()) break;
        wchar_t text[512];
        swprintf_s(text, Str(StrId::ErrMusicEmpty), MusicTypeList(ResolveLanguage(CurrentSettings().language)).c_str());
        MessageBoxW(w.hwnd, text, Str(StrId::SettingsTitle), MB_OK | MB_ICONWARNING);
        result.clear();
    }
    dlg->Release();
    return result;
}

void OnCommand(int id, int code) {
    if (w.loading) return;
    Settings s = CurrentSettings();
    switch (id) {
        case IDC_AUTOSTART:
            if (code == BN_CLICKED) {
                bool want = SendMessageW(Ctl(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (!SetAutostart(want)) {
                    MessageBoxW(w.hwnd, Str(StrId::ErrAutostart), Str(StrId::SettingsTitle), MB_OK | MB_ICONWARNING);
                    SendMessageW(Ctl(id), BM_SETCHECK, IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
                }
            }
            break;
        case IDC_ENABLED:
            if (code == BN_CLICKED) {
                s.enabled = !s.enabled;
                CommitSettings(s);
                UpdateStatusText();
            }
            break;
        case IDC_HIDETRAY:
            if (code == BN_CLICKED) {
                s.hideTray = SendMessageW(Ctl(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                CommitSettings(s);
            }
            break;
        case IDC_MODE_MUTE:
        case IDC_MODE_LOWER:
            if (code == BN_CLICKED) {
                s.mode = id == IDC_MODE_LOWER ? Mode::Lower : Mode::Mute;
                UpdateSliderEnabled(s);
                CommitSettings(s);
                Layout(Placement::KeepFit);
            }
            break;
        case IDC_MODE_MUSIC:
            if (code == BN_CLICKED) {
                // Only switches the mode; the source rows below pick the file or folder. Without one an ad stays
                // muted in silence.
                s.mode = Mode::Music;
                UpdateSliderEnabled(s);
                CommitSettings(s);
                UpdateMusicPath();
                Layout(Placement::KeepFit);
            }
            break;
        case IDC_LANGUAGE:
            if (code == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(Ctl(id), CB_GETCURSEL, 0, 0));
                s.language = LanguageAt(sel);
                CommitSettings(s);
                ApplyTexts();
                Layout(Placement::KeepFit);
            }
            break;
        case IDC_MEDIA:
            if (code == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(Ctl(id), CB_GETCURSEL, 0, 0));
                s.media = sel > 0 && sel < static_cast<int>(ComboFor(id)->count) ? static_cast<MediaMode>(sel) : MediaMode::Off;
                UpdateSliderEnabled(s);
                CommitSettings(s);
                Layout(Placement::KeepFit);
            }
            break;
        case IDC_MUSIC:
            if (code == CBN_SELCHANGE) {
                const int sel = static_cast<int>(SendMessageW(Ctl(id), CB_GETCURSEL, 0, 0));
                const AdMusic want = sel >= 0 && sel < static_cast<int>(ComboFor(id)->count) ? static_cast<AdMusic>(sel) : AdMusic::Shuffle;
                std::wstring path = s.adMusicPath;
                // A source without a fitting file or folder asks for one first; cancelling keeps the old source.
                if (!MusicPathFits(want, path)) path = PickMusicPath(want, path);
                if (path.empty()) {
                    SendMessageW(Ctl(id), CB_SETCURSEL, static_cast<WPARAM>(CurrentSettings().adMusic), 0);
                    break;
                }
                // The picker waits for the user; the tray menu may have changed other settings meanwhile.
                Settings now = CurrentSettings();
                now.adMusic = want;
                now.adMusicPath = path;
                CommitSettings(now);
                UpdateMusicPath();
            }
            break;
        case IDC_MUSIC_PICK:
            if (code == BN_CLICKED) {
                std::wstring path = PickMusicPath(s.adMusic, s.adMusicPath);
                if (path.empty()) break;
                Settings now = CurrentSettings();
                now.adMusicPath = path;
                CommitSettings(now);
                UpdateMusicPath();
            }
            break;
        case IDC_RESET:
            if (code == BN_CLICKED) OnReset();
            break;
        case IDC_CLOSE:
        case IDCANCEL:
        case IDOK: DestroyWindow(w.hwnd); break;
        default: break;
    }
}

void OnScroll(HWND from, int code) {
    if (w.loading) return;
    const SliderDef* slider = nullptr;
    for (const SliderDef& candidate : kSliders)
        if (from == Ctl(candidate.track)) slider = &candidate;
    if (!slider) return;
    int pos = static_cast<int>(SendMessageW(from, TBM_GETPOS, 0, 0));
    int snapped = slider->field == &Settings::adMusicVolume ? ClampVolumePercent(pos) : ClampLowerPercent(pos);
    if (snapped != pos) SendMessageW(from, TBM_SETPOS, TRUE, snapped);
    UpdatePercentLabels();
    // While the thumb is dragged only the label follows; saving and changing the volume wait for the release.
    if (code == TB_THUMBTRACK) return;
    Settings s = CurrentSettings();
    if (s.*slider->field != snapped) {
        s.*slider->field = snapped;
        CommitSettings(s);
    }
}

// Owner-drawn buttons get hover tracking, and Enter activates the focused one instead of the default button.
LRESULT CALLBACK FlatButtonProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_MOUSEMOVE:
            if (w.hotId != GetDlgCtrlID(h)) {
                w.hotId = GetDlgCtrlID(h);
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            if (w.hotId == GetDlgCtrlID(h)) w.hotId = 0;
            InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_KILLFOCUS:
            if (GetDlgCtrlID(h) == IDC_RESET) DisarmReset();
            break;
        case WM_GETDLGCODE:
            if (lParam) {
                const MSG* m = reinterpret_cast<const MSG*>(lParam);
                if (m->message == WM_KEYDOWN && m->wParam == VK_RETURN) return DefSubclassProc(h, msg, wParam, lParam) | DLGC_WANTMESSAGE;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), reinterpret_cast<LPARAM>(h));
                return 0;
            }
            break;
        default: break;
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

// The choices of a box open as a menu in the tray menu style: it appears at once, has rounded corners
// on Windows 11 and matches the rest of the app, unlike the sliding system list.
void ShowComboMenu(HWND combo) {
    const int id = GetDlgCtrlID(combo);
    const ComboDef* def = ComboFor(id);
    if (w.menuOpenId || !def) return;
    RECT rc;
    GetWindowRect(combo, &rc);
    const wchar_t* items[kComboMaxItems];
    for (size_t i = 0; i < def->count; ++i) items[i] = ComboItemText(*def, i);
    const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    const int count = static_cast<int>(def->count);
    HMENU menu = BuildChoiceMenu(items, count, sel, rc.right - rc.left, w.dpi);
    w.menuOpenId = id;
    InvalidateRect(combo, nullptr, FALSE);
    UINT cmd = TrackTrayMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, rc.left,
                             rc.bottom + Scale(4), w.hwnd);
    DestroyTrayMenu(menu);
    w.menuOpenId = 0;
    if (!w.hwnd) return;
    POINT cursor;
    if (cmd == 0 && GetCursorPos(&cursor) && PtInRect(&rc, cursor)) {
        MSG pending;
        while (PeekMessageW(&pending, combo, WM_LBUTTONDOWN, WM_LBUTTONDBLCLK, PM_REMOVE)) {
        }
        w.ignoreClickUntil = GetTickCount64() + 400;
        w.ignoreClickId = id;
    }
    InvalidateRect(combo, nullptr, FALSE);
    const int chosen = static_cast<int>(cmd) - 1;
    if (chosen < 0 || chosen >= count || chosen == sel) return;
    SendMessageW(combo, CB_SETCURSEL, chosen, 0);
    SendMessageW(w.hwnd, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
}

// A closed choice box is drawn as a rounded field.
void PaintCombo(HWND h, HDC target) {
    RECT rc;
    GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    FillRect(dc, &rc, w.card);
    const bool enabled = IsWindowEnabled(h) != FALSE;
    const bool dropped = w.menuOpenId == GetDlgCtrlID(h);
    const bool hot = w.hotId == GetDlgCtrlID(h);
    COLORREF fill = dropped ? ThemeControlPressed() : (hot ? ThemeControlHot() : ThemeControl());
    const float radius = static_cast<float>(Scale(5));
    FillRounded(dc, rc, radius, fill, ThemeControlBorder());
    const bool focus = GetFocus() == h && !(SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    if (focus) StrokeRounded(dc, rc, radius, ThemeText(), static_cast<float>(Scale(2)));

    const int padX = Scale(11);
    const int chevW = Scale(10);
    wchar_t text[256] = L"";
    int sel = static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && SendMessageW(h, CB_GETLBTEXTLEN, sel, 0) < static_cast<LRESULT>(std::size(text)))
        SendMessageW(h, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text));
    HGDIOBJ oldFont = SelectObject(dc, w.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? ThemeText() : ThemeDisabledText());
    RECT tr{rc.left + padX, rc.top, rc.right - padX - chevW - Scale(6), rc.bottom};
    DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);

    {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(ToColor(enabled ? ThemeDisabledText() : ThemeControlBorder()), static_cast<float>(Scale(1)) * 1.3f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        const float cx = static_cast<float>(rc.right - padX - chevW / 2);
        const float cy = static_cast<float>(rc.top + rc.bottom) / 2.0f;
        const float half = chevW / 2.0f;
        const float drop = half / 2.0f;
        Gdiplus::PointF pts[3] = {{cx - half, cy - drop}, {cx, cy + drop}, {cx + half, cy - drop}};
        g.DrawLines(&pen, pts, 3);
    }

    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

LRESULT CALLBACK ComboProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            SetFocus(h);
            // Only the box that just closed its menu swallows the click; another box opens normally.
            if (GetTickCount64() < w.ignoreClickUntil && GetDlgCtrlID(h) == w.ignoreClickId) {
                w.ignoreClickUntil = 0;
                return 0;
            }
            ShowComboMenu(h);
            return 0;
        case WM_KEYDOWN:
            // The arrow and paging keys would step the selection one by one, and every step is a choice (a file
            // picker, a language switch): they open the menu instead, like F4.
            switch (wParam) {
                case VK_F4:
                case VK_UP:
                case VK_DOWN:
                case VK_HOME:
                case VK_END:
                case VK_PRIOR:
                case VK_NEXT: ShowComboMenu(h); return 0;
                default: break;
            }
            break;
        case WM_CHAR:
        case WM_MOUSEWHEEL:
            // Typing a letter or turning the wheel would also step the selection.
            return 0;
        case WM_SYSKEYDOWN:
            if (wParam == VK_DOWN || wParam == VK_UP) {
                ShowComboMenu(h);
                return 0;
            }
            break;
        case CB_SHOWDROPDOWN:
            // Accessibility clients expand the box this way; open the menu once this call has returned.
            if (wParam) PostMessageW(h, kMsgOpenCombo, 0, 0);
            return TRUE;
        case kMsgOpenCombo: ShowComboMenu(h); return 0;
        case WM_WINDOWPOSCHANGED:
        case WM_SETFONT:
        case WM_THEMECHANGED: {
            const LRESULT r = DefSubclassProc(h, msg, wParam, lParam);
            const int want = static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(h, kComboHeightProp)));
            if (want > 0 && SendMessageW(h, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0) != want)
                SendMessageW(h, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), want);
            return r;
        }
        case WM_NCDESTROY: RemovePropW(h, kComboHeightProp); break;
        default: break;
    }
    if (IsHighContrast()) return DefSubclassProc(h, msg, wParam, lParam);
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = wParam ? reinterpret_cast<HDC>(wParam) : BeginPaint(h, &ps);
            PaintCombo(h, dc);
            if (!wParam) EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE:
            if (w.hotId != GetDlgCtrlID(h)) {
                w.hotId = GetDlgCtrlID(h);
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            if (w.hotId == GetDlgCtrlID(h)) w.hotId = 0;
            InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE: {
            LRESULT r = DefSubclassProc(h, msg, wParam, lParam);
            InvalidateRect(h, nullptr, FALSE);
            return r;
        }
        default: break;
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

LRESULT OnCustomDraw(NMCUSTOMDRAW* nm) {
    if (IsHighContrast() || nm->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    HWND h = nm->hdr.hwndFrom;
    wchar_t cls[16];
    GetClassNameW(h, cls, static_cast<int>(std::size(cls)));
    if (_wcsicmp(cls, L"Button") != 0) return CDRF_DODEFAULT;
    LONG type = GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK;
    HDC dc = nm->hdc;
    RECT rc = nm->rc;
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = SelectObject(dc, font);
    wchar_t text[256];
    GetWindowTextW(h, text, static_cast<int>(std::size(text)));
    const bool disabled = (nm->uItemState & CDIS_DISABLED) != 0;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? ThemeDisabledText() : ThemeText());
    LRESULT result = CDRF_DODEFAULT;

    if (type == BS_AUTOCHECKBOX || type == BS_AUTORADIOBUTTON) {
        FillRect(dc, &rc, w.card);
        const bool radio = type == BS_AUTORADIOBUTTON;
        const bool checked = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        int state = 1;
        if (disabled) state = 4;
        else if (nm->uItemState & CDIS_SELECTED) state = 3;
        else if (nm->uItemState & CDIS_HOT) state = 2;
        if (checked) state += 4;
        HTHEME theme = IsDarkTheme() ? OpenThemeData(h, L"DarkMode_Explorer::Button") : nullptr;
        if (!theme) theme = OpenThemeData(h, L"Button");
        const int part = radio ? BP_RADIOBUTTON : BP_CHECKBOX;
        SIZE box{16, 16};
        if (theme) GetThemePartSize(theme, dc, part, state, nullptr, TS_DRAW, &box);
        RECT br{rc.left, rc.top + (rc.bottom - rc.top - box.cy) / 2, rc.left + box.cx, 0};
        br.bottom = br.top + box.cy;
        if (theme) {
            DrawThemeBackground(theme, dc, part, state, &br, nullptr);
            CloseThemeData(theme);
        }
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        RECT tr{br.right + tm.tmHeight / 2, rc.top, rc.right, rc.bottom};
        // A label that does not fit on one line wraps; the lines stay vertically centred in the row.
        const UINT wrap = (GetWindowLongW(h, GWL_STYLE) & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE;
        RECT body = tr;
        DrawTextW(dc, text, -1, &body, wrap | DT_LEFT | DT_NOPREFIX | DT_CALCRECT);
        const int textH = body.bottom - body.top;
        tr.top = rc.top + std::max<LONG>(0, (rc.bottom - rc.top - textH) / 2);
        tr.bottom = tr.top + textH;
        DrawTextW(dc, text, -1, &tr, wrap | DT_LEFT | DT_NOPREFIX);
        if ((nm->uItemState & CDIS_FOCUS) && !(SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            RECT fr = tr;
            DrawTextW(dc, text, -1, &fr, wrap | DT_LEFT | DT_NOPREFIX | DT_CALCRECT);
            fr.left -= 2;
            fr.right += 2;
            DrawFocusRect(dc, &fr);
        }
        result = CDRF_SKIPDEFAULT;
    }
    SelectObject(dc, oldFont);
    return result;
}

bool ShowFocus(const DRAWITEMSTRUCT* di) { return (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT); }

// Drawn off screen first: GDI+ output and GDI text on the same window DC can land out of order.
// The close button is the accent button; reset uses the standard button look, red while it waits for the second click.
void DrawButton(const DRAWITEMSTRUCT* di, bool accent) {
    const int cx = di->rcItem.right - di->rcItem.left;
    const int cy = di->rcItem.bottom - di->rcItem.top;
    HDC dc = CreateCompatibleDC(di->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(di->hDC, cx, cy);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT all{0, 0, cx, cy};
    // The pick button sits on a card, the footer buttons on the window background.
    FillRect(dc, &all, di->CtlID == IDC_MUSIC_PICK ? w.card : w.bg);
    const bool pressed = (di->itemState & ODS_SELECTED) != 0;
    const bool hot = w.hotId == static_cast<int>(di->CtlID);
    const bool critical = di->CtlID == IDC_RESET && w.resetArmed;
    COLORREF fill;
    COLORREF border;
    if (critical) {
        fill = ThemeCritical();
        border = fill;
    } else if (accent) {
        fill = pressed ? ThemeAccentPressed() : (hot ? ThemeAccentHot() : ThemeAccent());
        border = fill;
    } else {
        fill = pressed ? ThemeControlPressed() : (hot ? ThemeControlHot() : ThemeControl());
        border = ThemeControlBorder();
    }
    const int ring = Scale(3);
    RECT body = all;
    InflateRect(&body, -ring, -ring);
    const float radius = static_cast<float>(Scale(5));
    FillRounded(dc, body, radius, fill, border);
    if (ShowFocus(di)) StrokeRounded(dc, all, radius + ring, ThemeText(), static_cast<float>(Scale(2)));
    HGDIOBJ oldFont = SelectObject(dc, w.font);
    wchar_t text[256];
    GetWindowTextW(di->hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, critical ? ThemeCriticalText() : (accent ? ThemeAccentText() : ThemeText()));
    DrawTextW(dc, text, -1, &body, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    BitBlt(di->hDC, di->rcItem.left, di->rcItem.top, cx, cy, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// The on/off switch: a pill track (accent fill with a light knob on the right while on, outline with the knob on the
// left while off) and the state word below it. Drawn off screen like the buttons.
void DrawToggle(const DRAWITEMSTRUCT* di) {
    const int cx = di->rcItem.right - di->rcItem.left;
    const int cy = di->rcItem.bottom - di->rcItem.top;
    HDC dc = CreateCompatibleDC(di->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(di->hDC, cx, cy);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT all{0, 0, cx, cy};
    FillRect(dc, &all, w.bg);
    const bool on = CurrentSettings().enabled;
    const bool pressed = (di->itemState & ODS_SELECTED) != 0;
    const bool hot = w.hotId == static_cast<int>(di->CtlID);
    HDC measure = dc;
    HGDIOBJ oldFont = SelectObject(measure, w.font);
    TEXTMETRICW tm{};
    GetTextMetricsW(measure, &tm);
    const int u = tm.tmHeight;
    const int ring = Scale(4);
    const int trackH = u * 5 / 4;
    const int trackW = trackH * 2 + trackH / 4;
    RECT track{(cx - trackW) / 2, ring, (cx - trackW) / 2 + trackW, ring + trackH};
    const float radius = trackH / 2.0f;
    {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float border = static_cast<float>(Scale(1));
        Gdiplus::RectF r(track.left + border / 2, track.top + border / 2, trackW - border, trackH - border);
        Gdiplus::GraphicsPath path;
        RoundedPath(path, r, radius - border / 2);
        COLORREF knob;
        if (on) {
            const COLORREF fill = pressed ? ThemeAccentPressed() : (hot ? ThemeAccentHot() : ThemeAccent());
            Gdiplus::SolidBrush brush(ToColor(fill));
            g.FillPath(&brush, &path);
            knob = ThemeAccentText();
        } else {
            const COLORREF fill = pressed ? ThemeControlPressed() : (hot ? ThemeControlHot() : ThemeControl());
            Gdiplus::SolidBrush brush(ToColor(fill));
            g.FillPath(&brush, &path);
            Gdiplus::Pen pen(ToColor(ThemeText()), border);
            g.DrawPath(&pen, &path);
            knob = ThemeText();
        }
        // The knob grows a little under the mouse, like the Windows switch.
        const float knobD = trackH * (hot && !pressed ? 0.62f : 0.54f);
        const float knobY = track.top + (trackH - knobD) / 2;
        const float gap = (trackH - knobD) / 2;
        const float knobX = on ? track.right - gap - knobD : track.left + gap;
        Gdiplus::SolidBrush knobBrush(ToColor(knob));
        g.FillEllipse(&knobBrush, knobX, knobY, knobD, knobD);
    }
    if (ShowFocus(di)) {
        RECT focus = track;
        InflateRect(&focus, ring - Scale(1), ring - Scale(1));
        StrokeRounded(dc, focus, radius + ring, ThemeText(), static_cast<float>(Scale(2)));
    }
    wchar_t text[64];
    GetWindowTextW(di->hwndItem, text, static_cast<int>(std::size(text)));
    RECT label{0, track.bottom + ring, cx, cy};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ThemeText());
    DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(measure, oldFont);
    BitBlt(di->hDC, di->rcItem.left, di->rcItem.top, cx, cy, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    FillRect(mem, &rc, w.bg);
    const float radius = static_cast<float>(Scale(8));
    for (const RECT& card : w.cards)
        if (card.bottom > card.top) FillRounded(mem, card, radius, ThemeCard(), ThemeCardBorder());
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

bool OnCard(int id) { return id != IDC_GROUP_ADS && id != IDC_VERSION && id != IDC_ADS_COUNT &&
                                                   id != IDC_ADS_CAPTION && id != IDC_STATUS; }

LRESULT OnCtlColor(HDC dc, HWND ctl) {
    int id = GetDlgCtrlID(ctl);
    COLORREF text = ThemeText();
    if (const SliderDef* slider = SliderFor(id); slider && id != slider->track && !IsWindowEnabled(Ctl(slider->track)))
        text = ThemeDisabledText();
    if (id == IDC_MUSIC_PATH && CurrentSettings().adMusicPath.empty()) text = ThemeDisabledText();
    if (id == IDC_ADS_COUNT) text = IsHighContrast() ? ThemeText() : ThemeAccent();
    if (id == IDC_ADS_CAPTION) text = ThemeDisabledText();
    const bool card = OnCard(id);
    SetTextColor(dc, text);
    SetBkColor(dc, card ? ThemeCard() : ThemeBackground());
    return reinterpret_cast<LRESULT>(card ? w.card : w.bg);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            w.hwnd = hwnd;
            w.dpi = GetDpiForWindow(hwnd);
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&w.gdiplus, &input, nullptr);
            w.bg = CreateSolidBrush(ThemeBackground());
            w.card = CreateSolidBrush(ThemeCard());
            CreateControls();
            MakeFonts();
            ApplyTexts();
            LoadFromSettings();
            RefreshTheme();
            Layout(Placement::Center);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: OnPaint(hwnd); return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORDLG: return OnCtlColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->code == NM_CUSTOMDRAW) return OnCustomDraw(reinterpret_cast<NMCUSTOMDRAW*>(lParam));
            break;
        }
        case WM_DRAWITEM: {
            auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (TrayMenuDraw(di)) return TRUE;
            if (di->CtlID == IDC_ENABLED) {
                DrawToggle(di);
                return TRUE;
            }
            if (di->CtlID == IDC_RESET || di->CtlID == IDC_CLOSE || di->CtlID == IDC_MUSIC_PICK) {
                DrawButton(di, di->CtlID == IDC_CLOSE);
                return TRUE;
            }
            break;
        }
        case WM_MEASUREITEM:
            if (TrayMenuMeasure(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) return TRUE;
            break;
        case WM_COMMAND: OnCommand(LOWORD(wParam), HIWORD(wParam)); return 0;
        case WM_HSCROLL: OnScroll(reinterpret_cast<HWND>(lParam), LOWORD(wParam)); return 0;
        case DM_GETDEFID: return MAKELRESULT(IDC_CLOSE, DC_HASDEFID);
        case WM_DPICHANGED: {
            w.dpi = HIWORD(wParam);
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            MakeFonts();
            Layout(Placement::Keep);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETHIGHCONTRAST ||
                (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0))
                RefreshTheme();
            else if (wParam == SPI_SETNONCLIENTMETRICS) {
                // Text size or system font changed.
                MakeFonts();
                Layout(Placement::KeepFit);
            }
            return 0;
        case WM_THEMECHANGED:
            MakeFonts();
            Layout(Placement::KeepFit);
            return 0;
        case WM_SYSCOLORCHANGE: RefreshTheme(); return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) DisarmReset();
            break;
        case WM_TIMER:
            if (wParam == kResetTimer) DisarmReset();
            return 0;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kResetTimer);
            w.resetArmed = false;
            w.tip = nullptr;
            w.hwnd = nullptr;
            w.hotId = 0;
            return 0;
        case WM_NCDESTROY:
            // Fonts and brushes go last, after the child windows that still use them are gone.
            if (w.font) DeleteObject(w.font);
            if (w.headFont) DeleteObject(w.headFont);
            if (w.bigFont) DeleteObject(w.bigFont);
            if (w.bg) DeleteObject(w.bg);
            if (w.card) DeleteObject(w.card);
            w.font = w.headFont = w.bigFont = nullptr;
            w.bg = w.card = nullptr;
            if (w.gdiplus) Gdiplus::GdiplusShutdown(w.gdiplus);
            w.gdiplus = 0;
            break;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

HWND SettingsWindowHandle() { return w.hwnd; }

void SettingsWindowUpdateCounter() {
    if (w.hwnd) UpdateCounterText();
}

void SettingsWindowUpdateStatus() {
    if (!w.hwnd) return;
    UpdateStatusText();
    // The tray menu can hide the icon while the window is open.
    SendMessageW(Ctl(IDC_HIDETRAY), BM_SETCHECK, CurrentSettings().hideTray ? BST_CHECKED : BST_UNCHECKED, 0);
}

void OpenSettingsWindow() {
    if (w.hwnd) {
        if (IsIconic(w.hwnd)) ShowWindow(w.hwnd, SW_RESTORE);
        SetForegroundWindow(w.hwnd);
        return;
    }
    if (!w.classRegistered) {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        w.classRegistered = true;
    }
    // Created on the monitor of the mouse pointer so the window starts with that monitor's DPI.
    POINT pt{};
    GetCursorPos(&pt);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    int cx = (mi.rcWork.left + mi.rcWork.right) / 2;
    int cy = (mi.rcWork.top + mi.rcWork.bottom) / 2;
    HWND hwnd = CreateWindowExW(0, kClassName, L"", WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, cx, cy, 100, 100,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

bool SettingsWindowPreTranslate(MSG* msg) {
    if (!w.hwnd) return false;
    // Esc ends the confirm state first and leaves the window open.
    if (w.resetArmed && msg->message == WM_KEYDOWN && msg->wParam == VK_ESCAPE && (msg->hwnd == w.hwnd || IsChild(w.hwnd, msg->hwnd))) {
        DisarmReset();
        return true;
    }
    return IsDialogMessageW(w.hwnd, msg);
}

void SettingsWindowArmReset() {
    if (w.hwnd && !w.resetArmed) OnReset();
}
