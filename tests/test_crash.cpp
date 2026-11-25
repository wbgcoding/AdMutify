#include <windows.h>

#include <string>

#include "crash.h"
#include "settings.h"
#include "test.h"
#include "test_helpers.h"

TEST_FILE_GROUP(Logic);

namespace {

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

void CleanLogs() {
    for (const wchar_t* suffix : {L"", L".1", L".2", L".3", L".4"}) DeleteFileW((LogDir() + L"\\admutify.log" + suffix).c_str());
}

}  // namespace

TEST(log_rollover_keeps_four_files) {
    CleanLogs();
    SetLogLimitForTests(1024);
    for (int i = 0; i < 200; ++i) Log("state NoSpotify -> Idle line %d with some padding text to fill the file", i);
    SetLogLimitForTests(0);

    std::wstring base = LogDir() + L"\\admutify.log";
    CHECK(Exists(base));
    CHECK(Exists(base + L".1"));
    CHECK(Exists(base + L".2"));
    CHECK(Exists(base + L".3"));
    CHECK(!Exists(base + L".4"));

    for (const wchar_t* suffix : {L"", L".1", L".2", L".3"}) {
        std::string data;
        CHECK(ReadFileUtf8(base + suffix, data));
        CHECK(data.size() <= 1024 + 200);
    }
    CleanLogs();
}

TEST(log_last_lines) {
    CleanLogs();
    for (int i = 0; i < 80; ++i) Log("line %d", i);
    std::string tail = LastLogLines(50);
    CHECK(tail.find("line 79") != std::string::npos);
    CHECK(tail.find("line 30") != std::string::npos);
    CHECK(tail.find("line 29") == std::string::npos);
    CleanLogs();
}

TEST(crash_flag_follows_the_file) {
    // The flag is the file the crash path writes: present after a crash, gone once it was handled.
    const std::wstring flag = LogDir() + L"\\crashed.flag";
    EnsureDirectory(LogDir());
    DeleteFileW(flag.c_str());
    CHECK(!CrashFlagExists());
    CHECK(WriteFileAtomic(flag, "restored=1\r\n"));
    CHECK(CrashFlagExists());
    DeleteFileW(flag.c_str());
    CHECK(!CrashFlagExists());
}

TEST(crash_flag_text_decides_the_dialog_body) {
    CHECK(CrashFlagMeansNotRestored("restored=0\r\n"));
    CHECK(!CrashFlagMeansNotRestored("restored=1\r\n"));
    // Older versions wrote an empty marker; anything unknown keeps the plain text.
    CHECK(!CrashFlagMeansNotRestored(""));
    CHECK(!CrashFlagMeansNotRestored("garbage"));
}

TEST(crash_startup_plan_table) {
    // Nothing left over: nothing to show.
    CrashStartupPlan p = PlanCrashStartup(false, false);
    CHECK(!p.showSeenNow && !p.deferSeen && !p.deferFlag);
    // A fresh crash waits for the dialog after the volume restore.
    p = PlanCrashStartup(false, true);
    CHECK(!p.showSeenNow && !p.deferSeen && p.deferFlag);
    // A report the last start never showed (no new crash) also waits; it used to block the restore.
    p = PlanCrashStartup(true, false);
    CHECK(!p.showSeenNow && p.deferSeen && !p.deferFlag);
    // The last start crashed again before its dialog: the old report at once, the new one later.
    p = PlanCrashStartup(true, true);
    CHECK(p.showSeenNow && !p.deferSeen && p.deferFlag);
}

TEST(tail_before_last_start_table) {
    const std::string run1 =
        "2026-09-01 10:00:00.000 start version=1\r\n2026-09-01 10:00:01.000 state a\r\n2026-09-01 10:00:02.000 state b\r\n";
    const std::string run2 = "2026-09-01 11:00:00.000 start version=1\r\n2026-09-01 11:00:01.000 restore sessions=1\r\n";
    // The crashed run's lines end before this run's start line; this run's own lines stay out.
    CHECK(TailBeforeLastStart(run1 + run2, 50) == run1);
    CHECK(TailBeforeLastStart(run1 + run2, 2) ==
          "2026-09-01 10:00:01.000 state a\r\n2026-09-01 10:00:02.000 state b\r\n");
    // Only this run in the file: nothing before its start line.
    CHECK(TailBeforeLastStart(run2, 50).empty());
    // No start line at all: the last n lines.
    CHECK(TailBeforeLastStart("x1\r\nx2\r\nx3\r\n", 2) == "x2\r\nx3\r\n");
    CHECK(TailBeforeLastStart("", 5).empty());
}

TEST(copied_details_drop_trace_lines) {
    const std::string text =
        "2026-09-01 10:00:00.000 start version=1\r\n2026-09-01 10:00:01.000 trace title kind=0 \"a - b\"\r\n"
        "2026-09-01 10:00:02.000 state Idle -> Music\r\n2026-09-01 10:00:03.000 trace media_exe \"x.exe\"\r\n";
    CHECK(DropTraceLines(text) == "2026-09-01 10:00:00.000 start version=1\r\n2026-09-01 10:00:02.000 state Idle -> Music\r\n");
    CHECK(DropTraceLines("").empty());
    CHECK(DropTraceLines("short\r\n") == "short\r\n");
}

TEST(copied_details_drop_continuation_of_trace_lines) {
    // A line without a timestamp belongs to the line before it, so a traced title with a line break cannot leak.
    const std::string text =
        "2026-09-01 10:00:00.000 start version=1\r\n2026-09-01 10:00:01.000 trace title \"a\r\nsecret part\"\r\n"
        "2026-09-01 10:00:02.000 state Idle -> Music\r\ncontinued state text\r\n";
    CHECK(DropTraceLines(text) ==
          "2026-09-01 10:00:00.000 start version=1\r\n2026-09-01 10:00:02.000 state Idle -> Music\r\ncontinued state text\r\n");
}

TEST(log_lines_stay_single_lines) {
    CleanLogs();
    Log("trace title \"a\nsecret\r\npart\"");
    Log("state next");
    std::string data;
    CHECK(ReadFileUtf8(LogDir() + L"\\admutify.log", data));
    size_t lines = 0;
    for (char c : data)
        if (c == '\n') ++lines;
    CHECK(lines == 2);
    CHECK(data.find("trace title \"a secret  part\"") != std::string::npos);
    CHECK(DropTraceLines(data).find("secret") == std::string::npos);
    CleanLogs();
}

TEST(tail_before_an_earlier_start) {
    const std::string a = "2026-09-01 09:00:00.000 start version=1\r\n2026-09-01 09:00:01.000 state a\r\n";
    const std::string b = "2026-09-01 10:00:00.000 start version=1\r\n2026-09-01 10:00:01.000 state b\r\n";
    const std::string c = "2026-09-01 11:00:00.000 start version=1\r\n";
    // runsBack 1: the run before the one that came before this start.
    CHECK(TailBeforeLastStart(a + b + c, 50, 1) == a);
    CHECK(TailBeforeLastStart(a + b + c, 50, 0) == a + b);
    // Fewer starts than asked for: the oldest start line ends the tail.
    CHECK(TailBeforeLastStart(b + c, 50, 3) == "");
    CHECK(TailBeforeLastStart(a + b + c, 1, 1) == "2026-09-01 09:00:01.000 state a\r\n");
}

TEST(log_drop_lines_before_cutoff) {
    const std::string text =
        "2026-09-01 10:00:00.000 start\r\n2026-09-02 11:00:00.000 exit\r\n2026-09-09 08:00:00.000 start\r\n";
    CHECK(DropLogLinesBefore(text, "2026-09-02 11:00:00") ==
          "2026-09-02 11:00:00.000 exit\r\n2026-09-09 08:00:00.000 start\r\n");
    CHECK(DropLogLinesBefore(text, "2026-09-05 00:00:00") == "2026-09-09 08:00:00.000 start\r\n");
    CHECK(DropLogLinesBefore(text, "2026-08-01 00:00:00") == text);
    CHECK(DropLogLinesBefore(text, "2026-10-01 00:00:00").empty());
}

TEST(log_cutoff_ignores_lines_without_a_timestamp) {
    // A line that does not start with a digit belongs to the line before it: dropped or kept with it.
    const std::string text =
        "2026-09-01 10:00:00.000 old\r\nstray text after old\r\n2026-09-09 08:00:00.000 new\r\ncontinued after new\r\n";
    CHECK(DropLogLinesBefore(text, "2026-09-05 00:00:00") == "2026-09-09 08:00:00.000 new\r\ncontinued after new\r\n");
    CHECK(DropLogLinesBefore(text, "2026-08-01 00:00:00") == text);
    CHECK(DropLogLinesBefore(text, "2026-10-01 00:00:00").empty());
}

TEST(log_prune_removes_month_old_files_and_lines) {
    CleanLogs();
    const std::wstring dir = LogDir();
    const std::wstring base = dir + L"\\admutify.log";
    CHECK(WriteFileAtomic(base, "2020-01-01 10:00:00.000 old line\r\n2999-01-01 10:00:00.000 new line\r\n"));
    CHECK(WriteFileAtomic(base + L".1", "old rotated\r\n"));
    CHECK(WriteFileAtomic(base + L".2", "fresh rotated\r\n"));
    CHECK(WriteFileAtomic(dir + L"\\crash-20200101-000000.dmp", "x"));
    CHECK(WriteFileAtomic(dir + L"\\crash-29990101-000000.dmp", "x"));

    // Kept for 30 days: one day past that goes, one day short of it stays.
    CHECK(kKeepLogDays == 30);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG nowV = static_cast<ULONGLONG>(now.dwHighDateTime) << 32 | now.dwLowDateTime;
    const ULONGLONG day = 24ULL * 60 * 60 * 10000000;
    auto setAge = [&](const std::wstring& p, ULONGLONG days) {
        const ULONGLONG v = nowV - days * day;
        FILETIME t{static_cast<DWORD>(v), static_cast<DWORD>(v >> 32)};
        HANDLE h = CreateFileW(p.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        CHECK(SetFileTime(h, nullptr, nullptr, &t));
        CloseHandle(h);
    };
    setAge(base + L".1", 31);
    setAge(dir + L"\\crash-20200101-000000.dmp", 31);
    setAge(base + L".2", 29);
    setAge(dir + L"\\crash-29990101-000000.dmp", 29);

    CHECK(PruneOldLogs());

    std::string data;
    CHECK(ReadFileUtf8(base, data));
    CHECK(data == "2999-01-01 10:00:00.000 new line\r\n");
    CHECK(!Exists(base + L".1"));
    CHECK(Exists(base + L".2"));
    CHECK(!Exists(dir + L"\\crash-20200101-000000.dmp"));
    CHECK(Exists(dir + L"\\crash-29990101-000000.dmp"));
    DeleteFileW((dir + L"\\crash-29990101-000000.dmp").c_str());
    CleanLogs();
}
