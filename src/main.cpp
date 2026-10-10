#include <windows.h>
#include <shellapi.h>

#include <string_view>

#include "app.h"
#include "settings.h"
#ifdef ADMUTIFY_REVIEW
#include "review.h"
#endif

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
#ifdef ADMUTIFY_PORTABLE
    SetPortableBuild(true);
#endif
    AppOptions options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#ifdef ADMUTIFY_REVIEW
    int reviewExit = 0;
    if (argv && RunReviewCommand(argc, argv, reviewExit)) {
        LocalFree(argv);
        return reviewExit;
    }
#endif
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring_view arg = argv[i];
        if (arg == L"--autostart") options.autostart = true;
        else if (arg == L"--second-instance") options.secondInstance = true;
        else if (arg == L"--trace") options.trace = true;
    }
    if (argv) LocalFree(argv);
    return RunApp(inst, options);
}
