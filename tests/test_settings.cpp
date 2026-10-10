#include <windows.h>

#include <iterator>
#include <string>

#include "settings.h"
#include "test.h"

namespace {

void RemoveSettingsFile() {
    DeleteFileW(SettingsPath().c_str());
}

}  // namespace

TEST(settings_roundtrip) {
    Settings s;
    s.enabled = false;
    s.mode = Mode::Lower;
    s.lowerPercent = 35;
    s.hideTray = true;
    s.language = Language::German;
    s.darkMenus = false;
    Settings back;
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.enabled == false);
    CHECK(back.mode == Mode::Lower);
    CHECK(back.lowerPercent == 35);
    CHECK(back.hideTray == true);
    CHECK(back.language == Language::German);
    CHECK(back.darkMenus == false);

    s.language = Language::English;
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.language == Language::English);
}

TEST(settings_language_codes) {
    struct Case {
        Language language;
        const char* code;
    };
    const Case cases[] = {{Language::System, "system"}, {Language::English, "en"},    {Language::German, "de"},
                          {Language::Spanish, "es"},   {Language::Portuguese, "pt"}, {Language::Italian, "it"},
                          {Language::Dutch, "nl"},     {Language::Polish, "pl"},     {Language::Turkish, "tr"},
                          {Language::Japanese, "ja"},  {Language::Chinese, "zh"}};
    CHECK(static_cast<int>(std::size(cases)) == kLanguageCount);
    for (const Case& c : cases) {
        CHECK(std::string(LanguageCode(c.language)) == c.code);
        CHECK(LanguageFromCode(c.code) == c.language);
        Settings s;
        s.language = c.language;
        Settings back;
        back.language = c.language == Language::Dutch ? Language::Polish : Language::Dutch;
        CHECK(ParseSettings(FormatSettings(s), back));
        CHECK(back.language == c.language);
        CHECK(FormatSettings(s).find(std::string("language=") + c.code + "\r\n") != std::string::npos);
        Settings parsed;
        parsed.language = c.language == Language::Dutch ? Language::Polish : Language::Dutch;
        CHECK(ParseSettings(std::string("version=1\r\nlanguage=") + c.code + "\r\n", parsed));
        CHECK(parsed.language == c.language);
    }
    CHECK(LanguageFromCode("klingon") == Language::System);
    CHECK(LanguageFromCode("") == Language::System);
    CHECK(LanguageFromCode("DE") == Language::System);
}

TEST(settings_media_roundtrip) {
    Settings s;
    CHECK(s.media == MediaMode::Off);
    Settings back;
    for (MediaMode m : {MediaMode::Off, MediaMode::Lower, MediaMode::Mute, MediaMode::Pause}) {
        s.media = m;
        back.media = MediaMode::Mute;
        CHECK(ParseSettings(FormatSettings(s), back));
        CHECK(back.media == m);
    }
    CHECK(ParseSettings("version=1\r\nmedia=pause\r\n", back));
    CHECK(back.media == MediaMode::Pause);
    CHECK(ParseSettings("version=1\r\nmedia=loud\r\n", back));
    CHECK(back.media == MediaMode::Off);
    CHECK(ParseSettings("version=1\r\nlanguage=de\r\n", back));
    CHECK(back.media == MediaMode::Off);
}

TEST(settings_media_percent) {
    Settings s;
    CHECK(s.mediaPercent == 20);
    s.mediaPercent = 45;
    s.lowerPercent = 30;
    Settings back;
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.mediaPercent == 45);
    CHECK(back.lowerPercent == 30);
    CHECK(ParseSettings("version=1\nmedia_percent=97\n", back));
    CHECK(back.mediaPercent == 90);
    CHECK(ParseSettings("version=1\nmedia_percent=23\n", back));
    CHECK(back.mediaPercent == 25);
    CHECK(ParseSettings("version=1\nmedia_percent=-4\n", back));
    CHECK(back.mediaPercent == 0);
    CHECK(ParseSettings("version=1\nlower_percent=60\n", back));
    CHECK(back.mediaPercent == 20);
    CHECK(back.lowerPercent == 60);
}

TEST(settings_file_roundtrip) {
    RemoveSettingsFile();
    Settings s;
    s.mode = Mode::Lower;
    s.lowerPercent = 50;
    unsigned long err = 1;
    CHECK(SaveSettings(s, &err));
    CHECK(err == 0);
    LoadResult r = LoadOrCreateSettings();
    CHECK(!r.created);
    CHECK(!r.newerVersion);
    CHECK(r.settings.mode == Mode::Lower);
    CHECK(r.settings.lowerPercent == 50);
}

TEST(settings_load_or_create_without_file) {
    RemoveSettingsFile();
    LoadResult r = LoadOrCreateSettings();
    CHECK(r.created);
    CHECK(!r.saveFailed);
    CHECK(r.settings.enabled);
    CHECK(r.settings.mode == Mode::Mute);
    CHECK(r.settings.lowerPercent == 20);
    CHECK(GetFileAttributesW(SettingsPath().c_str()) != INVALID_FILE_ATTRIBUTES);
}

TEST(settings_defaults) {
    Settings d;
    Settings parsed;
    CHECK(ParseSettings("", parsed));
    CHECK(parsed.enabled == d.enabled);
    CHECK(parsed.mode == d.mode);
    CHECK(parsed.lowerPercent == d.lowerPercent);
    CHECK(parsed.hideTray == d.hideTray);
    CHECK(parsed.language == d.language);
    CHECK(parsed.darkMenus == d.darkMenus);
}

TEST(settings_newer_version_keeps_file) {
    RemoveSettingsFile();
    const std::string original = "version=2\r\nenabled=0\r\nmode=lower\r\nfuture_key=1\r\n";
    CHECK(WriteFileAtomic(SettingsPath(), original));
    LoadResult r = LoadOrCreateSettings();
    CHECK(r.newerVersion);
    CHECK(r.settings.enabled);
    CHECK(r.settings.mode == Mode::Mute);
    Settings other;
    other.enabled = false;
    CHECK(SaveSettings(other));
    std::string after;
    CHECK(ReadFileUtf8(SettingsPath(), after));
    CHECK(after == original);
    RemoveSettingsFile();
    LoadOrCreateSettings();
}

TEST(settings_unreadable_file_is_kept) {
    RemoveSettingsFile();
    const std::string original = "version=1\r\nenabled=0\r\nmode=lower\r\n";
    CHECK(WriteFileAtomic(SettingsPath(), original));
    // An exclusive open makes the read fail with a sharing violation.
    HANDLE lock = CreateFileW(SettingsPath().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    LoadResult r = LoadOrCreateSettings();
    CHECK(r.readFailed);
    CHECK(!r.created);
    CHECK(r.settings.enabled);
    Settings other;
    other.mode = Mode::Lower;
    CHECK(SaveSettings(other));
    CHECK(r.readError == ERROR_SHARING_VIOLATION);
    // Still locked: the retry fails and keeps the hold.
    Settings fromFile;
    unsigned long retryError = 0;
    CHECK(SettingsReadFailed());
    CHECK(!RetrySettingsRead(fromFile, &retryError));
    CHECK(retryError == ERROR_SHARING_VIOLATION);
    CHECK(SettingsReadFailed());
    CloseHandle(lock);
    std::string after;
    CHECK(ReadFileUtf8(SettingsPath(), after));
    CHECK(after == original);
    // Readable again: the hold is lifted and the file's values come back.
    CHECK(RetrySettingsRead(fromFile, &retryError));
    CHECK(!SettingsReadFailed());
    CHECK(!fromFile.enabled);
    CHECK(fromFile.mode == Mode::Lower);
    // Only what the user changed overrides the file: here the language.
    Settings shown;
    Settings changed = shown;
    changed.language = Language::German;
    Settings merged = MergeSettingsChange(fromFile, shown, changed);
    CHECK(merged.language == Language::German);
    CHECK(!merged.enabled);
    CHECK(merged.mode == Mode::Lower);
    RemoveSettingsFile();
    CHECK(WriteFileAtomic(SettingsPath(), original));
    LoadResult again = LoadOrCreateSettings();
    CHECK(!again.readFailed);
    CHECK(!again.settings.enabled);
    RemoveSettingsFile();
    LoadOrCreateSettings();
}

TEST(settings_percent_clamped) {
    Settings s;
    CHECK(ParseSettings("version=1\nlower_percent=97\n", s));
    CHECK(s.lowerPercent == 90);
    CHECK(ParseSettings("version=1\nlower_percent=23\n", s));
    CHECK(s.lowerPercent == 25);
    CHECK(ParseSettings("version=1\nlower_percent=-4\n", s));
    CHECK(s.lowerPercent == 0);
    CHECK(ParseSettings("version=1\nlower_percent=22\n", s));
    CHECK(s.lowerPercent == 20);
}

TEST(settings_garbage_lines) {
    Settings s;
    CHECK(ParseSettings("\xEF\xBB\xBF# comment\nnonsense\n=\n\x01\x02 junk\nenabled=maybe\nmode=lower\n  hide_tray = 1  \nlanguage=klingon\n", s));
    CHECK(s.mode == Mode::Lower);
    CHECK(s.hideTray);
    CHECK(s.enabled);
    CHECK(s.language == Language::System);
}
