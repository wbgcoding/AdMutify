#pragma once

#include <windows.h>

#include <string>
#include <string_view>

// Appends one line to <LogDir>\admutify.log (rolling, 4 files of 256 KB, 1 MB at most). Only state changes and errors belong here.
// Log files, log lines and crash dumps older than kKeepLogDays are removed when the first line of a run is written.
constexpr int kKeepLogDays = 30;
void Log(const char* fmt, ...);
// Extra callback counting output, enabled by the --trace option.
void SetTraceEnabled(bool enabled);
bool TraceEnabled();
// Test seam: rollover size limit in bytes.
void SetLogLimitForTests(unsigned long bytes);
// Removes log files, log lines and crash dumps older than kKeepLogDays.
// Returns false when the log file needed a rewrite and that failed.
bool PruneOldLogs();
// Pure part: keeps the lines from the first one whose "YYYY-MM-DD HH:MM:SS" prefix is not before cutoff.
std::string DropLogLinesBefore(std::string_view text, std::string_view cutoff);
// Pure part: removes the lines a --trace run adds (message starts with "trace "); they can hold titles and exe names.
std::string DropTraceLines(std::string_view text);
// Pure part: the n lines that end right before the last "start version=" line (the tail of the run that
// came before this one). Without such a line, the last n lines.
std::string TailBeforeLastStart(std::string_view text, int n);
// The crashed run's tail from the log files, without trace lines; reaches into admutify.log.1 after a rollover.
std::string CrashedRunTail(int n);

// True once a crash is being handled on any thread. The main thread may still run for a few seconds
// meanwhile; audio changes must stop so they cannot undo the restore of the crash path.
bool CrashInProgress();
// Test seam: pretends a crash is in progress.
void SetCrashInProgressForTests(bool on);

// Reserves stack for the exception filter on the calling thread.
void ReserveStackGuarantee();
// Installs the unhandled exception filter and its dump thread.
void InstallCrashHandler();
#ifdef ADMUTIFY_REVIEW
// Review tool: the next InstallCrashHandler starts no dump thread, as when CreateThread fails.
void SkipCrashThreadForReview();
#endif
// Spotify's main window while we hold a pause (nullptr otherwise): a crash gives the pause back by posting the
// play/pause command to it. Plain atomic store, safe to read without the heap.
void SetCrashResumeWindow(HWND spotify);
bool CrashFlagExists();
// Pure part: the crash marker says the volume restore did not fully succeed. An empty or unknown marker
// (older versions wrote an empty file) does not.
bool CrashFlagMeansNotRestored(std::string_view flagText);
// Pure part of CrashStartupBegin: what happens to a leftover crashed.seen and a fresh crashed.flag.
struct CrashStartupPlan {
    bool showSeenNow = false;  // the last start crashed again before its dialog: show that report at once
    bool deferSeen = false;    // the last start ended some other way before its dialog: show it at the normal place
    bool deferFlag = false;    // a fresh crash: rename the flag to crashed.seen, show it at the normal place
};
CrashStartupPlan PlanCrashStartup(bool seenExists, bool flagExists);
// Call at start, after the first log line. A report left over from a start that crashed again before reaching
// its dialog is shown now; every other report waits for ShowCrashDialogIfNeeded, after the volume restore. A fresh
// crashed.flag is renamed to crashed.seen so a crash during the rest of the start-up cannot hide the report.
void CrashStartupBegin();
// Shows the dialog for the crash found by CrashStartupBegin, once, and removes the marker. The "could not be
// restored" text is used only when the crash-time restore failed and records are still pending after the
// restore at start (pendingAfterRestore).
void ShowCrashDialogIfNeeded(int pendingAfterRestore);
