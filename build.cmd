@echo off
REM Simon Says - Build script. Run from project root.
REM Uses VS 2022 Build Tools + Ninja (bundled in tools\). No Visual Studio IDE required.
setlocal
set "RSID_SDK=C:/Users/cmatthie/Documents/SDK_2.7.3.0701_471615c_Standard"
set "PROJECT_ROOT=%~dp0"
set "BUILD_DIR=%PROJECT_ROOT%build"
set "NINJA_EXE=%PROJECT_ROOT%tools\ninja.exe"
set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%PROJECT_ROOT%tools\ninja.exe" (
    echo Ninja not found. Download from: https://github.com/ninja-build/ninja/releases
    echo Extract ninja.exe to %PROJECT_ROOT%tools\
    exit /b 1
)

set "CMAKE_EXE="
where cmake >nul 2>&1 && set "CMAKE_EXE=cmake"
if not defined CMAKE_EXE if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
if not defined CMAKE_EXE (
    echo CMake not found. Install from https://cmake.org or: winget install Kitware.CMake
    exit /b 1
)

if not exist "%VCVARS%" (
    echo VS 2022 Build Tools not found. Install with: winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Optional: set SIMONSAYS_SECURE=1 for pairing + in-app enrollment (RSID_SECURE=1).
REM When not 1, pass OFF so CMake cache is cleared (otherwise a previous ON build stays).
if /i "%SIMONSAYS_SECURE%"=="1" (
    set "SECURE_OPT=-DSIMONSAYS_SECURE=ON"
) else (
    set "SECURE_OPT=-DSIMONSAYS_SECURE=OFF"
)

REM Configure with Ninja (so SDK skips C#; uses MSVC from vcvars)
call "%VCVARS%" >nul 2>&1
"%CMAKE_EXE%" .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" -DRSID_SDK_PATH="%RSID_SDK%" %SECURE_OPT%
if errorlevel 1 exit /b 1

REM Build (must run in same env as configure so compiler is in PATH)
call "%VCVARS%" >nul 2>&1
"%NINJA_EXE%"
if errorlevel 1 exit /b 1

echo.
echo Build succeeded.
echo Run with:  run.cmd   (from project root) - or from build folder:
echo   set "PATH=%%CD%%\bin;%%PATH%%" ^& simonsays.exe
exit /b 0
