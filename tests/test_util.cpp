// Tests for the small shared helpers in util.h.
#include <string>

#include "test.h"
#include "util.h"

TEST(util_equals_no_case) {
    CHECK(EqualsNoCase(L"", L""));
    CHECK(EqualsNoCase(L"Spotify.exe", L"SPOTIFY.EXE"));
    // Identical non-ASCII text is equal; only the ASCII letters are folded in the default locale.
    CHECK(EqualsNoCase(L"\x00E4rger", L"\x00E4rger"));
    CHECK(!EqualsNoCase(L"\x00E4rger", L"\x00F6rger"));
    CHECK(!EqualsNoCase(L"a", L"ab"));
    CHECK(!EqualsNoCase(L"ab", L"a"));
    CHECK(!EqualsNoCase(L"abc", L"abd"));
    CHECK(!EqualsNoCase(L"", L" "));
    // Embedded terminators count as characters.
    CHECK(EqualsNoCase(std::wstring(L"a\0b", 3), std::wstring(L"A\0B", 3)));
    CHECK(!EqualsNoCase(std::wstring(L"a\0b", 3), std::wstring(L"a\0c", 3)));
}

TEST(util_utf8_roundtrip) {
    const wchar_t* samples[] = {L"", L"plain", L"Gro\x00DF" L"e Stra\x00DF" L"e", L"\x4E2D\x6587", L"note \xD83C\xDFB5 end",
                                L"a\tb\r\nc"};
    for (const wchar_t* w : samples) {
        const std::string u8 = WideToUtf8(w);
        CHECK(Utf8ToWide(u8) == w);
    }
    // Known byte sequences.
    CHECK(WideToUtf8(L"\x00E4") == "\xC3\xA4");
    CHECK(WideToUtf8(L"\x20AC") == "\xE2\x82\xAC");
    CHECK(WideToUtf8(L"\xD83C\xDFB5") == "\xF0\x9F\x8E\xB5");
    CHECK(Utf8ToWide("\xC3\xA4") == L"\x00E4");
    CHECK(WideToUtf8(L"x").size() == 1);
}

TEST(util_utf8_handles_garbage_without_crashing) {
    // Invalid input becomes a replacement character instead of failing, and never reads out of range.
    const std::wstring w = Utf8ToWide("a\xFF" "b");
    CHECK(w.size() == 3);
    CHECK(w.front() == L'a' && w.back() == L'b');
    CHECK(Utf8ToWide("\xC3").size() == 1);
    CHECK(!WideToUtf8(L"\xD800").empty());
    // Embedded terminators survive both ways.
    const std::string withNul("a\0b", 3);
    CHECK(Utf8ToWide(withNul).size() == 3);
    CHECK(WideToUtf8(std::wstring(L"a\0b", 3)) == withNul);
    // Long input.
    const std::string big(200000, 'x');
    CHECK(Utf8ToWide(big).size() == big.size());
    CHECK(WideToUtf8(std::wstring(200000, L'y')).size() == 200000);
}

TEST(util_exe_path_and_names) {
    const std::wstring path = ExePath();
    CHECK(!path.empty());
    CHECK(path.find(L'\\') != std::wstring::npos);
    const size_t slash = path.find_last_of(L'\\');
    const std::wstring file = path.substr(slash + 1);
    CHECK(EqualsNoCase(file, L"admutify_tests.exe"));

    // The own process resolves to the same file name.
    CHECK(EqualsNoCase(ExeNameOfPid(GetCurrentProcessId()), file));
    CHECK(EqualsNoCase(ImageFileName(GetCurrentProcess()), file));
}

TEST(util_exe_name_of_missing_process) {
    // Pid 0 is the idle process, a huge value is no process; neither can be opened for a name.
    CHECK(ExeNameOfPid(0).empty());
    CHECK(ExeNameOfPid(0xFFFFFFF0u).empty());
    CHECK(ImageFileName(nullptr).empty());
}
