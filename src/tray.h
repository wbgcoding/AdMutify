#pragma once

#include <windows.h>

#include <string>

enum class TrayIconKind { On, Muted, Off };

enum TrayCommand : UINT {
    IDM_TRAY_ENABLED = 1001,
    IDM_TRAY_SETTINGS = 1002,
    IDM_TRAY_EXIT = 1003,
    IDM_TRAY_STATUS = 1004,
};

// Dark menu support through undocumented uxtheme ordinals; mode 1 = allow dark, 2 = force dark, 3 = force light.
// Returns false when the functions are not available.
bool ApplyMenuAppMode(int mode);

// Builds the popup menu (status line, enabled toggle, settings, exit); free it with DestroyTrayMenu.
HMENU BuildTrayMenu(bool enabled, const std::wstring& status);
// Same look as the tray menu: one checkable item per entry, command ids 1..count. minWidth in pixels,
// dpi 0 = the monitor under the cursor.
HMENU BuildChoiceMenu(const wchar_t* const* items, int count, int checked, int minWidth, UINT dpi);
void DestroyTrayMenu(HMENU menu);
// TrackPopupMenuEx with rounded Windows 11 corners on the menu window.
UINT TrackTrayMenu(HMENU menu, UINT flags, int x, int y, HWND owner);
// WM_MEASUREITEM / WM_DRAWITEM handlers for the owner-drawn menu items; false when the item is not a menu item.
bool TrayMenuMeasure(MEASUREITEMSTRUCT* mi);
bool TrayMenuDraw(const DRAWITEMSTRUCT* di);

class Tray {
public:
    void Init(HWND hwnd, HINSTANCE inst);
    bool Visible() const { return visible_; }
    void Show();
    void Hide();
    // Re-adds the icon after the shell restarted, or when the first add failed.
    void OnTaskbarCreated();
    void ReloadIcons();
    void Update(TrayIconKind kind, const std::wstring& tooltip);
    // Shows the menu at the given screen position and returns the chosen command (0 for none).
    UINT ShowMenu(POINT pt, bool enabled, const std::wstring& status);

private:
    void FillData(struct _NOTIFYICONDATAW& nid) const;
    HICON IconFor(TrayIconKind kind) const;
    bool Add();

    HWND hwnd_ = nullptr;
    HINSTANCE inst_ = nullptr;
    bool visible_ = false;
    // Set by Show: the icon should exist even if the shell refused the add so far.
    bool wantVisible_ = false;
    TrayIconKind kind_ = TrayIconKind::On;
    std::wstring tooltip_;
    HICON icons_[3] = {};
};
