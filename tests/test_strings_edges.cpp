// Edge tests for the string tables: unknown ids and languages, percent formatting limits, tooltip length.
#include <windows.h>

#include <climits>
#include <cwchar>
#include <string>

#include "settings.h"
#include "strings.h"
#include "test.h"

namespace {

constexpr Language kLangs[] = {Language::English,  Language::German, Language::Spanish, Language::Portuguese,
                               Language::Italian,  Language::Dutch,  Language::Polish,  Language::Turkish,
                               Language::Japanese, Language::Chinese};

// True when the text holds exactly one conversion and it is %d.
bool HasOnlyOnePercentD(const std::wstring& s) {
    int count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'%') continue;
        if (i + 1 < s.size() && s[i + 1] == L'%') {
            ++i;
            continue;
        }
        if (i + 1 < s.size() && s[i + 1] == L'd') {
            ++count;
            ++i;
            continue;
        }
        return false;
    }
    return count == 1;
}

}  // namespace

TEST(strings_edges_unknown_ids_give_empty_text) {
    for (Language lang : kLangs) {
        CHECK(std::wstring(StrFor(lang, StrId::Count)).empty());
        CHECK(std::wstring(StrFor(lang, static_cast<StrId>(9999))).empty());
        CHECK(std::wstring(StrFor(lang, static_cast<StrId>(-1))).empty());
    }
    CHECK(StrFor(Language::English, StrId::Count) != nullptr);
}

TEST(strings_edges_language_helpers_with_odd_input) {
    // Unsupported, neutral and invalid language ids fall back to English.
    CHECK(LanguageFromLangId(0) == Language::English);
    CHECK(LanguageFromLangId(LANG_NEUTRAL) == Language::English);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)) == Language::English);
    CHECK(LanguageFromLangId(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) == Language::English);
    CHECK(LanguageFromLangId(0xFFFF) == Language::English);
    CHECK(LanguageFromLangId(0x03FF) == Language::English);
    // A concrete language resolves to itself, System to a concrete one.
    for (Language lang : kLangs) CHECK(ResolveLanguage(lang) == lang);
    CHECK(ResolveLanguage(Language::System) != Language::System);
    CHECK(ResolveLanguage(ResolveLanguage(Language::System)) == ResolveLanguage(Language::System));
    // A value outside the enum has no native name and no text of its own.
    CHECK(std::wstring(LanguageNativeName(static_cast<Language>(99))).empty());
    CHECK(std::wstring(LanguageNativeName(static_cast<Language>(-1))).empty());
}

TEST(strings_edges_system_language_follows_resolution) {
    SetLanguage(Language::System);
    for (int i = 0; i < static_cast<int>(StrId::Count); ++i) {
        const StrId id = static_cast<StrId>(i);
        CHECK(std::wstring(Str(id)) == StrFor(ResolveLanguage(Language::System), id));
    }
    SetLanguage(Language::English);
}

TEST(strings_edges_percent_texts_fit_for_extreme_numbers) {
    // Every text with a %d must still fit the format buffer with the longest possible number.
    int checked = 0;
    for (Language lang : kLangs) {
        for (int i = 0; i < static_cast<int>(StrId::Count); ++i) {
            const StrId id = static_cast<StrId>(i);
            const std::wstring text = StrFor(lang, id);
            if (!HasOnlyOnePercentD(text)) continue;
            ++checked;
            CHECK(text.size() + 12 < 128);
        }
    }
    CHECK(checked >= 10);
}

TEST(strings_edges_format_percent_numbers) {
    for (Language lang : kLangs) {
        SetLanguage(lang);
        for (int i = 0; i < static_cast<int>(StrId::Count); ++i) {
            const StrId id = static_cast<StrId>(i);
            const std::wstring text = Str(id);
            if (!HasOnlyOnePercentD(text) || text.size() + 12 >= 128) continue;
            for (int value : {0, 1, 20, 100, -1, 123456, INT_MAX, INT_MIN}) {
                const std::wstring out = FormatPercent(id, value);
                CHECK(out.find(std::to_wstring(value)) != std::wstring::npos);
                CHECK(out.find(L"%d") == std::wstring::npos);
            }
        }
    }
    SetLanguage(Language::English);
    CHECK(FormatPercent(StrId::StatusLowered, 35).find(L"35") != std::wstring::npos);
}

TEST(strings_edges_tooltip_length_limits) {
    // "AdMutify\n" takes 9 units, so a status of 118 units fills the limit of 127 exactly.
    CHECK(TooltipText(L"").size() == 9);
    CHECK(TooltipText(L"") == L"AdMutify\n");
    CHECK(TooltipText(std::wstring(117, L'x').c_str()).size() == 126);
    CHECK(TooltipText(std::wstring(118, L'x').c_str()).size() == 127);
    CHECK(TooltipText(std::wstring(118, L'x').c_str()) == L"AdMutify\n" + std::wstring(118, L'x'));
    CHECK(TooltipText(std::wstring(119, L'x').c_str()).size() == 127);
    CHECK(TooltipText(std::wstring(100000, L'x').c_str()).size() == 127);
    // A high surrogate that is the last unit of a text that fits is not touched.
    const std::wstring lone = std::wstring(10, L'x') + L"\xD83D";
    CHECK(TooltipText(lone.c_str()) == L"AdMutify\n" + lone);
    // Cut right after a complete pair keeps the pair.
    const std::wstring pair = std::wstring(116, L'x') + L"\xD83D\xDE00";
    const std::wstring cut = TooltipText(pair.c_str());
    CHECK(cut.size() == 127 && cut.back() == 0xDE00);
    // Every real status text fits a tooltip in every language.
    for (Language lang : kLangs) {
        for (int i = 0; i < static_cast<int>(StrId::Count); ++i) {
            const std::wstring t = TooltipText(StrFor(lang, static_cast<StrId>(i)));
            CHECK(t.size() <= 127);
            CHECK(t.compare(0, 9, L"AdMutify\n") == 0);
        }
    }
}
