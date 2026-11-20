@echo off
REM ==========================================================================
REM  Crate - Windows build script (double-click it)
REM
REM  Works with any Visual Studio that has "Desktop development with C++"
REM  installed (2019, 2022, 2026 or newer). The script finds it automatically.
REM  Also needs Git. CMake is taken from Visual Studio if you don't have it.
REM
REM  Optional: set ASIO_SDK=C:\path\to\asiosdk before running for ASIO drivers.
REM ==========================================================================
setlocal EnableExtensions
cd /d "%~dp0"

REM ---- 1. Find Visual Studio with the C++ tools -----------------------------
set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSINSTALLER%\vswhere.exe" goto :novs

set "VSPATH="
pushd "%VSINSTALLER%"
for /f "usebackq tokens=*" %%i in (`vswhere.exe -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
popd
if not defined VSPATH goto :nocpp
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :nocpp

echo Using Visual Studio at:
echo   %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :nocpp
cd /d "%~dp0"

REM ---- 2. Check CMake and Git ---------------------------------------------
where cmake >nul 2>nul
if errorlevel 1 goto :nocmake
where git >nul 2>nul
if errorlevel 1 goto :nogit

REM ---- 3. Pick a generator (Ninja ships with Visual Studio's CMake tools) ---
set "GEN=NMake Makefiles"
set "BDIR=build-nmake"
where ninja >nul 2>nul
if errorlevel 1 goto :gotgen
set "GEN=Ninja"
set "BDIR=build-ninja"
:gotgen
echo Generator: %GEN%

set "ASIO_ARG="
if defined ASIO_SDK set "ASIO_ARG=-DCRATE_ASIO_SDK=%ASIO_SDK%"

REM ---- 4. Configure and build ----------------------------------------------
echo.
echo === Configuring (the first run downloads JUCE and can take a few minutes) ===
cmake -S . -B "%BDIR%" -G "%GEN%" -DCMAKE_BUILD_TYPE=Release %ASIO_ARG%
if errorlevel 1 goto :configfail

echo.
echo === Building Release (10-20 minutes the first time, much faster afterwards) ===
cmake --build "%BDIR%" --config Release
if errorlevel 1 goto :buildfail

if not exist dist mkdir dist
copy /y "%BDIR%\Crate_artefacts\Release\Crate.exe" "dist\Crate.exe" >nul
if errorlevel 1 goto :buildfail

echo.
echo === Done ===
echo The app is at: %~dp0dist\Crate.exe
start "" "%~dp0dist"
pause
exit /b 0

REM ---- errors --------------------------------------------------------------
:novs
echo [ERROR] Visual Studio was not found.
echo         Install Visual Studio (Community is free) from https://visualstudio.microsoft.com/
echo         and select the "Desktop development with C++" workload.
goto :fail

:nocpp
echo [ERROR] Visual Studio is installed, but without the C++ compiler.
echo         Open "Visual Studio Installer", click Modify, tick
echo         "Desktop development with C++" and install it.
goto :fail

:nocmake
echo [ERROR] CMake was not found. Either install it from https://cmake.org/download/
echo         (tick "Add CMake to the system PATH"), or in Visual Studio Installer add
echo         "C++ CMake tools for Windows" under Desktop development with C++.
goto :fail

:nogit
echo [ERROR] Git was not found. Install it from https://git-scm.com/download/win
goto :fail

:configfail
echo [ERROR] Configure failed. See the messages above.
echo         Removing "%BDIR%" so the next attempt starts clean.
if exist "%BDIR%" rmdir /s /q "%BDIR%"
goto :fail

:buildfail
echo [ERROR] Build failed. See the messages above. Running build.bat again will
echo         continue where it stopped. If the error repeats, send it to me.
goto :fail

:fail
pause
exit /b 1
