@echo off
rem AdMutify build: Visual Studio C++ toolchain + CMake, Release x64.
rem Output in dist\: AdMutify.exe (portable build), AdMutify-Setup-<version>.exe, SHA256SUMS.txt.
rem Debug symbols for crash dumps stay in build\symbols\<version>\ and are never shipped.
rem Env: NOSTART=1 skips the final launch, NOPAUSE=1 never pauses on errors.
rem Running AdMutify instances are closed first.
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "RC=0"

rem --- toolchain
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found. Install Visual Studio with the C++ workload.
    set "RC=1"
    goto :end
)
set "VSPATH="
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq delims=" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo No Visual Studio installation with the C++ x64 tools found.
    set "RC=1"
    goto :end
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo vcvars64.bat failed.
    set "RC=1"
    goto :end
)

rem --- leftovers: stop test processes, close running AdMutify instances (graceful, so they restore
rem Spotify's volume) because two instances would fight over the same audio session, empty dist
taskkill /f /im admutify_tests.exe >nul 2>&1
rem An open settings window takes the first close request, so ask again each round.
for /l %%i in (1,1,20) do (
    tasklist /fi "imagename eq AdMutify.exe" /nh 2>nul | find /i "AdMutify.exe" >nul && (taskkill /im AdMutify.exe >nul 2>&1 & ping -n 2 127.0.0.1 >nul)
)
tasklist /fi "imagename eq AdMutify.exe" /nh 2>nul | find /i "AdMutify.exe" >nul && (
    echo AdMutify is still running ^(elevated or hung^). Close it and build again.
    set "RC=1"
    goto :end
)
if not exist dist mkdir dist
for /f "delims=" %%f in ('dir /b /a-d dist 2^>nul') do del /f /q "dist\%%f" >nul 2>&1
for /d %%d in (dist\*) do rd /s /q "%%d" >nul 2>&1
if exist "dist\AdMutify.exe" (
    echo dist\AdMutify.exe is still in use. Close AdMutify from its tray icon and build again.
    set "RC=1"
    goto :end
)

rem --- build
cmake -S . -B build\x64 -A x64
if errorlevel 1 ( set "RC=1" & goto :end )
cmake --build build\x64 --config Release
if errorlevel 1 ( set "RC=1" & goto :end )

rem --- tests
"build\x64\Release\admutify_tests.exe"
if errorlevel 1 (
    echo Tests failed.
    set "RC=1"
    goto :end
)

rem --- review build (surface tool), never shipped
cmake -S . -B build\x64-review -A x64 -DADMUTIFY_REVIEW=ON
if errorlevel 1 ( set "RC=1" & goto :end )
cmake --build build\x64-review --config Release --target AdMutify
if errorlevel 1 ( set "RC=1" & goto :end )

set "VER="
set /p VER=<"build\x64\version.txt"
set "YEAR=2026"
if exist "build\x64\copyright_year.txt" set /p YEAR=<"build\x64\copyright_year.txt"
if not defined VER (
    echo version.txt missing.
    set "RC=1"
    goto :end
)

rem --- symbols (match the shipped builds; needed to read crash dumps)
set "SYM=build\symbols\%VER%"
mkdir "%SYM%" >nul 2>&1
for %%n in (AdMutify AdMutifyPortable) do if exist "build\x64\Release\%%n.pdb" copy /y "build\x64\Release\%%n.pdb" "%SYM%\" >nul

rem --- portable exe (single file, keeps its settings next to itself)
copy /y "build\x64\Release\AdMutifyPortable.exe" "dist\AdMutify.exe" >nul
if errorlevel 1 ( set "RC=1" & goto :end )
rem copy keeps the date of the last link; stamp the build time so a fresh build is recognisable
pushd dist
copy /b AdMutify.exe +,, >nul
popd

rem --- installer
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" for %%k in ("HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1" "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1") do if not exist "%ISCC%" for /f "tokens=2,*" %%a in ('reg query %%k /v InstallLocation 2^>nul ^| find "InstallLocation"') do set "ISCC=%%b\ISCC.exe"
if not exist "%ISCC%" for /f "delims=" %%p in ('where ISCC.exe 2^>nul') do set "ISCC=%%p"
if not exist "installer\AdMutify.iss" (
    echo Warning: installer skipped, installer\AdMutify.iss is missing.
) else if not exist "%ISCC%" (
    echo Warning: installer skipped, Inno Setup 6 not found.
) else (
    "%ISCC%" /Qp "/DAppVersion=%VER%" "/DCopyrightYear=%YEAR%" "installer\AdMutify.iss"
    if errorlevel 1 ( set "RC=1" & goto :end )
)

rem --- checksums
rem LF line endings and no BOM, so "sha256sum -c" works on Linux and macOS too.
set "SUMS=dist\SHA256SUMS.txt"
if exist "%SUMS%" del /f /q "%SUMS%" >nul 2>&1
powershell -NoProfile -Command "$l = Get-ChildItem dist -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name | ForEach-Object { (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() + ' *' + $_.Name }; [IO.File]::WriteAllText((Resolve-Path dist).Path + '\SHA256SUMS.txt', (($l -join \"`n\") + \"`n\"), (New-Object Text.UTF8Encoding $false))"
if errorlevel 1 ( set "RC=1" & goto :end )

for %%f in ("dist\AdMutify.exe") do echo AdMutify.exe: %%~zf bytes
echo Build %VER% done.

if not defined NOSTART start "" "build\x64\Release\AdMutify.exe"

:end
if not "%RC%"=="0" (
    echo.
    echo Build FAILED.
    echo %CMDCMDLINE% | find /i "/c" >nul
    if not errorlevel 1 if not defined NOPAUSE pause
)
endlocal & exit /b %RC%
