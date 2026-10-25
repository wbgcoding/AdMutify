#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "ad_music.h"
#include "app_messages.h"
#include "settings.h"
#include "test.h"

namespace {

std::wstring MusicRoot() { return DataDir() + L"\\music-test"; }

void RemoveTree(const std::wstring& path) {
    std::wstring doomed = path;
    doomed.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = doomed.c_str();
    op.fFlags = FOF_NO_UI | FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;
    SHFileOperationW(&op);
}

void WriteBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(h);
}

// A silent 16-bit mono PCM wave file of the given length.
std::string SilentWav(int ms) {
    const uint32_t rate = 8000;
    const uint32_t bytes = rate * 2 * static_cast<uint32_t>(ms) / 1000;
    std::string out;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    out += "RIFF";
    u32(36 + bytes);
    out += "WAVEfmt ";
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    out += "data";
    u32(bytes);
    out.append(bytes, '\0');
    return out;
}

std::vector<std::wstring> Names(const std::vector<std::wstring>& paths) {
    std::vector<std::wstring> names;
    for (const std::wstring& p : paths) names.push_back(p.substr(p.find_last_of(L'\\') + 1));
    return names;
}

}  // namespace

TEST(ad_music_file_types) {
    CHECK(IsMusicFile(L"song.mp3"));
    CHECK(IsMusicFile(L"C:\\Music\\Song.MP3"));
    CHECK(IsMusicFile(L"a.m4a"));
    CHECK(IsMusicFile(L"a.aac"));
    CHECK(IsMusicFile(L"a.wma"));
    CHECK(IsMusicFile(L"a.wav"));
    CHECK(IsMusicFile(L"a.FLAC"));
    CHECK(IsMusicFile(L"a.ogg"));
    CHECK(IsMusicFile(L"a.OGA"));
    CHECK(IsMusicFile(L"a.opus"));
    CHECK(IsMusicFile(L"a.webm"));
    CHECK(IsMusicFile(L"a.mka"));
    CHECK(IsMusicFile(L"a.mp2"));
    CHECK(!IsMusicFile(L"a.aiff"));
    CHECK(!IsMusicFile(L"clip.mp4"));
    CHECK(!IsMusicFile(L"cover.jpg"));
    CHECK(!IsMusicFile(L"playlist.m3u"));
    CHECK(!IsMusicFile(L"mp3"));
    CHECK(!IsMusicFile(L"folder.mp3\\readme"));
    CHECK(!IsMusicFile(L""));
}

TEST(ad_music_type_lists) {
    CHECK(MusicFilterSpec() == L"*.mp3;*.m4a;*.aac;*.wma;*.wav;*.flac;*.ogg;*.oga;*.opus;*.webm;*.mka;*.mp2");
    CHECK(MusicTypeList() == L"MP3, M4A, AAC, WMA, WAV, FLAC, OGG, OGA, OPUS, WEBM, MKA, MP2");
}

TEST(ad_music_name_order) {
    std::vector<std::wstring> names = {L"Track 10.mp3", L"track 2.mp3", L"Track 1.mp3", L"b.mp3", L"A.mp3"};
    std::sort(names.begin(), names.end(), MusicNameLess);
    const std::vector<std::wstring> want = {L"A.mp3", L"b.mp3", L"Track 1.mp3", L"track 2.mp3", L"Track 10.mp3"};
    CHECK(names == want);
}

TEST(ad_music_playlist_order) {
    Playlist list;
    CHECK(list.Empty());
    CHECK(list.Current().empty());
    CHECK(!list.Next());
    list.Reset({L"a", L"b", L"c"}, false, 7);
    CHECK(list.Size() == 3);
    CHECK(list.Current() == L"a");
    CHECK(!list.Next());
    CHECK(list.Current() == L"b");
    CHECK(!list.Next());
    CHECK(list.Current() == L"c");
    CHECK(list.Next());
    CHECK(list.Current() == L"a");
}

TEST(ad_music_playlist_shuffle_plays_each_once_per_round) {
    std::vector<std::wstring> files;
    for (int i = 0; i < 20; ++i) files.push_back(std::to_wstring(i));
    Playlist list;
    list.Reset(files, true, 12345);
    std::wstring last;
    bool someOrderDiffers = false;
    for (int round = 0; round < 5; ++round) {
        std::set<std::wstring> seen;
        for (size_t i = 0; i < files.size(); ++i) {
            if (i == 0 && round > 0) CHECK(list.Current() != last);
            if (list.Current() != files[i]) someOrderDiffers = true;
            seen.insert(list.Current());
            last = list.Current();
            const bool wrapped = list.Next();
            CHECK(wrapped == (i + 1 == files.size()));
        }
        CHECK(seen.size() == files.size());
    }
    CHECK(someOrderDiffers);
}

TEST(ad_music_playlist_shuffle_single_track) {
    Playlist list;
    list.Reset({L"only"}, true, 0);
    CHECK(list.Current() == L"only");
    CHECK(list.Next());
    CHECK(list.Current() == L"only");
}

TEST(ad_music_playlist_remove_current) {
    Playlist list;
    list.Reset({L"a", L"b", L"c"}, false, 1);
    list.Next();
    list.RemoveCurrent();
    CHECK(list.Size() == 2);
    CHECK(list.Current() == L"c");
    list.RemoveCurrent();
    CHECK(list.Current() == L"a");
    list.RemoveCurrent();
    CHECK(list.Empty());
    CHECK(list.Current().empty());
    list.RemoveCurrent();
    CHECK(list.Empty());
}

TEST(ad_music_scan_folder) {
    const std::wstring root = MusicRoot();
    RemoveTree(root);
    EnsureDirectory(root);
    EnsureDirectory(root + L"\\Album 2");
    EnsureDirectory(root + L"\\Album 10");
    EnsureDirectory(root + L"\\.hidden");
    SetFileAttributesW((root + L"\\.hidden").c_str(), FILE_ATTRIBUTE_HIDDEN);
    WriteBytes(root + L"\\b.mp3", "x");
    WriteBytes(root + L"\\a.wav", "x");
    WriteBytes(root + L"\\cover.jpg", "x");
    WriteBytes(root + L"\\Album 10\\01.flac", "x");
    WriteBytes(root + L"\\Album 2\\01.m4a", "x");
    WriteBytes(root + L"\\.hidden\\secret.mp3", "x");
    WriteBytes(root + L"\\h\u00e4ndel.mp3", "x");

    std::vector<std::wstring> found = ScanMusicFolder(root);
    const std::vector<std::wstring> want = {root + L"\\a.wav", root + L"\\Album 2\\01.m4a", root + L"\\Album 10\\01.flac",
                                            root + L"\\b.mp3", root + L"\\h\u00e4ndel.mp3"};
    CHECK(found == want);
    CHECK(ScanMusicFolder(root, 2).size() == 2);
    CHECK(ScanMusicFolder(root + L"\\").size() == want.size());
    CHECK(ScanMusicFolder(root + L"\\missing").empty());
    CHECK(ScanMusicFolder(L"").empty());
    CHECK(Names(ScanMusicFolder(root + L"\\Album 2")) == std::vector<std::wstring>{L"01.m4a"});
    RemoveTree(root);
}

TEST(ad_music_settings_roundtrip) {
    Settings s;
    CHECK(s.adMusic == AdMusic::Off);
    CHECK(s.adMusicPath.empty());
    CHECK(s.adMusicVolume == 50);
    for (AdMusic m : {AdMusic::Off, AdMusic::File, AdMusic::Shuffle, AdMusic::Order}) {
        s.adMusic = m;
        Settings back;
        CHECK(ParseSettings(FormatSettings(s), back));
        CHECK(back.adMusic == m);
    }
    s.adMusicPath = L"D:\\Musik\\Pausen \u00fc \u65e5\u672c\\";
    s.adMusicVolume = 35;
    Settings back;
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.adMusicPath == s.adMusicPath);
    CHECK(back.adMusicVolume == 35);

    // Long paths do not fit the fixed part of the file and must survive whole.
    s.adMusicPath = L"C:\\" + std::wstring(600, L'x') + L"\\song.mp3";
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.adMusicPath == s.adMusicPath);

    // A value with a line break would split the file; it is written empty instead.
    s.adMusicPath = L"C:\\a\r\nmode=lower";
    CHECK(ParseSettings(FormatSettings(s), back));
    CHECK(back.adMusicPath.empty());
    CHECK(back.mode == s.mode);
}

TEST(ad_music_settings_parse_values) {
    Settings back;
    CHECK(ParseSettings("version=1\nad_music=bogus\nad_music_volume=250\n", back));
    CHECK(back.adMusic == AdMusic::Off);
    CHECK(back.adMusicVolume == 100);
    CHECK(ParseSettings("version=1\nad_music=order\nad_music_volume=-4\nad_music_path= E:\\Music \n", back));
    CHECK(back.adMusic == AdMusic::Order);
    CHECK(back.adMusicVolume == 0);
    CHECK(back.adMusicPath == L"E:\\Music");
    CHECK(ParseSettings("version=1\nad_music_volume=43\n", back));
    CHECK(back.adMusicVolume == 45);
    CHECK(ParseSettings("version=1\nad_music_volume=abc\n", back));
    CHECK(back.adMusicVolume == 50);
    CHECK(ClampVolumePercent(101) == 100);
    CHECK(ClampVolumePercent(97) == 95);
    CHECK(ClampVolumePercent(98) == 100);
}

TEST(ad_music_settings_merge) {
    Settings file;
    file.adMusicPath = L"F:\\from-file";
    Settings shown;
    Settings current = shown;
    current.adMusic = AdMusic::Shuffle;
    current.adMusicVolume = 80;
    Settings out = MergeSettingsChange(file, shown, current);
    CHECK(out.adMusic == AdMusic::Shuffle);
    CHECK(out.adMusicVolume == 80);
    CHECK(out.adMusicPath == L"F:\\from-file");
    current = shown;
    current.adMusicPath = L"G:\\new";
    out = MergeSettingsChange(file, shown, current);
    CHECK(out.adMusicPath == L"G:\\new");
}

// Real playback through Media Foundation at volume 0: opening, moving on at a track end, skipping a broken file,
// fading out and continuing at the kept position.
namespace {

AdMusicPlayer* g_player = nullptr;

LRESULT CALLBACK PlayerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_APP_MUSIC && g_player) {
        g_player->OnEvent(wParam, lParam);
        return 0;
    }
    if (msg == WM_TIMER && g_player) {
        g_player->OnTimer();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

template <class Pred>
bool PumpUntil(Pred done, DWORD timeoutMs) {
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < end) {
        if (done()) return true;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    return done();
}

}  // namespace

TEST(ad_music_player_plays_resumes_and_skips) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring root = MusicRoot();
    RemoveTree(root);
    EnsureDirectory(root);
    WriteBytes(root + L"\\1 broken.mp3", "this is not audio");
    WriteBytes(root + L"\\2 short.wav", SilentWav(700));
    WriteBytes(root + L"\\3 long.wav", SilentWav(8000));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &PlayerWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AdMutifyTest.Music";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    CHECK(hwnd != nullptr);

    {
        AdMusicPlayer player;
        g_player = &player;
        player.Init(hwnd, 9);
        player.Configure(AdMusic::Order, root, 0);
        player.SetActive(true);
        // The broken file is dropped, the short one plays and its end moves on to the long one.
        CHECK(PumpUntil([&] { return player.IsPlaying(); }, 5000));
        CHECK(player.TrackCount() == 2);
        CHECK(PumpUntil([&] { return player.IsPlaying() && player.Position() > 1.0; }, 6000));

        // The ad ends: the music fades out, lets the engine go and keeps its spot.
        player.SetActive(false);
        CHECK(PumpUntil([&] { return !player.IsPlaying(); }, 3000));
        const double kept = player.Position();
        CHECK(kept > 1.0);

        // The next ad continues there.
        player.SetActive(true);
        CHECK(PumpUntil([&] { return player.IsPlaying(); }, 5000));
        CHECK(player.Position() >= kept - 0.3);

        // A new folder starts over; switching off stops at once.
        player.Configure(AdMusic::Off, root, 0);
        CHECK(!player.IsPlaying());
        CHECK(player.Position() == 0);
        player.Shutdown();
        g_player = nullptr;
    }

    {
        // A single file that cannot be played: nothing plays and the next ad tries again.
        AdMusicPlayer player;
        g_player = &player;
        player.Init(hwnd, 9);
        player.Configure(AdMusic::File, root + L"\\1 broken.mp3", 0);
        player.SetActive(true);
        PumpUntil([] { return false; }, 1500);
        CHECK(!player.IsPlaying());
        player.SetActive(false);
        player.Configure(AdMusic::File, root + L"\\missing.mp3", 0);
        player.SetActive(true);
        CHECK(!player.IsPlaying());
        player.Shutdown();
        g_player = nullptr;
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    RemoveTree(root);
    if (SUCCEEDED(co)) CoUninitialize();
}
