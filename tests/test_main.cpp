#include "test.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "autostart.h"
#include "settings.h"
#include "version.h"

static int g_failures = 0;
static int g_checkFailuresInTest = 0;

std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void ReportFailure(const char* expr, const char* file, int line) {
    ++g_failures;
    ++g_checkFailuresInTest;
    std::printf("  FAIL %s:%d: %s\n", file, line, expr);
}

TEST(version_defined) {
    CHECK(sizeof(ADMUTIFY_VERSION_STR) > 1);
}

int main() {
    std::wstring testData = ADMUTIFY_TEST_DATA_DIR;
    std::replace(testData.begin(), testData.end(), L'/', L'\\');
    // One folder per process, so two test runs at the same time do not clobber each other's files.
    testData += L"-" + std::to_wstring(GetCurrentProcessId());
    SetAutostartKeyOverride(L"Software\\AdMutifyTest\\Run");
    SetDataDirOverride(testData);
    SetLogDirOverride(testData + L"\\logs");

    int run = 0;
    for (const TestCase& t : Registry()) {
        g_checkFailuresInTest = 0;
        t.fn();
        ++run;
        std::printf("%s %s\n", g_checkFailuresInTest ? "FAIL" : "ok  ", t.name);
    }
    // Remove this run's data folder (the shell operation wants a double-null terminated path).
    std::wstring doomed = testData;
    doomed.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = doomed.c_str();
    op.fFlags = FOF_NO_UI | FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;
    SHFileOperationW(&op);
    std::printf("%d tests, %d failed checks\n", run, g_failures);
    return g_failures ? 1 : 0;
}
