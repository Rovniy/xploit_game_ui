<#
.SYNOPSIS
    Copies the Unity Native Plugin API headers out of an installed Unity Editor
    into native/third_party/unity/PluginAPI/.

.DESCRIPTION
    Those headers are covered by the Unity Companion License, which allows them
    to be used in Unity-dependent projects but not redistributed on their own,
    so this repository does not carry a copy. The build finds them by itself in
    a Unity Hub installation; run this script when you want a local copy pinned
    to a particular editor, or when Unity lives somewhere unusual.

.EXAMPLE
    .\tools\fetch-unity-headers.ps1
    .\tools\fetch-unity-headers.ps1 -UnityVersion 6000.5.1f1
    .\tools\fetch-unity-headers.ps1 -EditorPath 'D:\Unity\6000.5.1f1\Editor'
#>
[CmdletBinding()]
param(
    [string]$UnityVersion,
    [string]$EditorPath,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root 'native\third_party\unity\PluginAPI'
$required = @('IUnityInterface.h', 'IUnityGraphics.h', 'IUnityGraphicsD3D12.h', 'IUnityLog.h')

function Test-PluginApiDir([string]$path) {
    if (-not $path -or -not (Test-Path $path)) { return $false }
    foreach ($h in $required) {
        if (-not (Test-Path (Join-Path $path $h))) { return $false }
    }
    return $true
}

$candidates = New-Object System.Collections.Generic.List[string]
if ($EditorPath) {
    $candidates.Add((Join-Path $EditorPath 'Data\PluginAPI'))
    $candidates.Add((Join-Path $EditorPath 'Unity.app\Contents\PluginAPI'))
}
if ($env:UNITY_EDITOR_PATH) {
    $candidates.Add((Join-Path $env:UNITY_EDITOR_PATH 'Data\PluginAPI'))
}

$hubRoots = @(
    (Join-Path $env:ProgramFiles 'Unity\Hub\Editor'),
    'C:\Program Files\Unity\Hub\Editor',
    '/Applications/Unity/Hub/Editor'
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique

foreach ($hub in $hubRoots) {
    $editors = Get-ChildItem -Path $hub -Directory -ErrorAction SilentlyContinue
    if ($UnityVersion) { $editors = $editors | Where-Object { $_.Name -eq $UnityVersion } }
    foreach ($editor in ($editors | Sort-Object Name -Descending)) {
        $candidates.Add((Join-Path $editor.FullName 'Editor\Data\PluginAPI'))
        $candidates.Add((Join-Path $editor.FullName 'Unity.app\Contents\PluginAPI'))
    }
}

$source = $candidates | Where-Object { Test-PluginApiDir $_ } | Select-Object -First 1
if (-not $source) {
    $hint = if ($UnityVersion) { " matching version $UnityVersion" } else { '' }
    throw @"
No Unity installation$hint was found with the Native Plugin API headers.
Looked in: $($candidates -join '; ')
Install Unity 6000.2 or newer, or point the script at it:
    .\tools\fetch-unity-headers.ps1 -EditorPath '<path>\Editor'
"@
}

if ((Test-PluginApiDir $dest) -and -not $Force) {
    Write-Host "Headers already present in $dest (use -Force to overwrite)."
    exit 0
}

New-Item -ItemType Directory -Force $dest | Out-Null
Get-ChildItem -Path $source -Filter '*.h' | Copy-Item -Destination $dest -Force
$license = Join-Path $source 'LICENSE.md'
if (Test-Path $license) { Copy-Item $license -Destination $dest -Force }

$count = (Get-ChildItem -Path $dest -Filter '*.h').Count
Write-Host "Copied $count headers from $source to $dest"
Write-Host "These files stay under the Unity Companion License and are ignored by git."
