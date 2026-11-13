// Edge and error-path tests for the settings module: clamping, parsing of odd input, file helpers on bad paths.
// File tests work inside the per-run test data folder only.
#include <windows.h>

#include <climits>
#include <string>

#include "settings.h"
#include "test.h"

namespace {

std::wstring EdgeDir() { return DataDir() + L"\\edge"; }

void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(full);
            else DeleteFileW(full.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

size_t CountTempFiles(const std::wstring& dir) {
    size_t n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.tmp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do ++n;
    while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

}  // namespace

TEST(settings_clamp_lower_percent_table) {
    struct Row {
        int in;
        int out;
    };
    const Row rows[] = {{INT_MIN, 0}, {-1, 0}, {0, 0}, {2, 0}, {3, 5}, {5, 5}, {7, 5}, {8, 10}, {87, 85}, {88, 90},
                        {90, 90},     {91, 90}, {100, 90}, {INT_MAX, 90}};
    for (const Row& r : rows) CHECK(ClampLowerPercent(r.in) == r.out);
    // Always a multiple of five inside 0..90, and stable when applied twice.
    for (int v = -20; v <= 130; ++v) {
        const int c = ClampLowerPercent(v);
        CHECK(c >= 0 && c <= 90 && c % 5 == 0);
        CHECK(ClampLowerPercent(c) == c);
    }
}

TEST(settings_clamp_volume_percent_table) {
    struct Row {
        int in;
        int out;
    };
    const Row rows[] = {{INT_MIN, 0}, {-3, 0}, {0, 0}, {2, 0}, {3, 5}, {50, 50}, {52, 50}, {53, 55}, {97, 95},
                        {98, 100},    {100, 100}, {101, 100}, {INT_MAX, 100}};
    for (const Row& r : rows) CHECK(ClampVolumePercent(r.in) == r.out);
    for (int v = -20; v <= 130; ++v) {
        const int c = ClampVolumePercent(v);
        CHECK(c >= 0 && c <= 100 && c % 5 == 0);
        CHECK(ClampVolumePercent(c) == c);
    }
}

TEST(settings_parse_empty_and_junk_gives_defaults) {
    Settings s;
    s.enabled = false;
    s.mode = Mode::Lower;
    s.lowerPercent = 55;
    CHECK(ParseSettings("", s));
    // The result is built from the defaults, not from what the caller held before.
    CHECK(s.enabled && s.mode == Mode::Mute && s.lowerPercent == 20);
    std::string junk;
    for (int i = 0; i < 6000; ++i) junk.push_back(static_cast<char>((i * 131 + 7) & 0xFF));
    CHECK(ParseSettings(junk, s));
    CHECK(ParseSettings(std::string(100000, '\n'), s));
    CHECK(ParseSettings(std::string(100000, '='), s));
}

TEST(settings_parse_number_edge_cases) {
    Settings s;
    // Not a plain integer: the default stays.
    const char* bad[] = {"lower_percent=\n",         "lower_percent=abc\n", "lower_percent=+5\n",
                         "lower_percent=12.5\n",     "lower_percent=0x10\n", "lower_percent=99999999999999\n",
                         "lower_percent=1 2\n",      "lower_percent=--5\n"};
    for (const char* line : bad) {
        CHECK(ParseSettings(std::string("version=2\n") + line, s));
        CHECK(s.lowerPercent == 20);
    }
    CHECK(ParseSettings("version=2\nlower_percent= \t45\t \n", s));
    CHECK(s.lowerPercent == 45);
    CHECK(ParseSettings("version=2\nlower_percent=-99999\n", s));
    CHECK(s.lowerPercent == 0);
    CHECK(ParseSettings("version=2\nad_music_volume=999\n", s));
    CHECK(s.adMusicVolume == 100);
    CHECK(ParseSettings("version=2\nad_music_volume=53\n", s));
    CHECK(s.adMusicVolume == 55);
    CHECK(ParseSettings("version=2\nad_music_volume=oops\n", s));
    CHECK(s.adMusicVolume == 50);
}

TEST(settings_parse_flags_and_modes_strict) {
    Settings s;
    // Only 0 and 1 are flags; anything else keeps the default.
    const char* notFlags[] = {"enabled=true\n", "enabled=2\n", "enabled=\n", "enabled=01\n", "enabled=-1\n", "enabled=0 # off\n"};
    for (const char* line : notFlags) {
        CHECK(ParseSettings(line, s));
        CHECK(s.enabled);
    }
    CHECK(ParseSettings("enabled=0\n", s));
    CHECK(!s.enabled);
    // Keys and values are case sensitive.
    CHECK(ParseSettings("ENABLED=0\nMode=lower\nmode=LOWER\nmedia=Pause\n", s));
    CHECK(s.enabled);
    CHECK(s.mode == Mode::Mute);
    CHECK(s.media == MediaMode::Off);
    // An unknown mode value falls back to mute, an unknown media value to off.
    CHECK(ParseSettings("mode=banana\nmedia=banana\nad_music=banana\n", s));
    CHECK(s.mode == Mode::Mute && s.media == MediaMode::Off && s.adMusic == AdMusic::Shuffle);
}

TEST(settings_parse_duplicates_and_equals_in_values) {
    Settings s;
    // The last line of a key wins.
    CHECK(ParseSettings("mode=lower\nmode=music\nlower_percent=30\nlower_percent=40\n", s));
    CHECK(s.mode == Mode::Music);
    CHECK(s.lowerPercent == 40);
    // Only the first equals sign splits; the rest belongs to the value.
    CHECK(ParseSettings("ad_music_path=C:\\Music\\a=b\\c.mp3\n", s));
    CHECK(s.adMusicPath == L"C:\\Music\\a=b\\c.mp3");
    // Spaces around the key and the value are cut, inside stay.
    CHECK(ParseSettings("  ad_music_path  =  C:\\My Music\\x  \n", s));
    CHECK(s.adMusicPath == L"C:\\My Music\\x");
    // Comment lines start with a hash only.
    CHECK(ParseSettings("#enabled=0\n  # mode=lower\n; hide_tray=1\n", s));
    CHECK(s.enabled && s.mode == Mode::Mute && !s.hideTray);
}

TEST(settings_parse_version_variants) {
    Settings s;
    CHECK(ParseSettings("version=abc\nmode=lower\n", s));
    CHECK(s.mode == Mode::Lower);
    CHECK(ParseSettings("version=\nmode=lower\n", s));
    CHECK(s.mode == Mode::Lower);
    CHECK(ParseSettings("version=0\nmode=lower\n", s));
    CHECK(s.mode == Mode::Lower);
    // A newer file hands back defaults whatever else it holds, and the order of lines does not matter.
    s.mode = Mode::Lower;
    CHECK(!ParseSettings("mode=lower\nversion=3\n", s));
    CHECK(s.mode == Mode::Mute);
    CHECK(!ParseSettings("version=2147483647\n", s));
    CHECK(!ParseSettings("\xEF\xBB\xBF" "version=3\n", s));
    // A number that does not fit is no version at all.
    CHECK(ParseSettings("version=99999999999999999999\nmode=lower\n", s));
    CHECK(s.mode == Mode::Lower);
    // A version 1 file with a music source moves to the music mode, a version 2 file does not.
    CHECK(ParseSettings("version=1\nad_music=order\n", s));
    CHECK(s.mode == Mode::Music);
    CHECK(ParseSettings("version=2\nad_music=order\n", s));
    CHECK(s.mode == Mode::Mute);
    CHECK(ParseSettings("version=1\nad_music=banana\n", s));
    CHECK(s.mode == Mode::Mute);
}

TEST(settings_parse_line_endings) {
    Settings s;
    CHECK(ParseSettings("version=2\r\nmode=lower\r\nlower_percent=35\r\n", s));
    CHECK(s.mode == Mode::Lower && s.lowerPercent == 35);
    CHECK(ParseSettings("version=2\nmode=lower\nlower_percent=35", s));
    CHECK(s.mode == Mode::Lower && s.lowerPercent == 35);
    // A stray carriage return before the line feed is cut from the value.
    CHECK(ParseSettings("mode=lower\r\r\nhide_tray=1\r\n", s));
    CHECK(s.mode == Mode::Lower && s.hideTray);
}

TEST(settings_format_all_combinations_roundtrip) {
    const Mode modes[] = {Mode::Mute, Mode::Lower, Mode::Music};
    const MediaMode medias[] = {MediaMode::Off, MediaMode::Lower, MediaMode::Mute, MediaMode::Pause};
    const AdMusic sources[] = {AdMusic::File, AdMusic::Shuffle, AdMusic::Order};
    for (Mode m : modes)
        for (MediaMode md : medias)
            for (AdMusic am : sources)
                for (bool flag : {false, true}) {
                    Settings s;
                    s.enabled = flag;
                    s.hideTray = !flag;
                    s.darkMenus = flag;
                    s.trace = flag;
                    s.mode = m;
                    s.media = md;
                    s.adMusic = am;
                    s.lowerPercent = flag ? 0 : 90;
                    s.mediaPercent = flag ? 90 : 5;
                    s.adMusicVolume = flag ? 0 : 100;
                    s.adMusicPath = L"D:\\m\\x.mp3";
                    Settings back;
                    CHECK(ParseSettings(FormatSettings(s), back));
                    CHECK(back.enabled == s.enabled && back.hideTray == s.hideTray && back.darkMenus == s.darkMenus);
                    CHECK(back.trace == s.trace && back.mode == s.mode && back.media == s.media && back.adMusic == s.adMusic);
                    CHECK(back.lowerPercent == s.lowerPercent && back.mediaPercent == s.mediaPercent);
                    CHECK(back.adMusicVolume == s.adMusicVolume && back.adMusicPath == s.adMusicPath);
                    // Formatting what was parsed gives the same text again.
                    CHECK(FormatSettings(back) == FormatSettings(s));
                }
    CHECK(FormatSettings(Settings{}).rfind("version=2\r\n", 0) == 0);
}

TEST(settings_format_path_edge_cases) {
    Settings s;
    s.adMusic = AdMusic::File;
    Settings back;
    // Unicode and a very long path survive.
    s.adMusicPath = L"C:\\Musik\\Gr\x00FC\x00DF" L"e \x4E2D\x6587 \xD83C\xDFB5.mp3";
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.adMusicPath == s.adMusicPath);
    s.adMusicPath = L"C:\\" + std::wstring(8000, L'p') + L"\\x.mp3";
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.adMusicPath == s.adMusicPath);
    // A line break in the path would cut the value and shift the next key, so the path is left out whole.
    s.adMusicPath = L"C:\\bad\r\nmode=lower";
    s.adMusicVolume = 35;
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.adMusicPath.empty());
    CHECK(back.mode == Mode::Mute);
    CHECK(back.adMusicVolume == 35);
    // Empty path.
    s.adMusicPath.clear();
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.adMusicPath.empty());
}

namespace {

bool SameSettings(const Settings& a, const Settings& b) {
    return a.enabled == b.enabled && a.mode == b.mode && a.lowerPercent == b.lowerPercent && a.hideTray == b.hideTray &&
           a.language == b.language && a.darkMenus == b.darkMenus && a.media == b.media &&
           a.mediaPercent == b.mediaPercent && a.adMusic == b.adMusic && a.adMusicPath == b.adMusicPath &&
           a.adMusicVolume == b.adMusicVolume && a.trace == b.trace;
}

// Copies one field (by index) from one settings value to another.
void CopyField(Settings& to, const Settings& from, int field) {
    switch (field) {
        case 0: to.enabled = from.enabled; break;
        case 1: to.mode = from.mode; break;
        case 2: to.lowerPercent = from.lowerPercent; break;
        case 3: to.hideTray = from.hideTray; break;
        case 4: to.language = from.language; break;
        case 5: to.darkMenus = from.darkMenus; break;
        case 6: to.media = from.media; break;
        case 7: to.mediaPercent = from.mediaPercent; break;
        case 8: to.adMusic = from.adMusic; break;
        case 9: to.adMusicPath = from.adMusicPath; break;
        case 10: to.adMusicVolume = from.adMusicVolume; break;
        case 11: to.trace = from.trace; break;
    }
}

constexpr int kSettingsFieldCount = 12;

}  // namespace

TEST(settings_merge_takes_only_changed_fields) {
    // Every field of the "other" value differs from the default.
    Settings other;
    other.enabled = false;
    other.mode = Mode::Lower;
    other.lowerPercent = 70;
    other.hideTray = true;
    other.language = Language::Polish;
    other.darkMenus = false;
    other.media = MediaMode::Pause;
    other.mediaPercent = 65;
    other.adMusic = AdMusic::Order;
    other.adMusicPath = L"E:\\file";
    other.adMusicVolume = 85;
    other.trace = true;
    const Settings shown;
    for (int field = 0; field < kSettingsFieldCount; ++field) {
        CHECK(!SameSettings(other, shown));
        // The file holds "other" except in the field the user is about to change; the user moves only that field.
        Settings file = other;
        CopyField(file, shown, field);
        Settings current = shown;
        CopyField(current, other, field);
        CHECK(SameSettings(MergeSettingsChange(file, shown, current), other));
    }
    // Nothing changed: the file wins everywhere.
    CHECK(SameSettings(MergeSettingsChange(other, shown, shown), other));
    // Everything changed: the current values win everywhere.
    CHECK(SameSettings(MergeSettingsChange(shown, shown, other), other));
}

TEST(settings_read_file_edge_cases) {
    const std::wstring dir = EdgeDir();
    RemoveTree(dir);
    CHECK(EnsureDirectory(dir));
    std::string out = "stale";

    // Missing file and missing folder give distinct, documented errors.
    SetLastError(0);
    CHECK(!ReadFileUtf8(dir + L"\\none.txt", out));
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    SetLastError(0);
    CHECK(!ReadFileUtf8(dir + L"\\nodir\\none.txt", out));
    CHECK(GetLastError() == ERROR_PATH_NOT_FOUND);
    CHECK(!ReadFileUtf8(L"", out));

    // A folder is no file.
    CHECK(!ReadFileUtf8(dir, out));

    // Empty file reads as empty text and clears what the caller held.
    CHECK(WriteFileAtomic(dir + L"\\empty.txt", ""));
    out = "stale";
    CHECK(ReadFileUtf8(dir + L"\\empty.txt", out));
    CHECK(out.empty());

    // Sizes around the internal chunk size and binary content with terminators.
    for (size_t size : {size_t{1}, size_t{4095}, size_t{4096}, size_t{4097}, size_t{8192}, size_t{100000}}) {
        std::string data;
        for (size_t i = 0; i < size; ++i) data.push_back(static_cast<char>(i * 7 + 3));
        CHECK(WriteFileAtomic(dir + L"\\bin.dat", data));
        CHECK(ReadFileUtf8(dir + L"\\bin.dat", out));
        CHECK(out == data);
    }
    RemoveTree(dir);
}

TEST(settings_write_atomic_edge_cases) {
    const std::wstring dir = EdgeDir();
    RemoveTree(dir);

    // Missing folders are created, an existing target is replaced, and no temp file stays behind.
    const std::wstring nested = dir + L"\\a\\b\\c\\file.txt";
    unsigned long err = 123;
    CHECK(WriteFileAtomic(nested, "first", &err));
    CHECK(err == 0);
    CHECK(WriteFileAtomic(nested, "second, longer", &err));
    std::string text;
    CHECK(ReadFileUtf8(nested, text));
    CHECK(text == "second, longer");
    CHECK(WriteFileAtomic(nested, "x"));
    CHECK(ReadFileUtf8(nested, text));
    CHECK(text == "x");
    CHECK(CountTempFiles(dir + L"\\a\\b\\c") == 0);

    // Unicode file name and a one megabyte body.
    const std::wstring uni = dir + L"\\Gr\x00FC\x00DF" L"e \x4E2D.txt";
    CHECK(WriteFileAtomic(uni, std::string(1 << 20, 'q')));
    CHECK(ReadFileUtf8(uni, text));
    CHECK(text.size() == (1u << 20));

    RemoveTree(dir);
}

TEST(settings_write_atomic_failures_report_and_clean_up) {
    const std::wstring dir = EdgeDir();
    RemoveTree(dir);
    CHECK(EnsureDirectory(dir));
    unsigned long err = 0;

    // The parent is a file: no folder can be made below it.
    CHECK(WriteFileAtomic(dir + L"\\plain.txt", "x"));
    CHECK(!WriteFileAtomic(dir + L"\\plain.txt\\child.txt", "y", &err));
    CHECK(err != 0);

    // The target is a folder: the rename cannot replace it, the temp file must go away.
    CHECK(EnsureDirectory(dir + L"\\target"));
    err = 0;
    CHECK(!WriteFileAtomic(dir + L"\\target", "z", &err));
    CHECK(err != 0);
    CHECK(CountTempFiles(dir) == 0);

    // Characters Windows does not allow in a name.
    err = 0;
    CHECK(!WriteFileAtomic(dir + L"\\bad?name*.txt", "z", &err));
    CHECK(err != 0);
    // Without an error out-parameter the call still returns false.
    CHECK(!WriteFileAtomic(dir + L"\\plain.txt\\child.txt", "y"));

    // The earlier content of the blocked name is untouched.
    std::string text;
    CHECK(ReadFileUtf8(dir + L"\\plain.txt", text));
    CHECK(text == "x");
    RemoveTree(dir);
}

TEST(settings_ensure_directory_edge_cases) {
    const std::wstring dir = EdgeDir();
    RemoveTree(dir);
    CHECK(EnsureDirectory(dir + L"\\x\\y\\z"));
    // Calling again for an existing folder is fine.
    CHECK(EnsureDirectory(dir + L"\\x\\y\\z"));
    CHECK(EnsureDirectory(dir + L"\\x"));
    // Trailing separator.
    CHECK(EnsureDirectory(dir + L"\\w\\"));
    CHECK((GetFileAttributesW((dir + L"\\w").c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0);
    // Illegal characters and an empty path fail.
    CHECK(!EnsureDirectory(dir + L"\\bad|name"));
    CHECK(!EnsureDirectory(L""));
    RemoveTree(dir);
}

TEST(settings_load_with_a_folder_in_place_of_the_file) {
    // Something that cannot be read as a file must neither be replaced nor turn into a reset to defaults.
    DeleteFileW(SettingsPath().c_str());
    CHECK(CreateDirectoryW(SettingsPath().c_str(), nullptr));
    LoadResult r = LoadOrCreateSettings();
    CHECK(r.readFailed);
    CHECK(r.readError != 0);
    CHECK(!r.created);
    CHECK(SettingsReadFailed());
    // Saving while the read is in doubt writes nothing and reports success.
    unsigned long err = 99;
    CHECK(SaveSettings(Settings{}, &err));
    CHECK(err == 0);
    CHECK((GetFileAttributesW(SettingsPath().c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0);
    // A retry while the folder is still there fails with the same kind of error and keeps the hold.
    Settings fromFile;
    unsigned long retryError = 0;
    CHECK(!RetrySettingsRead(fromFile, &retryError));
    CHECK(retryError != 0);
    CHECK(SettingsReadFailed());
    // With the obstacle gone the retry succeeds and finds no file, so defaults come back and the hold is lifted.
    CHECK(RemoveDirectoryW(SettingsPath().c_str()));
    CHECK(RetrySettingsRead(fromFile, &retryError));
    CHECK(!SettingsReadFailed());
    CHECK(fromFile.enabled && fromFile.mode == Mode::Mute);
    // A second retry with nothing held is a plain success.
    CHECK(RetrySettingsRead(fromFile, nullptr));
    LoadOrCreateSettings();
    DeleteFileW(SettingsPath().c_str());
}

TEST(settings_load_state_resets_between_runs) {
    // The flags describe the last load only: a good file after a newer one clears the newer flag.
    CHECK(WriteFileAtomic(SettingsPath(), "version=9\n"));
    LoadResult r = LoadOrCreateSettings();
    CHECK(r.newerVersion);
    CHECK(SettingsNewerVersion());
    unsigned long err = 5;
    CHECK(SaveSettings(Settings{}, &err));
    CHECK(err == 0);
    std::string text;
    CHECK(ReadFileUtf8(SettingsPath(), text));
    CHECK(text == "version=9\n");
    CHECK(WriteFileAtomic(SettingsPath(), "version=2\nmode=lower\n"));
    r = LoadOrCreateSettings();
    CHECK(!r.newerVersion && !r.readFailed && !r.created);
    CHECK(r.settings.mode == Mode::Lower);
    CHECK(!SettingsNewerVersion());
    CHECK(!SettingsReadFailed());
    DeleteFileW(SettingsPath().c_str());
}

TEST(settings_paths_follow_the_override) {
    const std::wstring dir = DataDir();
    CHECK(!dir.empty());
    CHECK(SettingsPath() == dir + L"\\settings.ini");
    CHECK(StatePath() == dir + L"\\state.ini");
    CHECK(!LogDir().empty());
    // The run works in its own folders, never in the real application data.
    CHECK(dir.find(L"\\Roaming\\AdMutify") == std::wstring::npos);
    CHECK(LogDir().find(L"\\Local\\AdMutify") == std::wstring::npos);
}
