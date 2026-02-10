@echo off
REM Run Simon Says with DLL path set. Use from project root.
set "BUILD=%~dp0build"
set "BIN=%BUILD%\bin"
set "EXE=%BUILD%\simonsays.exe"
if not exist "%EXE%" (
    echo Not found: %EXE%
    echo Run build.cmd first.
    exit /b 1
)
set "PATH=%BIN%;%PATH%"
"%EXE%" %*
