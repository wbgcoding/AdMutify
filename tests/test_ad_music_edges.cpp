// Edge tests for the ad music helpers: file types, name order, folder scan limits and the playlist.
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ad_music.h"
#include "settings.h"
#include "test.h"

namespace {

std::wstring EdgeRoot() { return DataDir() + L"\\music-edge"; }

void RemoveTree(const std::wstring& path) {
    std::wstring doomed = path;
    doomed.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = doomed.c_str();
    op.fFlags = FOF_NO_UI | FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;
    SHFileOperationW(&op);
}

void Touch(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

}  // namespace

TEST(music_edges_file_type_names) {
    // Only the part after the last dot counts, and only when no separator follows it.
    CHECK(IsMusicFile(L".mp3"));
    CHECK(IsMusicFile(L"a.b.c.mp3"));
    CHECK(IsMusicFile(L"C:/dir.d/song.mp3"));
    CHECK(IsMusicFile(L"a b (1).MpEg.mp3"));
    CHECK(!IsMusicFile(L"a.mp3."));
    CHECK(!IsMusicFile(L"a.mp3 "));
    CHECK(!IsMusicFile(L"a.mp3\\"));
    CHECK(!IsMusicFile(L"a.mp3/b"));
    CHECK(!IsMusicFile(L"a.mp3.txt"));
    CHECK(!IsMusicFile(L"a.mp"));
    CHECK(!IsMusicFile(L"a.mp33"));
    CHECK(!IsMusicFile(L"."));
    CHECK(!IsMusicFile(L".."));
    CHECK(!IsMusicFile(L"a.mp3\x00E4"));
    CHECK(!IsMusicFile(std::wstring(L"a.mp3\0x", 7)));
    // A very long name is no problem.
    CHECK(IsMusicFile(std::wstring(5000, L'x') + L".flac"));
    CHECK(!IsMusicFile(std::wstring(5000, L'x')));
}

TEST(music_edges_name_order_is_strict) {
    // A strict weak order: nothing is less than itself, equal ignoring case means neither is less.
    CHECK(!MusicNameLess(L"", L""));
    CHECK(MusicNameLess(L"", L"a"));
    CHECK(!MusicNameLess(L"a", L""));
    CHECK(!MusicNameLess(L"Track 1.mp3", L"Track 1.mp3"));
    CHECK(!MusicNameLess(L"A.mp3", L"a.mp3") || !MusicNameLess(L"a.mp3", L"A.mp3"));
    // Numbers compare by value.
    CHECK(MusicNameLess(L"2.mp3", L"10.mp3"));
    CHECK(!MusicNameLess(L"10.mp3", L"2.mp3"));
    CHECK(MusicNameLess(L"a\\2.mp3", L"a\\10.mp3"));
}

TEST(music_edges_scan_limits_and_oddities) {
    const std::wstring root = EdgeRoot();
    RemoveTree(root);
    EnsureDirectory(root);
    Touch(root + L"\\song.mp3");
    Touch(root + L"\\noext");
    Touch(root + L"\\song.mp3.bak");
    // A folder named like a song is a folder, not a track; its content is still scanned.
    EnsureDirectory(root + L"\\album.mp3");
    Touch(root + L"\\album.mp3\\inner.ogg");

    std::vector<std::wstring> found = ScanMusicFolder(root);
    CHECK(found.size() == 2);
    CHECK(found == std::vector<std::wstring>({root + L"\\album.mp3\\inner.ogg", root + L"\\song.mp3"}));
    // A limit of zero finds nothing; a limit of one stops after one file.
    CHECK(ScanMusicFolder(root, 0).empty());
    CHECK(ScanMusicFolder(root, 1).size() == 1);
    // A file given as the folder, and a folder with a forward slash at the end.
    CHECK(ScanMusicFolder(root + L"\\song.mp3").empty());
    CHECK(ScanMusicFolder(root + L"/").size() == 2);
    RemoveTree(root);
    CHECK(ScanMusicFolder(root).empty());
}

TEST(music_edges_scan_depth_limit) {
    const std::wstring root = EdgeRoot();
    RemoveTree(root);
    // The folder itself is level 0; levels up to 16 are scanned, deeper ones are not.
    std::wstring path = root;
    for (int level = 1; level <= 18; ++level) {
        path += L"\\d";
        EnsureDirectory(path);
        Touch(path + L"\\t" + std::to_wstring(level) + L".mp3");
    }
    std::vector<std::wstring> found = ScanMusicFolder(root);
    CHECK(found.size() == 16);
    bool has16 = false;
    bool has17 = false;
    for (const std::wstring& f : found) {
        if (f.find(L"\\t16.mp3") != std::wstring::npos) has16 = true;
        if (f.find(L"\\t17.mp3") != std::wstring::npos) has17 = true;
    }
    CHECK(has16);
    CHECK(!has17);
    RemoveTree(root);
}

TEST(music_edges_playlist_rounds) {
    Playlist list;
    // Replacing the files starts over at the first track.
    list.Reset({L"a", L"b", L"c"}, false, 1);
    list.Next();
    list.Reset({L"x", L"y"}, false, 1);
    CHECK(list.Size() == 2);
    CHECK(list.Current() == L"x");
    // An emptied list is empty again.
    list.Reset({}, false, 1);
    CHECK(list.Empty());
    CHECK(list.Current().empty());
    CHECK(!list.Next());
    // A single track ends a round on every step.
    list.Reset({L"only"}, false, 1);
    CHECK(list.Next());
    CHECK(list.Next());
    CHECK(list.Current() == L"only");
}

TEST(music_edges_shuffle_never_repeats_across_rounds) {
    for (uint32_t seed : {0u, 1u, 2u, 7u, 0xFFFFFFFFu}) {
        for (size_t count : {2u, 3u, 5u}) {
            std::vector<std::wstring> files;
            for (size_t i = 0; i < count; ++i) files.push_back(L"t" + std::to_wstring(i));
            Playlist list;
            list.Reset(files, true, seed);
            std::wstring previous = list.Current();
            for (int step = 0; step < 60; ++step) {
                list.Next();
                CHECK(list.Current() != previous);
                previous = list.Current();
            }
        }
    }
}

TEST(music_edges_shuffle_is_repeatable_per_seed) {
    std::vector<std::wstring> files;
    for (int i = 0; i < 8; ++i) files.push_back(L"t" + std::to_wstring(i));
    Playlist a;
    Playlist b;
    a.Reset(files, true, 42);
    b.Reset(files, true, 42);
    for (int step = 0; step < 40; ++step) {
        CHECK(a.Current() == b.Current());
        a.Next();
        b.Next();
    }
}

TEST(music_edges_shuffle_remove_keeps_every_other_track) {
    std::vector<std::wstring> files;
    for (int i = 0; i < 6; ++i) files.push_back(L"t" + std::to_wstring(i));
    Playlist list;
    list.Reset(files, true, 5);
    const std::wstring dropped = list.Current();
    list.RemoveCurrent();
    CHECK(list.Size() == 5);
    // One full round plays each remaining track exactly once and never the dropped one.
    std::vector<std::wstring> seen;
    for (int i = 0; i < 5; ++i) {
        CHECK(list.Current() != dropped);
        seen.push_back(list.Current());
        list.Next();
    }
    std::sort(seen.begin(), seen.end());
    CHECK(std::unique(seen.begin(), seen.end()) == seen.end());
}
