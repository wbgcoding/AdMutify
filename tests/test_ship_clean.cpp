#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "settings.h"
#include "test.h"

namespace {

// Em and en dash, as bytes. Every other needle (tool names, developer path and mail fragments) lives in the
// untracked file .shipclean-needles next to the sources, so the tracked repo never spells them out.
std::vector<std::string> DashNeedles() { return {"\xE2\x80\x94", "\xE2\x80\x93"}; }

std::string LowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// One needle per line, '#' starts a comment line; a UTF-8 byte order mark at the start is ignored.
std::vector<std::string> ParseNeedles(std::string_view text) {
    std::vector<std::string> out;
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        out.push_back(LowerAscii(line));
    }
    return out;
}

// Empty when the file does not exist.
std::vector<std::string> LocalNeedles(const std::wstring& root) {
    std::string text;
    if (!ReadFileUtf8(root + L"\\.shipclean-needles", text)) return {};
    return ParseNeedles(text);
}

// Returns the needles found in text (case-insensitive for ASCII).
std::vector<std::string> ShipCleanHits(std::string_view text, const std::vector<std::string>& needles) {
    const std::string low = LowerAscii(text);
    std::vector<std::string> hits;
    for (const std::string& n : needles)
        if (low.find(n) != std::string::npos) hits.push_back(n);
    return hits;
}

std::vector<std::string> TrackedFiles(const std::wstring& root) {
    std::vector<std::string> files;
    std::wstring cmd = L"git -C \"" + root + L"\" ls-files";
    FILE* p = _wpopen(cmd.c_str(), L"rt");
    if (!p) return files;
    char line[1024];
    while (fgets(line, sizeof(line), p)) {
        std::string f = line;
        while (!f.empty() && (f.back() == '\n' || f.back() == '\r')) f.pop_back();
        if (!f.empty()) files.push_back(f);
    }
    _pclose(p);
    return files;
}

bool IsBinary(const std::string& data) { return data.find('\0') != std::string::npos; }

}  // namespace

TEST(ship_clean_detects_planted_lines) {
    std::vector<std::string> needles = DashNeedles();
    needles.push_back("forbidden");
    CHECK(ShipCleanHits("Plain text without findings.", needles).empty());
    CHECK(!ShipCleanHits("\xE2\x80\x94 at line start", needles).empty());
    CHECK(!ShipCleanHits("mid \xE2\x80\x93 sentence", needles).empty());
    CHECK(!ShipCleanHits("A Forbidden word", needles).empty());
    CHECK(ShipCleanHits("Nothing to see", {}).empty());
}

TEST(ship_clean_needle_file_parsing) {
    // BOM, comment line, CRLF endings, mixed case and blank lines: only the real needles remain, lower-cased.
    const std::vector<std::string> got = ParseNeedles("\xEF\xBB\xBF" "First\r\n# comment\r\n\r\nSecond  \r\nthird");
    CHECK(got.size() == 3);
    if (got.size() == 3) {
        CHECK(got[0] == "first");
        CHECK(got[1] == "second");
        CHECK(got[2] == "third");
    }
    CHECK(ParseNeedles("# only a comment\n").empty());
    CHECK(ParseNeedles("").empty());
}

TEST(ship_clean_local_needle_file_is_optional) {
    // A folder without the file gives no local needles; the dash checks still run.
    CHECK(LocalNeedles(L"C:\\does-not-exist-shipclean").empty());
}

TEST(ship_clean_tracked_files) {
    const std::wstring root = ADMUTIFY_SOURCE_DIR;
    const std::vector<std::string> files = TrackedFiles(root);
    CHECK(files.size() >= 20);
    std::vector<std::string> needles = DashNeedles();
    const std::vector<std::string> local = LocalNeedles(root);
    if (local.empty()) std::printf("note: .shipclean-needles not found, only the dash checks ran\n");
    needles.insert(needles.end(), local.begin(), local.end());
    bool sawReadme = false, sawChangelog = false;
    for (const std::string& f : files) {
        if (f == "README.md") sawReadme = true;
        if (f == "CHANGELOG.md") sawChangelog = true;
        std::string data;
        std::wstring path = root + L"\\" + std::wstring(f.begin(), f.end());
        if (!ReadFileUtf8(path, data) || IsBinary(data)) continue;
        const std::vector<std::string> hits = ShipCleanHits(data, needles);
        if (!hits.empty()) std::printf("ship-clean finding in %s\n", f.c_str());
        CHECK(hits.empty());
    }
    CHECK(sawReadme);
    CHECK(sawChangelog);
}
