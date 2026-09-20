@echo off
rem LocalSendXP - setup builder (Inno Setup 5.6.1).
rem
rem Usage: build_installer.bat [rebuild]
rem
rem   * takes the version number from src\util\common.cpp (LSXP_CLIENT_VERSION)
rem   * builds the Release binary first when it is missing
rem   * checks that every file the setup script needs really exists
rem   * produces output\LocalSendXP-<version>-setup.exe
rem
rem   Double-click it and the window stays open at the end.
rem   If Inno Setup was not found, set INNO_SETUP_DIR to its folder, e.g.
rem       set INNO_SETUP_DIR=C:\Program Files (x86)\Inno Setup 5
rem
rem The script is ASCII only on purpose (cmd.exe reads batch files with the
rem OEM code page, Chinese text would come out as garbage).
setlocal enabledelayedexpansion

set "HOLD="
if "%~1"=="" set "HOLD=1"

set "ROOT=%~dp0.."
set "BIN=%ROOT%\LocalSendXP\bin\Release"
set "ISS=%~dp0LocalSendXP.iss"
set "SRC=%ROOT%\LocalSendXP\src\util\common.cpp"

echo === LocalSendXP setup builder ===

rem ---------------------------------------------------------------- 1) binary
if not exist "%BIN%\LocalSendXP.exe" (
    echo [INFO] Release build not found, building it first ...
    call "%ROOT%\LocalSendXP\build\build_release.bat" Win32 Release
    if errorlevel 1 goto :failed
) else if /i "%~1"=="rebuild" (
    echo [INFO] Rebuilding the Release binary ...
    call "%ROOT%\LocalSendXP\build\build_release.bat" Win32 Release rebuild
    if errorlevel 1 goto :failed
)

rem ---------------------------------------------------------------- 2) version
set "VERSION="
set "RAW="
for /f "tokens=2 delims==" %%L in ('findstr /c:"const char* const LSXP_CLIENT_VERSION" "%SRC%"') do set "RAW=%%L"
if defined RAW (
    set "RAW=!RAW: =!"
    for /f "tokens=1 delims=;" %%T in ("!RAW!") do set "VERSION=%%~T"
)
if not defined VERSION (
    echo [WARN] cannot read LSXP_CLIENT_VERSION, falling back to 1.0.0
    set "VERSION=1.0.0"
)
echo [INFO] version %VERSION%

rem ------------------------------------------------------------ 3) payload
set "MISSING=0"
for %%F in ("%BIN%\LocalSendXP.exe" "%BIN%\libeay32.dll" "%BIN%\ssleay32.dll" "%BIN%\certs\ca-bundle.crt" "%ROOT%\LICENSE" "%ROOT%\NOTICE" "%ROOT%\README.md" "%ROOT%\architecture.md" "%~dp0THIRD-PARTY-NOTICES.txt" "%~dp0licenses\LICENSE-OpenSSL.txt") do (
    if not exist "%%~F" (
        echo [ERROR] missing file: %%~F
        set "MISSING=1"
    )
)
if "!MISSING!"=="1" (
    echo [ERROR] cannot package an incomplete build.
    goto :failed
)

rem ------------------------------------------------------------ 4) Inno Setup
set "ISCC="
set "ISCCDIR="
if defined INNO_SETUP_DIR (
    if exist "%INNO_SETUP_DIR%\ISCC.exe" (
        set "ISCC=%INNO_SETUP_DIR%\ISCC.exe"
        set "ISCCDIR=%INNO_SETUP_DIR%"
    )
)
call :try_inno "%ProgramFiles%\Inno Setup 5"
call :try_inno "C:\Program Files (x86)\Inno Setup 5"
call :try_inno "D:\Program Files (x86)\Inno Setup 5"
call :try_inno "E:\Program Files (x86)\Inno Setup 5"
call :try_inno "C:\Program Files (x86)\Inno Setup 6"
call :try_inno "C:\Program Files\Inno Setup 6"
if not defined ISCC (
    for %%P in (ISCC.exe) do set "FROM_PATH=%%~$PATH:P"
    if defined FROM_PATH (
        set "ISCC=!FROM_PATH!"
        for %%D in ("!FROM_PATH!") do set "ISCCDIR=%%~dpD"
    )
)
if not defined ISCC (
    echo [ERROR] Inno Setup was not found.
    echo         Install Inno Setup 5.6.1 ^(the last version whose compiler
    echo         runs on Windows XP^) from https://jrsoftware.org/isdl.php
    echo         or point INNO_SETUP_DIR at its folder.
    goto :failed
)
echo [INFO] compiler: !ISCC!

rem -------------------------------------------------- 5) Chinese wizard file
set "ZH="
if exist "%~dp0ChineseSimplified.isl" set "ZH=%~dp0ChineseSimplified.isl"
if not defined ZH if exist "!ISCCDIR!\Languages\ChineseSimplified.isl" set "ZH=!ISCCDIR!\Languages\ChineseSimplified.isl"
if not exist "%~dp0output" mkdir "%~dp0output"
if defined ZH (
    echo [INFO] Chinese wizard source: !ZH!
    rem Inno Setup 5 reads .isl files with the ANSI code page, so a UTF-8
    rem translation has to be converted first (see prepare_isl.ps1).
    set "ZH_ISL=%~dp0output\ChineseSimplified.isl"
    if exist "%~dp0prepare_isl.ps1" (
        for %%P in (powershell.exe) do set "PS=%%~$PATH:P"
        if defined PS (
            "!PS!" -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare_isl.ps1" "!ZH!" "!ZH_ISL!"
            if errorlevel 1 goto :failed
        ) else (
            echo [WARN] PowerShell not found; using the language file as is.
            echo        If the Chinese wizard comes out garbled, save the .isl as
            echo        ANSI ^(code page 936^) first.
            set "ZH_ISL=!ZH!"
        )
    ) else (
        set "ZH_ISL=!ZH!"
    )
) else (
    echo [WARN] ChineseSimplified.isl not found - building an English only setup.
    echo        Put the file into "%ISCCDIR%\Languages" ^(see
    echo        https://jrsoftware.org/files/istrans/^) to get a bilingual one.
)

rem ------------------------------------------------------------ 6) compile
echo [INFO] compiling %ISS% ...
set "ISCC_LOG=%~dp0output\iscc.log"
if defined ZH (
    "!ISCC!" "/DMyAppVersion=%VERSION%" "/DZH_ISL_FILE=!ZH_ISL!" "%ISS%" > "!ISCC_LOG!" 2>&1
) else (
    "!ISCC!" "/DMyAppVersion=%VERSION%" "%ISS%" > "!ISCC_LOG!" 2>&1
)
set "ISCC_RESULT=!errorlevel!"
rem Show the compiler output (it was captured so that it is also kept on disk).
rem The Chinese translation ships messages for newer Inno Setup versions; 5.6.1
rem ignores them one by one, which would bury the interesting warnings in noise.
set "IGNORED=0"
for /f %%C in ('findstr /c:"is not recognized by this version of Inno Setup" "!ISCC_LOG!" ^| find /c /v ""') do set "IGNORED=%%C"
findstr /v /c:"is not recognized by this version of Inno Setup" "!ISCC_LOG!"
if not "!IGNORED!"=="0" (
    echo [INFO] !IGNORED! message^(s^) meant for a newer Inno Setup were ignored by 5.6.1.
)
if not "!ISCC_RESULT!"=="0" (
    echo [ERROR] the compiler log is in "!ISCC_LOG!"
    goto :failed
)

set "SETUP=%~dp0output\LocalSendXP-%VERSION%-setup.exe"
echo.
if exist "%SETUP%" (
    for %%F in ("%SETUP%") do echo Done. Setup: %%F  ^(%%~tF, %%~zF bytes^)
) else (
    echo Done. See %~dp0output
)
call :hold_window
endlocal
exit /b 0

:try_inno
if defined ISCC exit /b 0
if exist "%~1\ISCC.exe" (
    set "ISCC=%~1\ISCC.exe"
    set "ISCCDIR=%~1"
)
exit /b 0

:failed
echo.
echo [ERROR] Setup build failed.
call :hold_window
endlocal
exit /b 1

:hold_window
if not defined HOLD exit /b 0
echo.
echo ---- Build finished. Press any key to close this window ----
pause >nul
exit /b 0
