<#
.SYNOPSIS
    Configures and builds the xploit_game_ui native runtime, runs the native
    tests and copies the plugin DLL into the Unity package.

.EXAMPLE
    .\tools\build.ps1                 # Release build + tests + copy
    .\tools\build.ps1 -Config Debug   # Debug build
    .\tools\build.ps1 -SkipTests -SkipCopy
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [switch]$SkipTests,
    [switch]$SkipCopy,
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$native = Join-Path $root 'native'

# --- Locate Visual Studio (C++ toolset) and enter its developer environment ---
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio 2022/2026 with the C++ workload." }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No Visual Studio installation with the C++ toolset was found." }
Write-Host "Visual Studio: $vsPath"

$env:Path = (Split-Path -Parent $vswhere) + ';' + $env:Path   # Enter-VsDevShell expects vswhere.exe on PATH
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

# --- vcpkg (bundled with Visual Studio) ---
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = Join-Path $vsPath 'VC\vcpkg' }
if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) { throw "vcpkg not found at $env:VCPKG_ROOT" }
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $root 'build\vcpkg-cache'
New-Item -ItemType Directory -Force $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
Write-Host "vcpkg: $env:VCPKG_ROOT (binary cache: $env:VCPKG_DEFAULT_BINARY_CACHE)"

$preset = if ($Config -eq 'Debug') { 'x64-windows-debug' } else { 'x64-windows-release' }
$buildDir = Join-Path $native "out\$preset"
if ($Reconfigure -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

Push-Location $native
try {
    Write-Host "== configure ($preset) =="
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

    Write-Host "== build =="
    cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

    if (-not $SkipTests) {
        Write-Host "== tests =="
        ctest --preset $preset --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Native tests failed ($LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}

if (-not $SkipCopy) {
    $bin = Join-Path $buildDir 'bin'
    $dest = Join-Path $root 'unity\Packages\com.xploit.game_ui\Plugins\x86_64'
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($pattern in @('xploit_game_ui.dll', 'xploit_game_ui.pdb', 'v8*.dll', 'zlib*.dll', 'third_party_*.dll', 'icudt*.dat', '*_blob.bin')) {
        Get-ChildItem -Path $bin -Filter $pattern -ErrorAction SilentlyContinue | Copy-Item -Destination $dest -Force
    }
    Write-Host "Plugin binaries copied to $dest"
}

Write-Host "Done."
