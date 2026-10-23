# Usage: .\build.ps1 [-Config Release|Debug] [-Run]
# Build tree lives on C: because JUCE's juceaide hangs when writing generated files on E:.
param(
    [string]$Config = "Release",
    [switch]$Run
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$buildDir = Join-Path $env:LOCALAPPDATA "WaveTagger\build"
$binDir = Join-Path $root "bin"

cmake -S $root -B $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDir --config $Config --target WaveTagger -j
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $buildDir "WaveTagger_artefacts\$Config\WaveTagger.exe"
New-Item -ItemType Directory -Force $binDir | Out-Null
Copy-Item $exe $binDir -Force
Write-Host "Built: $binDir\WaveTagger.exe"

if ($Run) { Start-Process (Join-Path $binDir "WaveTagger.exe") }
