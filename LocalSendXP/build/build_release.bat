@echo off
rem LocalSendXP - Visual Studio 2008 (VC9) build helper.
rem Usage: build_release.bat [Win32^|x64] [Debug^|Release]
setlocal enabledelayedexpansion

set "PROJECT=%~dp0..\LocalSendXP.vcproj"
set "PLATFORM=%~1"
if "%PLATFORM%"=="" set "PLATFORM=Win32"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Release"

call :find_vs
if "%VSDIR%"=="" goto :no_vs

if /i "%PLATFORM%"=="x64" goto :vs_x64
call "%VSDIR%\VC\vcvarsall.bat" x86
goto :have_vs

:vs_x64
call "%VSDIR%\VC\vcvarsall.bat" x86_amd64

:have_vs
echo Building LocalSendXP (%CONFIG%^|%PLATFORM%) ...
"%VSDIR%\VC\vcpackages\vcbuild.exe" "%PROJECT%" "%CONFIG%|%PLATFORM%"
if errorlevel 1 goto :failed

echo.
echo Done. Executable: %~dp0..\..\bin\%CONFIG%\LocalSendXP.exe
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
endlocal
exit /b 1

:failed
echo [ERROR] Build failed.
endlocal
exit /b 1
