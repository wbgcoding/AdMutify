#include "tray.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>

#include <algorithm>

#include "app_messages.h"
#include "resource.h"
#include "strings.h"
#include "theme.h"

namespace {

using SetPreferredAppModeFn = int(WINAPI*)(int);
using FlushMenuThemesFn = void(WINAPI*)();

constexpr UINT kTrayId = 1;

int g_menuMode = 0;

// Owner-drawn items so the menu is exactly as wide as its longest entry.
struct MenuLook {
    UINT dpi = 96;
    HFONT font = nullptr;
    // Outer width the menu should at least have (choice menus line up with their field).
    int minWidth = 0;
};
MenuLook g_look;

constexpr int kSeparatorData = -1;

int MenuScale(int px) { return MulDiv(px, static_cast<int>(g_look.dpi), 96); }

bool MenuDark() {
    if (IsHighContrast()) return false;
    if (g_menuMode == 2) return true;
    return g_menuMode == 1 && SystemUsesDarkApps();
}

COLORREF MenuBack() {
    if (IsHighContrast()) return GetSysColor(COLOR_MENU);
    return MenuDark() ? RGB(0x2C, 0x2C, 0x2C) : RGB(0xF9, 0xF9, 0xF9);
}
COLORREF MenuHot() {
    if (IsHighContrast()) return GetSysColor(COLOR_HIGHLIGHT);
    return MenuDark() ? RGB(0x3D, 0x3D, 0x3D) : RGB(0xE9, 0xE9, 0xE9);
}
COLORREF MenuText(bool disabled, bool hot) {
    if (IsHighContrast()) return GetSysColor(disabled ? COLOR_GRAYTEXT : (hot ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
    if (disabled) return MenuDark() ? RGB(0x9D, 0x9D, 0x9D) : RGB(0x6E, 0x6E, 0x6E);
    return MenuDark() ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x1A, 0x1A, 0x1A);
}
COLORREF MenuLine() {
    if (IsHighContrast()) return GetSysColor(COLOR_GRAYTEXT);
    return MenuDark() ? RGB(0x48, 0x48, 0x48) : RGB(0xE0, 0xE0, 0xE0);
}

void PrepareMenuLook(UINT dpi, int minWidth) {
    if (!dpi) {
        POINT pt{};
        GetCursorPos(&pt);
        UINT dy = 96;
        dpi = 96;
        if (FAILED(GetDpiForMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpi, &dy))) dpi = 96;
    }
    g_look.dpi = dpi;
    g_look.minWidth = minWidth;
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g_look.dpi);
    if (g_look.font) DeleteObject(g_look.font);
    g_look.font = CreateFontIndirectW(&ncm.lfMenuFont);
}

// Left column holds the check mark, so every label starts at the same x.
int MenuTextLeft() { return MenuScale(32); }
int MenuTextRight() { return MenuScale(20); }

void AddItem(HMENU menu, UINT id, const wchar_t* text, UINT state) {
    MENUITEMINFOW mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID | MIIM_STATE | MIIM_DATA;
    mi.fType = MFT_OWNERDRAW;
    mi.fState = state;
    mi.wID = id;
    mi.dwTypeData = const_cast<wchar_t*>(text);
    mi.dwItemData = reinterpret_cast<ULONG_PTR>(menu);
    InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &mi);
}

void AddSeparator(HMENU menu) {
    MENUITEMINFOW mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_FTYPE | MIIM_DATA;
    mi.fType = MFT_SEPARATOR | MFT_OWNERDRAW;
    mi.dwItemData = static_cast<ULONG_PTR>(kSeparatorData);
    InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &mi);
}

// Windows 11 keeps owner-drawn menus square unless asked. The menu window lives in the kernel, so a
// window hook never sees it; the popup-start event hands over its handle while the menu loop runs.
constexpr DWORD kCornerPreference = 33;
constexpr DWORD kCornerRound = 2;

void CALLBACK MenuPopupEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (!hwnd || idObject != OBJID_CLIENT) return;
    DWORD round = kCornerRound;
    DwmSetWindowAttribute(hwnd, kCornerPreference, &round, sizeof(round));
}

std::wstring ItemText(HMENU menu, UINT id) {
    wchar_t buf[256] = L"";
    GetMenuStringW(menu, id, buf, static_cast<int>(std::size(buf)), MF_BYCOMMAND);
    return buf;
}

}  // namespace

bool TrayMenuMeasure(MEASUREITEMSTRUCT* mi) {
    if (!mi || mi->CtlType != ODT_MENU) return false;
    if (static_cast<int>(mi->itemData) == kSeparatorData) {
        mi->itemWidth = 0;
        mi->itemHeight = MenuScale(9);
        return true;
    }
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, g_look.font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    std::wstring text = ItemText(reinterpret_cast<HMENU>(mi->itemData), mi->itemID);
    SIZE sz{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(dc, old);
    DeleteDC(dc);
    // The system adds the check mark width to owner-drawn menu items; take it back out.
    const int systemCheck = GetSystemMetricsForDpi(SM_CXMENUCHECK, g_look.dpi);
    const int width = std::max(MenuTextLeft() + static_cast<int>(sz.cx) + MenuTextRight(), g_look.minWidth - MenuScale(2)) - systemCheck;
    mi->itemWidth = static_cast<UINT>(std::max(width, 0));
    mi->itemHeight = static_cast<UINT>(tm.tmHeight + MenuScale(14));
    return true;
}

bool TrayMenuDraw(const DRAWITEMSTRUCT* di) {
    if (!di || di->CtlType != ODT_MENU) return false;
    HDC dc = di->hDC;
    RECT rc = di->rcItem;
    HBRUSH back = CreateSolidBrush(MenuBack());
    FillRect(dc, &rc, back);
    DeleteObject(back);
    if (static_cast<int>(di->itemData) == kSeparatorData) {
        const int y = (rc.top + rc.bottom) / 2;
        RECT line{rc.left + MenuScale(4), y, rc.right - MenuScale(4), y + 1};
        HBRUSH b = CreateSolidBrush(MenuLine());
        FillRect(dc, &line, b);
        DeleteObject(b);
        return true;
    }
    const bool disabled = (di->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    const bool hot = !disabled && (di->itemState & ODS_SELECTED) != 0;
    if (hot) {
        RECT hr{rc.left + MenuScale(4), rc.top + MenuScale(2), rc.right - MenuScale(4), rc.bottom - MenuScale(2)};
        HBRUSH b = CreateSolidBrush(MenuHot());
        HGDIOBJ oldBrush = SelectObject(dc, b);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        const int r = MenuScale(8);
        RoundRect(dc, hr.left, hr.top, hr.right + 1, hr.bottom + 1, r, r);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(b);
    }
    const COLORREF fg = MenuText(disabled, hot);
    if (di->itemState & ODS_CHECKED) {
        // Check mark drawn as two strokes in the left column.
        const int cx = rc.left + MenuTextLeft() / 2;
        const int cy = (rc.top + rc.bottom) / 2;
        const int s = MenuScale(4);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, MenuScale(2) * 3 / 4), fg);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        POINT pts[3] = {{cx - s - s / 2, cy}, {cx - s / 2, cy + s}, {cx + s + s / 2, cy - s}};
        Polyline(dc, pts, 3);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    std::wstring text = ItemText(reinterpret_cast<HMENU>(di->itemData), di->itemID);
    HGDIOBJ oldFont = SelectObject(dc, g_look.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    RECT tr{rc.left + MenuTextLeft(), rc.top, rc.right - MenuScale(8), rc.bottom};
    DrawTextW(dc, text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    return true;
}

bool ApplyMenuAppMode(int mode) {
    g_menuMode = mode;
    // uxtheme is linked, so it is already loaded; no extra module reference per call.
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) return false;
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    if (!setMode || !flush) return false;
    setMode(mode);
    flush();
    return true;
}

void FinishMenu(HMENU menu) {
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_STYLE | MIM_BACKGROUND;
    info.dwStyle = MNS_NOCHECK;
    info.hbrBack = CreateSolidBrush(MenuBack());
    SetMenuInfo(menu, &info);
}

HMENU BuildTrayMenu(bool enabled, const std::wstring& status) {
    PrepareMenuLook(0, 0);
    HMENU menu = CreatePopupMenu();
    AddItem(menu, IDM_TRAY_STATUS, status.c_str(), MFS_GRAYED);
    AddSeparator(menu);
    AddItem(menu, IDM_TRAY_ENABLED, Str(StrId::MenuEnabled), enabled ? MFS_CHECKED : MFS_UNCHECKED);
    AddItem(menu, IDM_TRAY_SETTINGS, Str(StrId::MenuSettings), 0);
    AddSeparator(menu);
    AddItem(menu, IDM_TRAY_EXIT, Str(StrId::MenuExit), 0);
    FinishMenu(menu);
    return menu;
}

HMENU BuildChoiceMenu(const wchar_t* const* items, int count, int checked, int minWidth, UINT dpi) {
    PrepareMenuLook(dpi, minWidth);
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < count; ++i)
        AddItem(menu, static_cast<UINT>(i + 1), items[i], i == checked ? MFS_CHECKED : MFS_UNCHECKED);
    FinishMenu(menu);
    return menu;
}

UINT TrackTrayMenu(HMENU menu, UINT flags, int x, int y, HWND owner) {
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_MENUPOPUPSTART, EVENT_SYSTEM_MENUPOPUPSTART, nullptr, &MenuPopupEvent,
                                         GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
    UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, flags, x, y, owner, nullptr));
    if (hook) UnhookWinEvent(hook);
    return cmd;
}

void DestroyTrayMenu(HMENU menu) {
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    if (GetMenuInfo(menu, &info) && info.hbrBack) DeleteObject(info.hbrBack);
    DestroyMenu(menu);
}

void Tray::Init(HWND hwnd, HINSTANCE inst) {
    hwnd_ = hwnd;
    inst_ = inst;
    ReloadIcons();
}

void Tray::ReloadIcons() {
    // The "on" state uses the app icon itself.
    const int ids[3] = {IDI_APP, IDI_TRAY_MUTED, IDI_TRAY_OFF};
    for (int i = 0; i < 3; ++i) {
        HICON icon = nullptr;
        if (SUCCEEDED(LoadIconMetric(inst_, MAKEINTRESOURCEW(ids[i]), LIM_SMALL, &icon))) {
            if (icons_[i]) DestroyIcon(icons_[i]);
            icons_[i] = icon;
        }
    }
    if (visible_) {
        NOTIFYICONDATAW nid{};
        FillData(nid);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

HICON Tray::IconFor(TrayIconKind kind) const { return icons_[static_cast<int>(kind)]; }

void Tray::FillData(NOTIFYICONDATAW& nid) const {
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = IconFor(kind_);
    wcsncpy_s(nid.szTip, tooltip_.c_str(), _TRUNCATE);
}

bool Tray::Add() {
    NOTIFYICONDATAW nid{};
    FillData(nid);
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return true;
}

void Tray::Show() {
    wantVisible_ = true;
    if (!visible_) visible_ = Add();
}

void Tray::Hide() {
    // Hide runs only on the way out; the menu font is rebuilt on every menu, so dropping it here is safe.
    if (g_look.font) {
        DeleteObject(g_look.font);
        g_look.font = nullptr;
    }
    if (!wantVisible_ && !visible_) return;
    wantVisible_ = false;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    visible_ = false;
}

void Tray::OnTaskbarCreated() {
    ReloadIcons();
    if (!wantVisible_) return;
    // The message can also arrive while the shell still holds the icon, so remove it before adding again.
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    visible_ = Add();
}

void Tray::Update(TrayIconKind kind, const std::wstring& tooltip) {
    // Every modify call is a round trip to the shell, so unchanged state is not sent again.
    if (kind == kind_ && tooltip == tooltip_) return;
    kind_ = kind;
    tooltip_ = tooltip;
    if (!visible_) return;
    NOTIFYICONDATAW nid{};
    FillData(nid);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

UINT Tray::ShowMenu(POINT pt, bool enabled, const std::wstring& status) {
    HMENU menu = BuildTrayMenu(enabled, status);
    SetForegroundWindow(hwnd_);
    UINT cmd = TrackTrayMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN, pt.x, pt.y, hwnd_);
    DestroyTrayMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    return cmd;
}
