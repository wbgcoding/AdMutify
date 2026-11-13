// Tests for the theme colour table and the owner-drawn tray menu builders. Menus are only built and inspected,
// never shown; drawing goes to an in-memory bitmap.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "strings.h"
#include "test.h"
#include "theme.h"
#include "tray.h"

namespace {

double Channel(int v) {
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double Luminance(COLORREF c) { return 0.2126 * Channel(GetRValue(c)) + 0.7152 * Channel(GetGValue(c)) + 0.0722 * Channel(GetBValue(c)); }

double Contrast(COLORREF a, COLORREF b) {
    const double la = Luminance(a);
    const double lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

void UseTheme(int mode) {
    SetThemeOverride(mode);
    RefreshThemeCache();
}

void FollowSystem() {
    SetThemeOverride(0);
    RefreshThemeCache();
}

std::wstring ItemString(HMENU menu, UINT id) {
    wchar_t buf[1024] = {};
    MENUITEMINFOW mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_STRING;
    mi.dwTypeData = buf;
    mi.cch = 1023;
    if (!GetMenuItemInfoW(menu, id, FALSE, &mi)) return L"<missing>";
    return buf;
}

UINT ItemState(HMENU menu, UINT id) {
    MENUITEMINFOW mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_STATE;
    return GetMenuItemInfoW(menu, id, FALSE, &mi) ? mi.fState : 0xFFFFFFFFu;
}

MEASUREITEMSTRUCT MeasureOf(HMENU menu, UINT id) {
    MEASUREITEMSTRUCT m{};
    m.CtlType = ODT_MENU;
    m.itemID = id;
    m.itemData = reinterpret_cast<ULONG_PTR>(menu);
    return m;
}

}  // namespace

TEST(theme_override_picks_light_or_dark) {
    UseTheme(1);
    CHECK(!IsDarkTheme());
    UseTheme(2);
    if (!IsHighContrast()) CHECK(IsDarkTheme());
    UseTheme(1);
    CHECK(!IsDarkTheme());
    FollowSystem();
    // Following the system gives the same answer as reading the system, unless high contrast overrides it.
    if (!IsHighContrast()) CHECK(IsDarkTheme() == SystemUsesDarkApps());
}

TEST(theme_light_and_dark_tables_are_fixed) {
    SetAccentForTests(&kDefaultAccent);
    UseTheme(1);
    if (!IsHighContrast()) {
        CHECK(ThemeBackground() == RGB(0xF3, 0xF3, 0xF3));
        CHECK(ThemeText() == RGB(0x1A, 0x1A, 0x1A));
        CHECK(ThemeAccent() == RGB(0x00, 0x67, 0xC0));
        CHECK(ThemeAccentText() == RGB(0xFF, 0xFF, 0xFF));
        CHECK(ThemeAccentOnBackground() == RGB(0x00, 0x67, 0xC0));
    }
    UseTheme(2);
    if (!IsHighContrast()) {
        CHECK(ThemeBackground() == RGB(0x20, 0x20, 0x20));
        CHECK(ThemeText() == RGB(0xFF, 0xFF, 0xFF));
        // Windows blue on dark: white text on the accent itself, not black on the light shade.
        CHECK(ThemeAccent() == RGB(0x00, 0x78, 0xD4));
        CHECK(ThemeAccentText() == RGB(0xFF, 0xFF, 0xFF));
        CHECK(ThemeAccentOnBackground() == RGB(0x4C, 0xC2, 0xFF));
    }
    SetAccentForTests(nullptr);
    FollowSystem();
}

TEST(theme_accent_palette_parsing) {
    // The default Windows blue palette as the registry stores it.
    const BYTE blue[32] = {0x99, 0xEB, 0xFF, 0, 0x4C, 0xC2, 0xFF, 0, 0x00, 0x91, 0xF8, 0, 0x00, 0x78, 0xD4, 0,
                           0x00, 0x67, 0xC0, 0, 0x00, 0x3E, 0x92, 0, 0x00, 0x1A, 0x68, 0, 0xF7, 0x63, 0x0C, 0};
    AccentColors got{};
    CHECK(ParseAccentPalette(blue, sizeof(blue), &got));
    CHECK(got.light2 == RGB(0x4C, 0xC2, 0xFF));
    CHECK(got.base == RGB(0x00, 0x78, 0xD4));
    CHECK(got.dark1 == RGB(0x00, 0x67, 0xC0));
    // Too short or missing: refused, the output stays as it was.
    AccentColors keep = kDefaultAccent;
    CHECK(!ParseAccentPalette(blue, 31, &keep));
    CHECK(!ParseAccentPalette(nullptr, 32, &keep));
    CHECK(!ParseAccentPalette(blue, 32, nullptr));
    CHECK(keep.base == kDefaultAccent.base);
}

TEST(theme_accent_helpers) {
    CHECK(ContrastRatio(RGB(0, 0, 0), RGB(0xFF, 0xFF, 0xFF)) > 20.9);
    CHECK(ContrastRatio(RGB(0x55, 0x55, 0x55), RGB(0x55, 0x55, 0x55)) == 1.0);
    CHECK(BlendColor(RGB(0, 0, 0), RGB(200, 100, 50), 0) == RGB(0, 0, 0));
    CHECK(BlendColor(RGB(0, 0, 0), RGB(200, 100, 50), 100) == RGB(200, 100, 50));
    CHECK(BlendColor(RGB(0, 0, 0), RGB(200, 100, 50), 10) == RGB(20, 10, 5));
    CHECK(TextOn(RGB(0x00, 0x78, 0xD4)) == RGB(0xFF, 0xFF, 0xFF));
    CHECK(TextOn(RGB(0xFF, 0xB9, 0x00)) == RGB(0x00, 0x00, 0x00));
    CHECK(TextOn(RGB(0xFF, 0xFF, 0xFF)) == RGB(0x00, 0x00, 0x00));
    CHECK(TextOn(RGB(0x00, 0x00, 0x00)) == RGB(0xFF, 0xFF, 0xFF));
}

TEST(theme_any_accent_stays_readable) {
    // Windows presets from light to dark: yellow, orange, red, pink, purple, green, teal, grey, blue.
    const AccentColors palettes[] = {
        kDefaultAccent,
        {RGB(0xFF, 0xE0, 0x5C), RGB(0xFF, 0xB9, 0x00), RGB(0xD1, 0x93, 0x00)},
        {RGB(0xFF, 0xB3, 0x7A), RGB(0xF7, 0x63, 0x0C), RGB(0xC8, 0x4C, 0x00)},
        {RGB(0xFF, 0x8F, 0x91), RGB(0xE8, 0x11, 0x23), RGB(0xC4, 0x00, 0x14)},
        {RGB(0xF7, 0x9F, 0xD8), RGB(0xE3, 0x00, 0x8C), RGB(0xBF, 0x00, 0x77)},
        {RGB(0xC6, 0xA2, 0xF8), RGB(0x88, 0x17, 0x98), RGB(0x74, 0x00, 0x86)},
        {RGB(0x6C, 0xE2, 0x6C), RGB(0x10, 0x7C, 0x10), RGB(0x05, 0x6A, 0x05)},
        {RGB(0x5C, 0xE0, 0xD7), RGB(0x00, 0x99, 0xBC), RGB(0x00, 0x82, 0xA0)},
        {RGB(0xBB, 0xBB, 0xBB), RGB(0x76, 0x76, 0x76), RGB(0x64, 0x64, 0x64)},
    };
    for (const AccentColors& p : palettes) {
        SetAccentForTests(&p);
        for (int mode : {1, 2}) {
            UseTheme(mode);
            if (IsHighContrast()) continue;
            CHECK(Contrast(ThemeAccentText(), ThemeAccent()) >= 4.5);
            // Hover and pressed last only a moment on a bold label, so the large text limit applies.
            CHECK(Contrast(ThemeAccentText(), ThemeAccentHot()) >= 3.0);
            CHECK(Contrast(ThemeAccentText(), ThemeAccentPressed()) >= 3.0);
            CHECK(ThemeAccent() != ThemeAccentHot());
            CHECK(ThemeAccentHot() != ThemeAccentPressed());
        }
    }
    SetAccentForTests(nullptr);
    FollowSystem();
}

TEST(theme_text_stays_readable_in_both_modes) {
    SetAccentForTests(&kDefaultAccent);
    for (int mode : {1, 2}) {
        UseTheme(mode);
        if (IsHighContrast()) continue;
        CHECK(Contrast(ThemeText(), ThemeBackground()) >= 7.0);
        CHECK(Contrast(ThemeText(), ThemeCard()) >= 7.0);
        CHECK(Contrast(ThemeText(), ThemeControl()) >= 7.0);
        CHECK(Contrast(ThemeDisabledText(), ThemeBackground()) >= 4.0);
        CHECK(Contrast(ThemeDisabledText(), ThemeBackground()) < Contrast(ThemeText(), ThemeBackground()));
        CHECK(Contrast(ThemeAccentText(), ThemeAccent()) >= 4.5);
        CHECK(Contrast(ThemeAccentText(), ThemeAccentHot()) >= 4.5);
        // The pressed state lasts a moment and the label is bold-sized, so the large text limit applies.
        CHECK(Contrast(ThemeAccentText(), ThemeAccentPressed()) >= 3.0);
        CHECK(Contrast(ThemeCriticalText(), ThemeCritical()) >= 4.5);
        // The large ad counter in accent colour on the window background.
        CHECK(Contrast(ThemeAccentOnBackground(), ThemeBackground()) >= 3.0);
    }
    SetAccentForTests(nullptr);
    FollowSystem();
}

TEST(theme_states_are_distinguishable) {
    for (int mode : {1, 2}) {
        UseTheme(mode);
        if (IsHighContrast()) continue;
        CHECK(ThemeControl() != ThemeControlHot());
        CHECK(ThemeControl() != ThemeControlPressed());
        CHECK(ThemeControlHot() != ThemeControlPressed());
        CHECK(ThemeAccent() != ThemeAccentHot());
        CHECK(ThemeAccent() != ThemeAccentPressed());
        CHECK(ThemeAccentHot() != ThemeAccentPressed());
        CHECK(ThemeBackground() != ThemeCard());
        CHECK(ThemeCard() != ThemeCardBorder());
        CHECK(ThemeControl() != ThemeControlBorder());
    }
    // Light and dark never share the main surface colours.
    UseTheme(1);
    const COLORREF lightBack = ThemeBackground();
    const COLORREF lightText = ThemeText();
    UseTheme(2);
    if (!IsHighContrast()) {
        CHECK(lightBack != ThemeBackground());
        CHECK(lightText != ThemeText());
    }
    FollowSystem();
}

TEST(theme_override_applies_without_a_refresh) {
    // Forcing a mode changes the answer at once even when the system state was cached before.
    UseTheme(1);
    const bool first = IsDarkTheme();
    SetThemeOverride(2);
    const bool afterOverride = IsDarkTheme();
    CHECK(!first);
    if (!IsHighContrast()) CHECK(afterOverride);
    FollowSystem();
}

TEST(tray_menu_has_the_expected_items) {
    SetLanguage(Language::English);
    HMENU menu = BuildTrayMenu(true, L"Ad: muted");
    CHECK(menu != nullptr);
    CHECK(GetMenuItemCount(menu) == 7);
    CHECK(ItemString(menu, IDM_TRAY_STATUS) == L"Ad: muted");
    CHECK(ItemString(menu, IDM_TRAY_ENABLED) == Str(StrId::MenuEnabled));
    CHECK(ItemString(menu, IDM_TRAY_SETTINGS) == Str(StrId::MenuSettings));
    CHECK(ItemString(menu, IDM_TRAY_HIDE) == Str(StrId::MenuHideTray));
    CHECK(ItemString(menu, IDM_TRAY_EXIT) == Str(StrId::MenuExit));
    // The status line is only text; the switch shows its state.
    CHECK((ItemState(menu, IDM_TRAY_STATUS) & MFS_GRAYED) != 0);
    CHECK((ItemState(menu, IDM_TRAY_ENABLED) & MFS_CHECKED) != 0);
    CHECK((ItemState(menu, IDM_TRAY_EXIT) & (MFS_GRAYED | MFS_CHECKED)) == 0);
    DestroyTrayMenu(menu);

    menu = BuildTrayMenu(false, L"");
    CHECK((ItemState(menu, IDM_TRAY_ENABLED) & MFS_CHECKED) == 0);
    CHECK(ItemString(menu, IDM_TRAY_STATUS).empty());
    DestroyTrayMenu(menu);
}

TEST(tray_menu_keeps_long_and_odd_status_text) {
    const std::wstring longText(500, L'x');
    HMENU menu = BuildTrayMenu(true, longText);
    CHECK(ItemString(menu, IDM_TRAY_STATUS).size() == 500);
    DestroyTrayMenu(menu);
    menu = BuildTrayMenu(true, L"a & b\tc");
    CHECK(ItemString(menu, IDM_TRAY_STATUS) == L"a & b\tc");
    DestroyTrayMenu(menu);
}

TEST(tray_choice_menu_marks_one_entry) {
    const wchar_t* const items[] = {L"One", L"Two", L"Three"};
    for (int checked : {0, 1, 2, -1, 3}) {
        HMENU menu = BuildChoiceMenu(items, 3, checked, 120, 96);
        CHECK(GetMenuItemCount(menu) == 3);
        for (int i = 0; i < 3; ++i) {
            CHECK(ItemString(menu, static_cast<UINT>(i + 1)) == items[i]);
            const bool marked = (ItemState(menu, static_cast<UINT>(i + 1)) & MFS_CHECKED) != 0;
            CHECK(marked == (i == checked));
        }
        DestroyTrayMenu(menu);
    }
    HMENU empty = BuildChoiceMenu(items, 0, 0, 120, 96);
    CHECK(GetMenuItemCount(empty) == 0);
    DestroyTrayMenu(empty);
}

TEST(tray_menu_measure_rejects_foreign_items) {
    CHECK(!TrayMenuMeasure(nullptr));
    MEASUREITEMSTRUCT mi{};
    mi.CtlType = ODT_LISTBOX;
    CHECK(!TrayMenuMeasure(&mi));
    CHECK(!TrayMenuDraw(nullptr));
    DRAWITEMSTRUCT di{};
    di.CtlType = ODT_BUTTON;
    CHECK(!TrayMenuDraw(&di));
}

TEST(tray_menu_measure_scales_with_text_and_dpi) {
    const wchar_t* const items[] = {L"Short", L"A much longer menu entry that needs room"};
    HMENU small = BuildChoiceMenu(items, 2, 0, 0, 96);
    MEASUREITEMSTRUCT a = MeasureOf(small, 1);
    MEASUREITEMSTRUCT b = MeasureOf(small, 2);
    CHECK(TrayMenuMeasure(&a));
    CHECK(TrayMenuMeasure(&b));
    CHECK(b.itemWidth > a.itemWidth);
    CHECK(a.itemHeight > 0 && a.itemHeight == b.itemHeight);

    // A separator is a thin line without width.
    MEASUREITEMSTRUCT sep{};
    sep.CtlType = ODT_MENU;
    sep.itemData = static_cast<ULONG_PTR>(-1);
    CHECK(TrayMenuMeasure(&sep));
    CHECK(sep.itemWidth == 0 && sep.itemHeight > 0);
    DestroyTrayMenu(small);

    // A bigger scale gives taller rows.
    HMENU big = BuildChoiceMenu(items, 2, 0, 0, 192);
    MEASUREITEMSTRUCT c = MeasureOf(big, 1);
    CHECK(TrayMenuMeasure(&c));
    CHECK(c.itemHeight > a.itemHeight);
    DestroyTrayMenu(big);

    // A minimum width wins over short text.
    HMENU wide = BuildChoiceMenu(items, 2, 0, 600, 96);
    MEASUREITEMSTRUCT w = MeasureOf(wide, 1);
    CHECK(TrayMenuMeasure(&w));
    CHECK(w.itemWidth > a.itemWidth);
    DestroyTrayMenu(wide);
}

TEST(tray_menu_draw_fills_the_item_background) {
    UseTheme(1);
    if (IsHighContrast()) {
        FollowSystem();
        return;
    }
    const wchar_t* const items[] = {L"Entry"};
    HMENU menu = BuildChoiceMenu(items, 1, 0, 100, 96);

    // An in-memory 32 bit bitmap, so the test needs no window and no display surface.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = 200;
    bi.bmiHeader.biHeight = -40;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    CHECK(bmp != nullptr && bits != nullptr);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(dc, bmp);
        const unsigned* pixels = static_cast<const unsigned*>(bits);
        // Pixels are 0x00RRGGBB.
        const unsigned lightBack = 0xF9F9F9u;

        DRAWITEMSTRUCT di{};
        di.CtlType = ODT_MENU;
        di.itemID = 1;
        di.hDC = dc;
        di.rcItem = RECT{0, 0, 200, 40};
        di.itemData = reinterpret_cast<ULONG_PTR>(menu);
        di.itemState = 0;
        CHECK(TrayMenuDraw(&di));
        GdiFlush();
        // The right edge holds no text, so it shows the plain menu background of the light look.
        CHECK((pixels[2 * 200 + 198] & 0xFFFFFFu) == lightBack);

        // A selected item gets the hot colour inside its rounded highlight, past the text, and keeps the plain
        // background in the margin around it.
        di.itemState = ODS_SELECTED;
        CHECK(TrayMenuDraw(&di));
        GdiFlush();
        CHECK((pixels[20 * 200 + 150] & 0xFFFFFFu) == 0xE9E9E9u);
        CHECK((pixels[1 * 200 + 100] & 0xFFFFFFu) == lightBack);
        SelectObject(dc, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(dc);
    DestroyTrayMenu(menu);
    FollowSystem();
}
