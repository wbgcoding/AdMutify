@echo off
title AdMutify
rem AdMutify build: Visual Studio C++ toolchain + CMake, Release x64.
rem Output in dist\: AdMutify.exe (portable build), AdMutify-Setup-<version>.exe, SHA256SUMS.txt.
rem Debug symbols for crash dumps stay in build\symbols\<version>\ and are never shipped.
rem Env: NOSTART=1 skips the final launch, NOPAUSE=1 marks an automated run (no resize, pause or wait).
rem Running AdMutify instances are closed first. find.exe is called by path: a POSIX find earlier in PATH
rem (Git Bash) would make running instances look absent.
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "EXITRC=0"
rem A double-clicked run gets a small window that stays open on errors and closes itself 10 s after success.
set "INTERACTIVE="
if not defined NOPAUSE echo %CMDCMDLINE% | "%SystemRoot%\System32\find.exe" /i "/c" >nul && set "INTERACTIVE=1"
if defined INTERACTIVE mode con cols=100 lines=25
rem === build freshness: check ===
set "BS=%~dp0tools\build-stamp.ps1"
set "BSKEY=exe"
set "BSEXTRA="
set "BSART=dist\AdMutify.exe"
set "BSFRESH="
set "BSFORCE="
if "%FORCE%"=="1" set "BSFORCE=1"
for %%A in (%*) do if /i "%%~A"=="--force" set "BSFORCE=1"
if defined BSFORCE goto :bs_start
if not exist "%BS%" goto :bs_start
for /f "usebackq delims=" %%A in (`powershell -NoProfile -ExecutionPolicy RemoteSigned -File "%BS%" -Check -Project "%~dp0." -Key "%BSKEY%" -Extra "%BSEXTRA%"`) do set "BSFRESH=%%A"
if defined BSFRESH goto :bs_uptodate
:bs_start

rem --- toolchain
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found. Install Visual Studio with the C++ workload.
    set "EXITRC=1"
    goto :end
)
set "VSPATH="
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq delims=" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo No Visual Studio installation with the C++ x64 tools found.
    set "EXITRC=1"
    goto :end
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo vcvars64.bat failed.
    set "EXITRC=1"
    goto :end
)

rem --- leftovers: stop test processes, close running AdMutify instances (graceful, so they restore
rem Spotify's volume) because two instances would fight over the same audio session, empty dist
taskkill /f /im admutify_tests.exe >nul 2>&1
rem An open settings window takes the first close request, so ask again each round.
for /l %%i in (1,1,20) do (
    tasklist /fi "imagename eq AdMutify.exe" /nh 2>nul | "%SystemRoot%\System32\find.exe" /i "AdMutify.exe" >nul && (taskkill /im AdMutify.exe >nul 2>&1 & ping -n 2 127.0.0.1 >nul)
)
tasklist /fi "imagename eq AdMutify.exe" /nh 2>nul | "%SystemRoot%\System32\find.exe" /i "AdMutify.exe" >nul && (
    echo AdMutify is still running ^(elevated or hung^). Close it and build again.
    set "EXITRC=1"
    goto :end
)
if not exist dist mkdir dist
for /f "delims=" %%f in ('dir /b /a-d dist 2^>nul') do del /f /q "dist\%%f" >nul 2>&1
for /d %%d in (dist\*) do rd /s /q "%%d" >nul 2>&1
if exist "dist\AdMutify.exe" (
    echo dist\AdMutify.exe is still in use. Close AdMutify from its tray icon and build again.
    set "EXITRC=1"
    goto :end
)

rem --- build
rem A Visual Studio update moves the compiler and leaves the cached path dangling; a failed configure retries fresh once.
cmake -S . -B build\x64 -A x64
if errorlevel 1 (
    echo Configure failed, retrying with a fresh CMake cache.
    cmake --fresh -S . -B build\x64 -A x64
    if errorlevel 1 ( set "EXITRC=1" & goto :end )
)
cmake --build build\x64 --config Release
if errorlevel 1 ( set "EXITRC=1" & goto :end )

rem --- tests (the output is also kept in build\x64\tests.log, so a failure can be read after the window closes)
"build\x64\Release\admutify_tests.exe" > "build\x64\tests.log" 2>&1
set "TESTRC=!errorlevel!"
type "build\x64\tests.log"
if not "!TESTRC!"=="0" (
    echo Tests failed. Full output: build\x64\tests.log
    set "EXITRC=1"
    goto :end
)

rem --- review build (surface tool), never shipped
cmake -S . -B build\x64-review -A x64 -DADMUTIFY_REVIEW=ON
if errorlevel 1 (
    echo Configure failed, retrying with a fresh CMake cache.
    cmake --fresh -S . -B build\x64-review -A x64 -DADMUTIFY_REVIEW=ON
    if errorlevel 1 ( set "EXITRC=1" & goto :end )
)
cmake --build build\x64-review --config Release --target AdMutify
if errorlevel 1 ( set "EXITRC=1" & goto :end )

set "VER="
set /p VER=<"build\x64\version.txt"
set "YEAR="
if exist "build\x64\copyright_year.txt" set /p YEAR=<"build\x64\copyright_year.txt"
if not defined VER (
    echo version.txt missing.
    set "EXITRC=1"
    goto :end
)
if not defined YEAR (
    echo copyright_year.txt missing.
    set "EXITRC=1"
    goto :end
)

rem --- symbols (match the shipped builds; needed to read crash dumps)
set "SYM=build\symbols\%VER%"
mkdir "%SYM%" >nul 2>&1
for %%n in (AdMutify AdMutifyPortable) do if exist "build\x64\Release\%%n.pdb" copy /y "build\x64\Release\%%n.pdb" "%SYM%\" >nul

rem --- portable exe (single file, keeps its settings next to itself)
copy /y "build\x64\Release\AdMutifyPortable.exe" "dist\AdMutify.exe" >nul
if errorlevel 1 ( set "EXITRC=1" & goto :end )
rem copy keeps the date of the last link; stamp the build time so a fresh build is recognisable
pushd dist
copy /b AdMutify.exe +,, >nul
popd

rem --- installer
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" for %%k in ("HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1" "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1") do if not exist "%ISCC%" for /f "tokens=2,*" %%a in ('reg query %%k /v InstallLocation 2^>nul ^| "%SystemRoot%\System32\find.exe" "InstallLocation"') do set "ISCC=%%b\ISCC.exe"
if not exist "%ISCC%" for /f "delims=" %%p in ('where ISCC.exe 2^>nul') do set "ISCC=%%p"
if not exist "installer\AdMutify.iss" (
    echo Warning: installer skipped, installer\AdMutify.iss is missing.
) else if not exist "%ISCC%" (
    echo Warning: installer skipped, Inno Setup 6 not found.
) else (
    "%ISCC%" /Qp "/DAppVersion=%VER%" "/DCopyrightYear=%YEAR%" "installer\AdMutify.iss"
    if errorlevel 1 ( set "EXITRC=1" & goto :end )
)

rem --- checksums
rem LF line endings and no BOM, so "sha256sum -c" works on Linux and macOS too.
set "SUMS=dist\SHA256SUMS.txt"
if exist "%SUMS%" del /f /q "%SUMS%" >nul 2>&1
powershell -NoProfile -Command "$l = Get-ChildItem dist -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name | ForEach-Object { (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() + ' *' + $_.Name }; [IO.File]::WriteAllText((Resolve-Path dist).Path + '\SHA256SUMS.txt', (($l -join \"`n\") + \"`n\"), (New-Object Text.UTF8Encoding $false))"
if errorlevel 1 ( set "EXITRC=1" & goto :end )

for %%f in ("dist\AdMutify.exe") do echo AdMutify.exe: %%~zf bytes
rem === build freshness: save (every final file in dist) ===
set "BSART=dist\AdMutify.exe"
for %%f in ("dist\*.exe" "dist\SHA256SUMS.txt") do if /i not "%%~nxf"=="AdMutify.exe" set "BSART=!BSART!,dist\%%~nxf"
if exist "%BS%" powershell -NoProfile -ExecutionPolicy RemoteSigned -File "%BS%" -Save -Project "%~dp0." -Key "%BSKEY%" -Extra "%BSEXTRA%" -Artifact "!BSART!" >nul 2>&1
echo Build %VER% done.

rem Launched through Explorer so the app does not inherit this script's output handles (a caller that
rem captures the build output would otherwise wait until AdMutify exits).
if not defined NOSTART explorer.exe "%CD%\build\x64\Release\AdMutify.exe"

:end
if not "%EXITRC%"=="0" (
    echo.
    echo Build FAILED.
    if defined INTERACTIVE pause
) else if defined INTERACTIVE (
    "%SystemRoot%\System32\timeout.exe" /t 10
)
endlocal & exit /b %EXITRC%

:bs_uptodate
for %%F in ("%BSFRESH%") do echo Build is up to date (last build %%~tF), nothing to do: %%~fF
if defined INTERACTIVE "%SystemRoot%\System32\timeout.exe" /t 10
exit /b 0
