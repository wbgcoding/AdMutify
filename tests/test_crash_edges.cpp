// Edge and error-path tests for the pure log helpers and the log file readers in crash.h.
#include <windows.h>

#include <string>

#include "crash.h"
#include "settings.h"
#include "test.h"
#include "util.h"

namespace {

void CleanLogs() {
    for (const wchar_t* suffix : {L"", L".1", L".2", L".3"}) DeleteFileW((LogDir() + L"\\admutify.log" + suffix).c_str());
}

bool PutLog(const wchar_t* suffix, const std::string& text) {
    EnsureDirectory(LogDir());
    return WriteFileAtomic(LogDir() + L"\\admutify.log" + suffix, text);
}

}  // namespace

TEST(crash_drop_trace_lines_edges) {
    const std::string p = "2026-09-01 10:00:00.000 ";
    // Only a message that starts with "trace " is dropped.
    CHECK(DropTraceLines(p + "trace x\r\n").empty());
    CHECK(DropTraceLines(p + "trace\r\n") == p + "trace\r\n");
    CHECK(DropTraceLines(p + "TRACE x\r\n") == p + "TRACE x\r\n");
    CHECK(DropTraceLines(p + "state trace x\r\n") == p + "state trace x\r\n");
    // A line with nothing after the prefix, or shorter than the prefix, stays.
    CHECK(DropTraceLines(p + "\r\n") == p + "\r\n");
    CHECK(DropTraceLines("trace x\r\n") == "trace x\r\n");
    // The last line may lack a line break; LF-only breaks work as well.
    CHECK(DropTraceLines(p + "keep\r\n" + p + "trace x").size() == (p + "keep\r\n").size());
    CHECK(DropTraceLines(p + "keep\n" + p + "trace x\n" + p + "keep2\n") == p + "keep\n" + p + "keep2\n");
    // Only trace lines: nothing is left.
    CHECK(DropTraceLines(p + "trace a\r\n" + p + "trace b\r\n").empty());
}

TEST(crash_drop_lines_before_edges) {
    const std::string text = "2026-09-02 11:00:00.000 a\r\n2026-09-03 12:00:00.000 b";
    CHECK(DropLogLinesBefore("", "2026-09-01 00:00:00").empty());
    // The cutoff second itself stays; one second later drops the line.
    CHECK(DropLogLinesBefore(text, "2026-09-02 11:00:00") == text);
    CHECK(DropLogLinesBefore(text, "2026-09-02 11:00:01") == "2026-09-03 12:00:00.000 b");
    // A last line without a line break is kept as it is.
    CHECK(DropLogLinesBefore(text, "2026-09-03 12:00:00") == "2026-09-03 12:00:00.000 b");
    CHECK(DropLogLinesBefore(text, "2026-09-03 12:00:01").empty());
    // Text without any timestamped line is dropped; leading junk before the first stamped line goes too.
    CHECK(DropLogLinesBefore("junk\r\nmore junk", "2026-01-01 00:00:00").empty());
    CHECK(DropLogLinesBefore("junk\r\n" + text, "2026-01-01 00:00:00") == text);
    // An empty cutoff keeps everything from the first stamped line.
    CHECK(DropLogLinesBefore(text, "") == text);
}

TEST(crash_tail_before_last_start_edges) {
    const std::string s1 = "2026-09-01 10:00:00.000 start version=1\r\n";
    const std::string a = "2026-09-01 10:00:01.000 state a\r\n";
    const std::string b = "2026-09-01 10:00:02.000 state b\r\n";
    const std::string s2 = "2026-09-01 11:00:00.000 start version=1\r\n";
    const std::string c = "2026-09-01 11:00:01.000 restore\r\n";
    const std::string s3 = "2026-09-01 12:00:00.000 start version=2\r\n";

    // Several starts: only the last one cuts.
    CHECK(TailBeforeLastStart(s1 + a + b + s2 + c + s3, 50) == s1 + a + b + s2 + c);
    CHECK(TailBeforeLastStart(s1 + a + b + s2 + c + s3, 1) == c);
    CHECK(TailBeforeLastStart(s1 + a + b + s2 + c + s3, 2) == s2 + c);
    // The start line is the very last line: the whole earlier run is the tail.
    CHECK(TailBeforeLastStart(s1 + a + s2, 50) == s1 + a);
    // n larger than the number of lines gives all of them.
    CHECK(TailBeforeLastStart(a + b + s2, 1000) == a + b);
    // A start line without a line break at the end of the text counts as a start line.
    CHECK(TailBeforeLastStart(a + "2026-09-01 11:00:00.000 start version=1", 50) == a);
    CHECK(TailBeforeLastStart("2026-09-01 11:00:00.000 start version=1", 50).empty());
    // The marker must follow the 24 character prefix exactly.
    CHECK(TailBeforeLastStart(a + "2026-09-01 11:00:00.000 restart version=1\r\n", 1) ==
          "2026-09-01 11:00:00.000 restart version=1\r\n");
    CHECK(TailBeforeLastStart(a + "2026-09-01 11:00:00.000 start version\r\n", 1) ==
          "2026-09-01 11:00:00.000 start version\r\n");
    CHECK(TailBeforeLastStart(a + "start version=1\r\n", 1) == "start version=1\r\n");
    // LF-only line breaks and a last line without a break.
    const std::string lf = "2026-09-01 10:00:00.000 start version=1\nx\n2026-09-01 11:00:00.000 start version=1\ny\n";
    CHECK(TailBeforeLastStart(lf, 5) == "2026-09-01 10:00:00.000 start version=1\nx\n");
    CHECK(TailBeforeLastStart(lf, 1) == "x\n");
    CHECK(TailBeforeLastStart("x1\r\nx2", 1) == "x2");
    CHECK(TailBeforeLastStart("x1\r\nx2", 2) == "x1\r\nx2");
}

TEST(crash_flag_text_edges) {
    CHECK(CrashFlagMeansNotRestored("restored=0"));
    CHECK(CrashFlagMeansNotRestored("restored=1\r\nrestored=0\r\n"));
    CHECK(CrashFlagMeansNotRestored(std::string_view("a\0restored=0", 12)));
    CHECK(!CrashFlagMeansNotRestored("RESTORED=0"));
    CHECK(!CrashFlagMeansNotRestored("restored=10"));
    CHECK(!CrashFlagMeansNotRestored("restored = 0"));
    CHECK(!CrashFlagMeansNotRestored("restored="));
    CHECK(!CrashFlagMeansNotRestored("\r\n"));
    CHECK(!CrashFlagMeansNotRestored(std::string_view()));
}

TEST(crash_log_line_is_cut_not_lost) {
    CleanLogs();
    // A message beyond the line buffer is shortened; the line stays whole and ends with a line break.
    const std::string huge(5000, 'x');
    Log("%s", huge.c_str());
    std::string last = LastLogLines(1);
    CHECK(last.size() > 1000 && last.size() < 1100);
    CHECK(last.size() >= 2 && last.compare(last.size() - 2, 2, "\r\n") == 0);
    CHECK(last.find("xxxxxxxxxx") != std::string::npos);
    // An empty message leaves the timestamp prefix alone.
    Log("%s", "");
    CHECK(LastLogLines(1).size() == 26);
    // Percent signs come through the format as given by the caller.
    Log("%s", "100%% sure");
    CHECK(LastLogLines(1).find("100%% sure") != std::string::npos);
    Log("100%% sure");
    CHECK(LastLogLines(1).find("100% sure") != std::string::npos);
    CleanLogs();
}

TEST(crash_last_log_lines_edges) {
    CleanLogs();
    CHECK(LastLogLines(5).empty());
    CHECK(PutLog(L"", "a\r\nb\r\nc\r\n"));
    CHECK(LastLogLines(1) == "c\r\n");
    CHECK(LastLogLines(2) == "b\r\nc\r\n");
    CHECK(LastLogLines(3) == "a\r\nb\r\nc\r\n");
    CHECK(LastLogLines(1000) == "a\r\nb\r\nc\r\n");
    // A last line without a break still counts as a line.
    CHECK(PutLog(L"", "a\r\nb\r\nc"));
    CHECK(LastLogLines(1) == "c");
    CHECK(LastLogLines(2) == "b\r\nc");
    // An empty file gives nothing.
    CHECK(PutLog(L"", ""));
    CHECK(LastLogLines(5).empty());
    CleanLogs();
}

TEST(crash_crashed_run_tail_reads_across_a_rollover) {
    CleanLogs();
    CHECK(CrashedRunTail(10).empty());

    const std::string s1 = "2026-09-01 10:00:00.000 start version=1\r\n";
    const std::string a = "2026-09-01 10:00:01.000 state a\r\n";
    const std::string s2 = "2026-09-01 11:00:00.000 start version=1\r\n";
    const std::string t = "2026-09-01 10:00:02.000 trace title x\r\n";

    // The new run's start line is in the current file, the crashed run only in the rolled-over one.
    CHECK(PutLog(L".1", s1 + a + t));
    CHECK(PutLog(L"", s2));
    CHECK(CrashedRunTail(10) == s1 + a + t);
    CHECK(CrashedRunTail(2) == a + t);
    CHECK(CrashedRunTail(1) == t);

    // Without a rolled-over file the current one decides.
    DeleteFileW((LogDir() + L"\\admutify.log.1").c_str());
    CHECK(PutLog(L"", s1 + a + s2));
    CHECK(CrashedRunTail(10) == s1 + a);

    // An empty rolled-over file is ignored.
    CHECK(PutLog(L".1", ""));
    CHECK(CrashedRunTail(10) == s1 + a);
    CleanLogs();
}
