#include "crash.h"

#include <commctrl.h>
#include <dbghelp.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <vector>

#include "audio_control.h"
#include "settings.h"
#include "strings.h"
#include "util.h"
#include "version.h"

namespace {

constexpr unsigned long kDefaultLogLimit = 256 * 1024;
constexpr int kLogFiles = 3;
constexpr int kMaxDumps = 3;
constexpr int kKeepLogDays = 7;
constexpr ULONGLONG kFileTimeDay = 24ULL * 60 * 60 * 10000000;

std::mutex g_logMutex;
unsigned long g_logLimit = kDefaultLogLimit;
std::atomic<bool> g_trace{false};
bool g_prunedOnce = false;

HANDLE g_crashEvent = nullptr;
HANDLE g_doneEvent = nullptr;
HANDLE g_dumpThread = nullptr;
DWORD g_dumpThreadId = 0;
EXCEPTION_POINTERS* g_exceptionPointers = nullptr;
DWORD g_crashThreadId = 0;
std::atomic<bool> g_crashing{false};
std::atomic<HWND> g_resumeWindow{nullptr};

// Prepared at install time: after a crash the heap may be unusable, so the dump thread only formats into these.
constexpr size_t kCrashPathChars = 2048;
wchar_t g_dumpDir[kCrashPathChars];
wchar_t g_flagPath[kCrashPathChars];

std::wstring LogFilePath(int index) {
    std::wstring p = LogDir() + L"\\admutify.log";
    if (index > 0) p += L"." + std::to_wstring(index);
    return p;
}

void RotateLogs() {
    // Keeps admutify.log plus .1 and .2.
    DeleteFileW(LogFilePath(kLogFiles - 1).c_str());
    for (int i = kLogFiles - 2; i >= 0; --i)
        MoveFileExW(LogFilePath(i).c_str(), LogFilePath(i + 1).c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::wstring CrashFlagPath() { return LogDir() + L"\\crashed.flag"; }

void PruneDumps() {
    std::wstring pattern = LogDir() + L"\\crash-*.dmp";
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            names.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(names.begin(), names.end());
    while (static_cast<int>(names.size()) > kMaxDumps) {
        DeleteFileW((LogDir() + L"\\" + names.front()).c_str());
        names.erase(names.begin());
    }
}

ULONGLONG FileTimeValue(const FILETIME& ft) { return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }

// Local time kKeepLogDays ago in the log line format, so it compares as text against line prefixes.
std::string LogCutoff() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    FILETIME ft;
    SystemTimeToFileTime(&now, &ft);
    ULONGLONG v = FileTimeValue(ft) - kKeepLogDays * kFileTimeDay;
    ft.dwLowDateTime = static_cast<DWORD>(v);
    ft.dwHighDateTime = static_cast<DWORD>(v >> 32);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    return buf;
}

void DeleteOlderThan(const std::wstring& pattern, ULONGLONG cutoff) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && FileTimeValue(fd.ftLastWriteTime) < cutoff)
            DeleteFileW((LogDir() + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Caller holds g_logMutex. False when the log file needed a rewrite and it failed.
bool PruneOldLogsLocked() {
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    const ULONGLONG cutoff = FileTimeValue(nowFt) - kKeepLogDays * kFileTimeDay;
    DeleteOlderThan(LogDir() + L"\\admutify.log.*", cutoff);
    DeleteOlderThan(LogDir() + L"\\crash-*.dmp", cutoff);

    std::string text;
    if (!ReadFileUtf8(LogFilePath(0), text)) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    std::string kept = DropLogLinesBefore(text, LogCutoff());
    if (kept.size() == text.size()) return true;
    if (kept.empty()) return DeleteFileW(LogFilePath(0).c_str()) != 0;
    return WriteFileAtomic(LogFilePath(0), kept);
}

DWORD WINAPI DumpThread(LPVOID) {
    WaitForSingleObject(g_crashEvent, INFINITE);

    const bool restored = RestoreAudioUnsafe();

    // Spotify must not stay paused because of us. PostMessageW does not allocate and does not wait for Spotify,
    // so it fits the heap-free crash path (SendMessage could hang on a busy Spotify).
    if (HWND spotify = g_resumeWindow.exchange(nullptr)) {
        // A track title ("artist - song") means the user already started playing again: no toggle then.
        wchar_t title[256] = {};
        GetWindowTextW(spotify, title, static_cast<int>(std::size(title)));
        if (!wcsstr(title, L" - ")) PostMessageW(spotify, WM_APPCOMMAND, 0, MAKELPARAM(0, APPCOMMAND_MEDIA_PLAY_PAUSE));
    }

    if (g_flagPath[0]) {
        // Written before the dump so the next start reports the crash even if the dump stalls.
        HANDLE flag = CreateFileW(g_flagPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (flag != INVALID_HANDLE_VALUE) {
            const char* text = restored ? "restored=1\r\n" : "restored=0\r\n";
            DWORD written = 0;
            WriteFile(flag, text, 12, &written, nullptr);
            CloseHandle(flag);
        }
    }

    if (g_dumpDir[0]) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t path[kCrashPathChars + 40];
        swprintf_s(path, L"%s\\crash-%04u%02u%02u-%02u%02u%02u.dmp", g_dumpDir, st.wYear, st.wMonth, st.wDay, st.wHour,
                   st.wMinute, st.wSecond);
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = g_crashThreadId;
            mei.ExceptionPointers = g_exceptionPointers;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, MiniDumpNormal,
                              g_exceptionPointers ? &mei : nullptr, nullptr, nullptr);
            CloseHandle(f);
        }
    }

    SetEvent(g_doneEvent);
    return 0;
}

// Hands the crash to the dump thread and waits for it. A fault inside the dump thread itself returns at once.
void RunCrashPath(EXCEPTION_POINTERS* info) {
    if (!g_dumpThread || !g_crashEvent || !g_doneEvent || GetCurrentThreadId() == g_dumpThreadId) return;
    if (!g_crashing.exchange(true)) {
        g_exceptionPointers = info;
        g_crashThreadId = GetCurrentThreadId();
        SetEvent(g_crashEvent);
    }
    WaitForSingleObject(g_doneEvent, 10000);
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    RunCrashPath(info);
    return EXCEPTION_EXECUTE_HANDLER;
}

// abort(), std::terminate and failed CRT parameter checks do not reach the exception filter.
[[noreturn]] void FatalExit() {
    RunCrashPath(nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) Sleep(INFINITE);
}

void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { FatalExit(); }
void __cdecl OnTerminate() { FatalExit(); }
void __cdecl OnAbortSignal(int) { FatalExit(); }

bool CopyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        void* p = GlobalLock(mem);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

enum { kBtnCopy = 1001, kBtnOpen = 1002, kBtnClose = 1003 };

HRESULT CALLBACK CrashDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM, LONG_PTR) {
    if (msg == TDN_BUTTON_CLICKED) {
        if (wParam == kBtnCopy) {
            std::string tail = "AdMutify " ADMUTIFY_VERSION_STR "\r\n" + DropTraceLines(CrashedRunTail(50));
            CopyToClipboard(hwnd, Utf8ToWide(tail));
            return S_FALSE;
        }
        if (wParam == kBtnOpen) {
            ShellExecuteW(hwnd, L"explore", LogDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return S_FALSE;
        }
    }
    return S_OK;
}

}  // namespace

void Log(const char* fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[1100];
    int n = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour,
                     st.wMinute, st.wSecond, st.wMilliseconds, msg);
    if (n <= 0) return;

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_prunedOnce) {
        g_prunedOnce = true;
        PruneOldLogsLocked();
    }
    const std::wstring logPath = LogFilePath(0);
    HANDLE h = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PATH_NOT_FOUND) {
        // The folder is only created when it is missing, not on every line.
        EnsureDirectory(LogDir());
        h = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                        nullptr);
    }
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (static_cast<unsigned long long>(size.QuadPart) + static_cast<unsigned long>(n) > g_logLimit && size.QuadPart > 0) {
        CloseHandle(h);
        RotateLogs();
        h = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                        nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
    }
    DWORD written = 0;
    WriteFile(h, line, static_cast<DWORD>(n), &written, nullptr);
    CloseHandle(h);
}

std::string DropLogLinesBefore(std::string_view text, std::string_view cutoff) {
    // Lines are in time order, so everything from the first line at or after the cutoff stays. Only lines that
    // start with a digit carry a timestamp; any other line belongs to the line before it and follows its fate.
    size_t pos = 0;
    while (pos < text.size()) {
        const bool stamped = text[pos] >= '0' && text[pos] <= '9';
        if (stamped && text.compare(pos, cutoff.size(), cutoff) >= 0) return std::string(text.substr(pos));
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    return {};
}

bool PruneOldLogs() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return PruneOldLogsLocked();
}

void SetTraceEnabled(bool enabled) { g_trace = enabled; }
bool TraceEnabled() { return g_trace; }
void SetLogLimitForTests(unsigned long bytes) { g_logLimit = bytes ? bytes : kDefaultLogLimit; }

std::string DropTraceLines(std::string_view text) {
    // A line is "YYYY-MM-DD HH:MM:SS.mmm message": the message starts after 24 characters.
    constexpr size_t kPrefix = 24;
    std::string out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        size_t end = nl == std::string_view::npos ? text.size() : nl + 1;
        std::string_view line = text.substr(pos, end - pos);
        if (line.size() < kPrefix || line.compare(kPrefix, 6, "trace ") != 0) out.append(line);
        pos = end;
    }
    return out;
}

std::string TailBeforeLastStart(std::string_view text, int n) {
    // A line is "YYYY-MM-DD HH:MM:SS.mmm message": the message starts after 24 characters.
    constexpr size_t kPrefix = 24;
    constexpr std::string_view kStart = "start version=";
    size_t end = text.size();
    size_t pos = text.size();
    while (pos > 0) {
        size_t nl = text.rfind('\n', pos - 1);
        size_t lineStart = nl == std::string_view::npos ? 0 : nl + 1;
        if (text.size() - lineStart >= kPrefix + kStart.size() && text.compare(lineStart + kPrefix, kStart.size(), kStart) == 0) {
            end = lineStart;
            break;
        }
        if (nl == std::string_view::npos) break;
        pos = nl;
    }
    std::string_view head = text.substr(0, end);
    size_t from = head.size();
    int lines = 0;
    while (from > 0) {
        size_t nl = head.rfind('\n', from - 1);
        if (nl == std::string_view::npos) {
            from = 0;
            break;
        }
        if (nl + 1 < head.size() && ++lines >= n) {
            from = nl + 1;
            break;
        }
        from = nl;
    }
    return std::string(head.substr(from));
}

std::string CrashedRunTail(int n) {
    std::string cur;
    if (!ReadFileUtf8(LogFilePath(0), cur)) cur.clear();
    std::string_view view = cur;
    // The crashed run's lines may sit in the rolled-over file when this run's first lines caused the rollover.
    std::string older;
    if (ReadFileUtf8(LogFilePath(1), older) && !older.empty()) {
        std::string joined = older + cur;
        return TailBeforeLastStart(joined, n);
    }
    return TailBeforeLastStart(view, n);
}

std::string LastLogLines(int n) {
    std::string text;
    if (!ReadFileUtf8(LogFilePath(0), text)) return {};
    size_t end = text.size();
    int lines = 0;
    size_t pos = end;
    while (pos > 0) {
        --pos;
        if (text[pos] == '\n' && pos + 1 < end) {
            if (++lines >= n) {
                ++pos;
                break;
            }
        }
    }
    return text.substr(pos);
}

void ReserveStackGuarantee() {
    ULONG size = 64 * 1024;
    SetThreadStackGuarantee(&size);
}

void InstallCrashHandler() {
    if (g_dumpThread) return;
    g_crashEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const std::wstring dir = LogDir();
    const std::wstring flag = CrashFlagPath();
    if (dir.size() < kCrashPathChars && flag.size() < kCrashPathChars) {
        EnsureDirectory(dir);
        wcscpy_s(g_dumpDir, dir.c_str());
        wcscpy_s(g_flagPath, flag.c_str());
    }
    g_dumpThread = CreateThread(nullptr, 0, &DumpThread, nullptr, 0, &g_dumpThreadId);
    SetUnhandledExceptionFilter(&CrashFilter);
    _set_invalid_parameter_handler(&OnInvalidParameter);
    std::set_terminate(&OnTerminate);
    std::signal(SIGABRT, &OnAbortSignal);
}

void SetCrashResumeWindow(HWND spotify) { g_resumeWindow.store(spotify); }

bool CrashFlagExists() { return GetFileAttributesW(CrashFlagPath().c_str()) != INVALID_FILE_ATTRIBUTES; }

bool CrashFlagMeansNotRestored(std::string_view flagText) { return flagText.find("restored=0") != std::string_view::npos; }

namespace {

bool g_crashPending = false;
std::string g_crashFlagText;

std::wstring CrashSeenPath() { return LogDir() + L"\\crashed.seen"; }

void ShowCrashDialog(bool notRestored) {
    const TASKDIALOG_BUTTON buttons[] = {
        {kBtnCopy, Str(StrId::CrashCopy)},
        {kBtnOpen, Str(StrId::CrashOpenLogs)},
        {kBtnClose, Str(StrId::Close)},
    };
    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof(cfg);
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.pszWindowTitle = Str(StrId::CrashTitle);
    cfg.pszMainInstruction = Str(StrId::CrashTitle);
    cfg.pszContent = Str(notRestored ? StrId::CrashBodyNotRestored : StrId::CrashBody);
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.cButtons = static_cast<UINT>(std::size(buttons));
    cfg.pButtons = buttons;
    cfg.nDefaultButton = kBtnClose;
    cfg.pfCallback = &CrashDialogProc;
    int pressed = 0;
    // The open-folder button hands off to the shell, which needs COM on this thread.
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr);
    if (SUCCEEDED(co)) CoUninitialize();
}

}  // namespace

void CrashStartupBegin() {
    // A run that crashed again before it could show the dialog left crashed.seen behind: show it first.
    std::string seen;
    if (ReadFileUtf8(CrashSeenPath(), seen)) {
        PruneDumps();
        ShowCrashDialog(CrashFlagMeansNotRestored(seen));
        DeleteFileW(CrashSeenPath().c_str());
    }
    if (!CrashFlagExists()) return;
    if (!ReadFileUtf8(CrashFlagPath(), g_crashFlagText)) g_crashFlagText.clear();
    g_crashPending = true;
    // Kept under another name until the dialog was shown, so the flag cannot be mistaken for a newer crash.
    MoveFileExW(CrashFlagPath().c_str(), CrashSeenPath().c_str(), MOVEFILE_REPLACE_EXISTING);
}

void ShowCrashDialogIfNeeded(int pendingAfterRestore) {
    if (!g_crashPending) return;
    g_crashPending = false;
    PruneDumps();
    ShowCrashDialog(CrashFlagMeansNotRestored(g_crashFlagText) && pendingAfterRestore > 0);
    DeleteFileW(CrashSeenPath().c_str());
    DeleteFileW(CrashFlagPath().c_str());
}
