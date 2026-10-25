#include <windows.h>

#include <cstdio>
#include <cwctype>
#include <cwchar>
#include <string>
#include <vector>

#include "settings.h"
#include "strings.h"
#include "test.h"

namespace {

constexpr Language kAllLanguages[] = {Language::English, Language::German,  Language::Spanish, Language::Portuguese,
                                      Language::Italian, Language::Dutch,   Language::Polish,  Language::Turkish,
                                      Language::Japanese, Language::Chinese};

int CountSubstr(const std::wstring& s, const wchar_t* sub) {
    int n = 0;
    size_t pos = 0;
    while ((pos = s.find(sub, pos)) != std::wstring::npos) {
        ++n;
        pos += wcslen(sub);
    }
    return n;
}

// The printf conversions of a text in order of appearance ("?" for anything the app does not use). A literal percent
// sign takes no argument, so it may sit anywhere ("%50" in Turkish) and is left out.
std::vector<std::wstring> Specifiers(const std::wstring& s) {
    static const wchar_t* const known[] = {L"%%", L"%hs", L"%lu", L"%llu", L"%d", L"%s"};
    std::vector<std::wstring> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'%') continue;
        bool found = false;
        for (const wchar_t* k : known) {
            if (s.compare(i, wcslen(k), k) == 0) {
                if (wcscmp(k, L"%%") != 0) out.push_back(k);
                i += wcslen(k) - 1;
                found = true;
                break;
            }
        }
        if (!found) out.push_back(L"?");
    }
    return out;
}

}  // namespace

TEST(strings_not_empty_and_trimmed) {
    for (Language lang : kAllLanguages) {
        for (int i = 0; i < static_cast<int>(StrId::Count); ++i) {
            std::wstring s = StrFor(lang, static_cast<StrId>(i));
            CHECK(!s.empty());
            CHECK(s.front() != L' ' && s.back() != L' ');
        }
    }
}

TEST(strings_same_specifiers_as_english) {
    for (Language lang : kAllLanguages) {
        for (int i = 0; i < static_cast<int>(StrId::Count); ++i) {
            std::vector<std::wstring> en = Specifiers(StrFor(Language::English, static_cast<StrId>(i)));
            std::vector<std::wstring> got = Specifiers(StrFor(lang, static_cast<StrId>(i)));
            CHECK(en == got);
            for (const std::wstring& spec : got) CHECK(spec != L"?");
        }
    }
}

TEST(strings_status_lowered_has_one_placeholder) {
    for (Language lang : kAllLanguages) CHECK(CountSubstr(StrFor(lang, StrId::StatusLowered), L"%d") == 1);
    SetLanguage(Language::German);
    CHECK(FormatPercent(StrId::StatusLowered, 20) == L"Werbung: leiser (20 %)");
    SetLanguage(Language::English);
    CHECK(FormatPercent(StrId::StatusLowered, 20) == L"Ad: volume lowered (20%)");
}

TEST(strings_media_status) {
    for (Language lang : kAllLanguages) CHECK(CountSubstr(StrFor(lang, StrId::StatusMediaLowered), L"%d") == 1);
    SetLanguage(Language::German);
    CHECK(FormatPercent(StrId::StatusMediaLowered, 20) == L"Video: leiser (20 %)");
    SetLanguage(Language::English);
    CHECK(FormatPercent(StrId::StatusMediaLowered, 20) == L"Video: volume lowered (20%)");
}

TEST(strings_pause_choice) {
    CHECK(std::wstring(StrFor(Language::English, StrId::MediaPause)) == L"Pause Spotify");
    CHECK(std::wstring(StrFor(Language::German, StrId::MediaPause)) == L"Spotify pausieren");
    CHECK(std::wstring(StrFor(Language::English, StrId::StatusMediaPaused)) == L"Video: Spotify paused");
    CHECK(std::wstring(StrFor(Language::German, StrId::StatusMediaPaused)) == L"Video: Spotify pausiert");
}

TEST(strings_no_dashes) {
    for (Language lang : kAllLanguages) {
        for (int i = 0; i < static_cast<int>(StrId::Count); ++i) {
            std::wstring s = StrFor(lang, static_cast<StrId>(i));
            CHECK(s.find(wchar_t(0x2013)) == std::wstring::npos);
            CHECK(s.find(wchar_t(0x2014)) == std::wstring::npos);
        }
        std::wstring name = LanguageNativeName(lang);
        CHECK(name.find(wchar_t(0x2013)) == std::wstring::npos);
        CHECK(name.find(wchar_t(0x2014)) == std::wstring::npos);
    }
}

TEST(strings_tables_are_translated) {
    // A row equal to English is an untranslated row, except for names and words that are the same in that language.
    static const wchar_t* const kSameEverywhere[] = {L"AdMutify", L"Spotify", L"Volume", L"System", L"Video"};
    for (Language lang : kAllLanguages) {
        if (lang == Language::English) continue;
        for (int i = 0; i < static_cast<int>(StrId::Count); ++i) {
            const std::wstring en = StrFor(Language::English, static_cast<StrId>(i));
            if (en != StrFor(lang, static_cast<StrId>(i))) continue;
            // Rows without a letter outside their format codes ("%d %%") are the same in every language.
            bool allowed = true;
            for (size_t k = 0; k < en.size(); ++k)
                if (iswalpha(en[k]) && (k == 0 || en[k - 1] != L'%')) allowed = false;
            for (const wchar_t* same : kSameEverywhere)
                if (en == same) allowed = true;
            if (!allowed) printf("  untranslated: %s row %d \"%ls\"\n", LanguageCode(lang), i, en.c_str());
            CHECK(allowed);
        }
    }
}

TEST(strings_tooltip) {
    CHECK(TooltipText(L"Ad: muted") == L"AdMutify\nAd: muted");
    CHECK(TooltipText(std::wstring(300, L'x').c_str()).size() == 127);
    // "AdMutify\n" is 9 units; an emoji (surrogate pair) straddling unit 127 is cut whole.
    std::wstring emoji = std::wstring(117, L'x') + L"\xD83D\xDE00" + L"tail";
    std::wstring cut = TooltipText(emoji.c_str());
    CHECK(cut.size() == 126);
    CHECK(cut.back() == L'x');
    emoji = std::wstring(116, L'x') + L"\xD83D\xDE00" + L"tail";
    cut = TooltipText(emoji.c_str());
    CHECK(cut.size() == 127 && cut.back() == 0xDE00);
}

TEST(strings_language_switch) {
    SetLanguage(Language::German);
    CHECK(std::wstring(Str(StrId::MenuExit)) == L"Beenden");
    SetLanguage(Language::Spanish);
    CHECK(std::wstring(Str(StrId::MenuExit)) == std::wstring(StrFor(Language::Spanish, StrId::MenuExit)));
    SetLanguage(Language::English);
    CHECK(std::wstring(Str(StrId::MenuExit)) == L"Exit");
}

TEST(strings_system_follows_ui_language) {
    SetLanguage(Language::System);
    const Language expected = LanguageFromLangId(GetUserDefaultUILanguage());
    CHECK(ResolveLanguage(Language::System) == expected);
    CHECK(std::wstring(Str(StrId::MenuExit)) == std::wstring(StrFor(expected, StrId::MenuExit)));
    CHECK(std::wstring(StrFor(Language::System, StrId::MenuExit)) == std::wstring(StrFor(expected, StrId::MenuExit)));
    SetLanguage(Language::English);
}

TEST(strings_language_from_langid) {
    CHECK(LanguageFromLangId(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)) == Language::German);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN_AUSTRIAN)) == Language::German);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH)) == Language::Spanish);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MEXICAN)) == Language::Spanish);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_PORTUGUESE, SUBLANG_PORTUGUESE_BRAZILIAN)) == Language::Portuguese);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_PORTUGUESE, SUBLANG_PORTUGUESE)) == Language::Portuguese);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_ITALIAN, SUBLANG_ITALIAN)) == Language::Italian);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_DUTCH, SUBLANG_DUTCH)) == Language::Dutch);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_DUTCH, SUBLANG_DUTCH_BELGIAN)) == Language::Dutch);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_POLISH, SUBLANG_DEFAULT)) == Language::Polish);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_TURKISH, SUBLANG_DEFAULT)) == Language::Turkish);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT)) == Language::Japanese);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) == Language::Chinese);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) == Language::Chinese);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) == Language::English);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)) == Language::English);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_KOREAN, SUBLANG_DEFAULT)) == Language::English);
    CHECK(LanguageFromLangId(0) == Language::English);
}

TEST(strings_native_names) {
    CHECK(std::wstring(LanguageNativeName(Language::English)) == L"English");
    CHECK(std::wstring(LanguageNativeName(Language::German)) == L"Deutsch");
    CHECK(std::wstring(LanguageNativeName(Language::Spanish)) == L"Español");
    CHECK(std::wstring(LanguageNativeName(Language::Portuguese)) == L"Português (Brasil)");
    CHECK(std::wstring(LanguageNativeName(Language::Italian)) == L"Italiano");
    CHECK(std::wstring(LanguageNativeName(Language::Dutch)) == L"Nederlands");
    CHECK(std::wstring(LanguageNativeName(Language::Polish)) == L"Polski");
    CHECK(std::wstring(LanguageNativeName(Language::Turkish)) == L"Türkçe");
    CHECK(std::wstring(LanguageNativeName(Language::Japanese)) == L"日本語");
    CHECK(std::wstring(LanguageNativeName(Language::Chinese)) == L"简体中文");
    CHECK(std::wstring(LanguageNativeName(Language::System)).empty());
    for (Language lang : kAllLanguages) CHECK(!std::wstring(LanguageNativeName(lang)).empty());
}
