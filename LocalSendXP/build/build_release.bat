@echo off
rem LocalSendXP - Visual Studio 2008 (VC9) build helper.
rem
rem Usage: build_release.bat [Win32^|x64] [Debug^|Release] [rebuild]
rem
rem   Double-click it  -> builds Release^|Win32 and keeps the window open so
rem                       that the result can be read.
rem   From a console   -> the window is not held.
rem   Third argument   -> "rebuild" forces a full rebuild (vcbuild /rebuild);
rem                       without it the build is incremental and may report
rem                       that the project is up to date (the Chinese tool
rem                       chain prints its own wording) and finish in under a
rem                       second.  Keep this file ASCII only: cmd.exe reads
rem                       plain batch files with the OEM code page.
setlocal enabledelayedexpansion

set "PROJECT=%~dp0..\LocalSendXP.vcproj"
set "PLATFORM=%~1"
if "%PLATFORM%"=="" set "PLATFORM=Win32"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Release"

rem Started from Explorer (no arguments): hold the console at the end.
set "HOLD="
if "%~1"=="" set "HOLD=1"
set "REBUILD="
if /i "%~3"=="rebuild" set "REBUILD=/rebuild"

call :find_vs
if "%VSDIR%"=="" goto :no_vs

if /i "%PLATFORM%"=="x64" goto :vs_x64
call "%VSDIR%\VC\vcvarsall.bat" x86
goto :have_vs

:vs_x64
call "%VSDIR%\VC\vcvarsall.bat" x86_amd64

:have_vs
echo Building LocalSendXP (%CONFIG%^|%PLATFORM%) ...
set "PROJECT_ARG=%PROJECT%"
set "CONFIG_ARG=%CONFIG%|%PLATFORM%"
"%VSDIR%\VC\vcpackages\vcbuild.exe" %REBUILD% "%PROJECT_ARG%" "%CONFIG_ARG%"
if errorlevel 1 goto :failed

echo.
rem %~dp0 is ...\LocalSendXP\build\, so the output folder is one level up.
echo Done. Executable: %~dp0..\bin\%CONFIG%\LocalSendXP.exe
for %%F in ("%~dp0..\bin\%CONFIG%\LocalSendXP.exe") do echo       ^(%%~tF, %%~zF bytes^)
echo Tip: if it says "up to date", pass "rebuild" to force a full build.
call :hold_window
endlocal
exit /b 0

:find_vs
set "VSDIR="
call :try_vs "E:\Program Files (x86)\Microsoft Visual Studio 9.0"
call :try_vs "D:\Program Files (x86)\Microsoft Visual Studio 9.0"
call :try_vs "C:\Program Files (x86)\Microsoft Visual Studio 9.0"
call :try_vs "C:\Program Files\Microsoft Visual Studio 9.0"
exit /b 0

:try_vs
if not "%VSDIR%"=="" exit /b 0
if exist %1\VC\vcvarsall.bat set "VSDIR=%~1"
exit /b 0

:no_vs
echo [ERROR] Visual Studio 2008 was not found.
echo         Please edit the :find_vs section of this script.
call :hold_window
endlocal
exit /b 1

:failed
echo [ERROR] Build failed.
call :hold_window
endlocal
exit /b 1

:hold_window
if not defined HOLD exit /b 0
echo.
echo ---- Build finished. Press any key to close this window ----
pause >nul
exit /b 0
