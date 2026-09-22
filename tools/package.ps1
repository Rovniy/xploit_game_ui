<#
.SYNOPSIS
    Assembles the release artifacts: a UPM tarball that installs straight into
    Unity, and a zip of the native runtime on its own.

.DESCRIPTION
    Stages the package into dist/staging/package, adds the licence files, and
    produces:

        dist/com.xploit.game_ui-<version>.tgz     install through Package Manager
        dist/xploit_game_ui-native-win-x64.zip    the DLLs alone

    The plugin DLL must already be built and copied into the package, which
    tools/build.ps1 does. Run that first, or pass -Build to have this script do it.

.EXAMPLE
    .\tools\build.ps1
    .\tools\package.ps1

    .\tools\package.ps1 -Build      # build, test and package in one go
#>
[CmdletBinding()]
param(
    [switch]$Build,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root 'unity\Packages\com.xploit.game_ui'
$dist = Join-Path $root 'dist'
$staging = Join-Path $dist 'staging'

if ($Build) {
    $buildArgs = @()
    if ($SkipTests) { $buildArgs += '-SkipTests' }
    & (Join-Path $PSScriptRoot 'build.ps1') @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed ($LASTEXITCODE)" }
}

# --- Version comes from package.json, so there is one source of truth ---------
$manifest = Get-Content (Join-Path $package 'package.json') -Raw | ConvertFrom-Json
$version = $manifest.version
if (-not $version) { throw "package.json has no version" }
Write-Host "Packaging com.xploit.game_ui $version"

# --- The native runtime has to be in place ------------------------------------
$plugins = Join-Path $package 'Plugins\x86_64'
$required = @('xploit_game_ui.dll', 'v8.dll', 'v8_libbase.dll', 'v8_libplatform.dll', 'icudtl.dat')
$missing = $required | Where-Object { -not (Test-Path (Join-Path $plugins $_)) }
if ($missing) {
    throw @"
The native runtime is not in the package: $($missing -join ', ')
Run .\tools\build.ps1 first (or pass -Build to this script).
"@
}

# --- Stage ---------------------------------------------------------------------
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
$stagedPackage = Join-Path $staging 'package'
New-Item -ItemType Directory -Force $stagedPackage | Out-Null

Copy-Item -Path (Join-Path $package '*') -Destination $stagedPackage -Recurse -Force

# Copy-Item's -Exclude is unreliable together with -Recurse, so everything that
# must not ship is removed after the copy instead.
#
# The .pdb is ~280 MB of debug symbols and belongs in the native zip at most,
# never in a package every consumer downloads. The rest is local editor state
# that git ignores but that is still sitting in the working tree.
Get-ChildItem -Path $stagedPackage -Recurse -File -Include '*.pdb', '*.pdb.meta', '.DS_Store', 'Thumbs.db' |
    Remove-Item -Force
foreach ($junk in '.idea', '.vs', '.vscode') {
    Get-ChildItem -Path $stagedPackage -Recurse -Force -Directory -Filter $junk |
        Remove-Item -Recurse -Force
}

# Samples~ has to keep that exact name, or Unity stops hiding it from the asset
# database. The wildcard copy above already brought it across.
if (-not (Test-Path (Join-Path $stagedPackage 'Samples~'))) {
    throw "Samples~ did not make it into the staged package"
}

# --- Licence files, under the names the Package Manager shows ------------------
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stagedPackage 'LICENSE.md') -Force
Copy-Item (Join-Path $root 'docs\THIRD-PARTY-NOTICES.md') `
          (Join-Path $stagedPackage 'Third Party Notices.md') -Force

# The copyright lines shipped with each vcpkg port, concatenated, so the binary
# package carries the attributions its dependencies require.
$vcpkgShare = Join-Path $root 'native\vcpkg_installed\x64-windows-static-md\share'
if (Test-Path $vcpkgShare) {
    $notices = New-Object System.Text.StringBuilder
    [void]$notices.AppendLine("# Licence texts of the bundled components`n")
    [void]$notices.AppendLine("Collected from the vcpkg ports built into this release.`n")
    foreach ($copyright in Get-ChildItem -Path $vcpkgShare -Filter 'copyright' -Recurse -File | Sort-Object FullName) {
        $port = Split-Path -Leaf (Split-Path -Parent $copyright.FullName)
        [void]$notices.AppendLine("`n## $port`n")
        [void]$notices.AppendLine('```')
        [void]$notices.AppendLine((Get-Content $copyright.FullName -Raw))
        [void]$notices.AppendLine('```')
    }
    $notices.ToString() | Set-Content (Join-Path $stagedPackage 'Third Party Licences.md') -Encoding utf8
} else {
    Write-Warning "vcpkg_installed not found; 'Third Party Licences.md' is not included."
}

# --- Tarball -------------------------------------------------------------------
# A UPM tarball is an npm tarball: gzipped tar with everything under package/.
New-Item -ItemType Directory -Force $dist | Out-Null
$tgz = Join-Path $dist "com.xploit.game_ui-$version.tgz"
if (Test-Path $tgz) { Remove-Item $tgz -Force }

Push-Location $staging
try {
    # bsdtar ships with Windows 10 1803 and later.
    tar --format=ustar -czf $tgz package
    if ($LASTEXITCODE -ne 0) { throw "tar failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}

# --- The native runtime on its own, for anyone not using UPM -------------------
$zip = Join-Path $dist 'xploit_game_ui-native-win-x64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
$binaries = Get-ChildItem -Path $plugins -File |
    Where-Object { $_.Extension -in '.dll', '.dat', '.bin' }
Compress-Archive -Path $binaries.FullName -DestinationPath $zip -CompressionLevel Optimal

Remove-Item -Recurse -Force $staging

$tgzSize = [math]::Round((Get-Item $tgz).Length / 1MB, 1)
$zipSize = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ""
Write-Host "  $tgz  ($tgzSize MB)"
Write-Host "  $zip  ($zipSize MB)"
Write-Host ""
Write-Host "Install in Unity: Window > Package Manager > + > Install package from tarball..."
