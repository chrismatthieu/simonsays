# Run Simon Says with DLL path set. Use from project root or build folder.
$ErrorActionPreference = "Stop"
$BuildDir = if ($PSScriptRoot) { Join-Path $PSScriptRoot "build" } else { "build" }
$BinDir = Join-Path $BuildDir "bin"
$Exe = Join-Path $BuildDir "simonsays.exe"

if (-not (Test-Path $Exe)) {
    Write-Error "Not found: $Exe. Run .\build.cmd first."
}
$env:PATH = "$BinDir;$env:PATH"
& $Exe @args
