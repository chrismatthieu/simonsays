# Simon Says - Build script (PowerShell)
# Finds CMake and builds the project. Run from project root.

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$RSID_SDK = "C:/Users/cmatthie/Documents/SDK_2.7.3.0701_471615c_Standard"

# Find cmake.exe
$cmake = $null
foreach ($dir in @(
    "C:\Program Files\CMake\bin",
    "C:\Program Files (x86)\CMake\bin",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
)) {
    if (Test-Path "$dir\cmake.exe") {
        $cmake = "$dir\cmake.exe"
        break
    }
}
if (-not $cmake) {
    $inPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($inPath) { $cmake = $inPath.Source }
}
if (-not $cmake) {
    Write-Error "CMake not found. Install CMake from https://cmake.org or install Visual Studio with 'Desktop development with C++' and ensure CMake component is installed."
}

Write-Host "Using CMake: $cmake"

# Create build dir (ignore if exists)
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
Set-Location $BuildDir

& $cmake .. -G "Visual Studio 17 2022" -A x64 -DRSID_SDK_PATH="$RSID_SDK"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build . --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Build succeeded. Run: .\build\Release\simonsays.exe" -ForegroundColor Green
