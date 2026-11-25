#include "test.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "autostart.h"
#include "settings.h"
#include "version.h"

static int g_failures = 0;
static int g_checkFailuresInTest = 0;
// Every failed check, repeated at the end so it stays readable after a long run scrolled it away.
static std::vector<std::string> g_failureLines;
static const char* g_currentTest = "";

std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void ReportFailure(const char* expr, const char* file, int line) {
    ++g_failures;
    ++g_checkFailuresInTest;
    std::printf("  FAIL %s:%d: %s\n", file, line, expr);
    g_failureLines.push_back(std::string(g_currentTest) + "  " + file + ":" + std::to_string(line) + ": " + expr);
}

TEST_FILE_GROUP(Logic);

TEST(version_defined) {
    CHECK(sizeof(ADMUTIFY_VERSION_STR) > 1);
}

namespace {

const char* GroupName(TestGroup g) {
    switch (g) {
        case TestGroup::Logic: return "logic";
        case TestGroup::Ui: return "ui";
        case TestGroup::Integration: return "integration";
    }
    return "";
}

// Tests slower than this are listed with their time, so a slow one stands out.
constexpr long long kSlowTestMs = 200;

int Usage() {
    std::printf("Usage: admutify_tests [--group logic|ui|integration]... [--list]\n");
    return 2;
}

long long MsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> groups;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--group") == 0 && i + 1 < argc) {
            groups.emplace_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--list") == 0) {
            list = true;
        } else {
            return Usage();
        }
    }
    for (const std::string& g : groups)
        if (g != "logic" && g != "ui" && g != "integration") return Usage();
    auto selected = [&](const TestCase& t) {
        return groups.empty() || std::find(groups.begin(), groups.end(), GroupName(t.group)) != groups.end();
    };
    if (list) {
        for (const TestCase& t : Registry())
            if (selected(t)) std::printf("%-12s %s\n", GroupName(t.group), t.name);
        return 0;
    }
    // A test run must never slow the desktop down.
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    std::wstring testData = ADMUTIFY_TEST_DATA_DIR;
    std::replace(testData.begin(), testData.end(), L'/', L'\\');
    // One folder per process, so two test runs at the same time do not clobber each other's files.
    testData += L"-" + std::to_wstring(GetCurrentProcessId());
    SetAutostartKeyOverride(L"Software\\AdMutifyTest\\Run");
    SetDataDirOverride(testData);
    SetLogDirOverride(testData + L"\\logs");

    const auto runStart = std::chrono::steady_clock::now();
    int run = 0;
    for (const TestCase& t : Registry()) {
        if (!selected(t)) continue;
        g_checkFailuresInTest = 0;
        g_currentTest = t.name;
        const auto start = std::chrono::steady_clock::now();
        t.fn();
        const long long ms = MsSince(start);
        ++run;
        const char* result = g_checkFailuresInTest ? "FAIL" : "ok  ";
        if (ms >= kSlowTestMs)
            std::printf("%s %s (%lld ms)\n", result, t.name, ms);
        else
            std::printf("%s %s\n", result, t.name);
    }
    const long long totalMs = MsSince(runStart);
    // Remove this run's data folder (the shell operation wants a double-null terminated path).
    std::wstring doomed = testData;
    doomed.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = doomed.c_str();
    op.fFlags = FOF_NO_UI | FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;
    SHFileOperationW(&op);
    if (!g_failureLines.empty()) {
        std::printf("\nFailed checks:\n");
        for (const std::string& f : g_failureLines) std::printf("  %s\n", f.c_str());
    }
    std::printf("%d tests, %d failed checks, %lld ms\n", run, g_failures, totalMs);
    return g_failures ? 1 : 0;
}
