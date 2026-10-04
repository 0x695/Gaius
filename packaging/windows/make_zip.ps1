# SPDX-License-Identifier: GPL-3.0-or-later
# Packs a Windows build into <Name>.zip: gaius.exe (the language files are built into it), the readme, the licence
# and the third-party notices. No game files: players bring their own copy of Caesar. A static build (vcpkg's x64-windows-static, the
# release workflow's) is one program that needs nothing installed; a dynamic one brings SDL2.dll along.
#
#   pwsh packaging/windows/make_zip.ps1 -Build build/Release -Name gaius-0.9.0-windows-x64
#
# The zip is written to the current folder; it holds one folder called <Name>.
param(
    [Parameter(Mandatory = $true)][string]$Build,
    [Parameter(Mandatory = $true)][string]$Name
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '..\..')).Path
$build = (Resolve-Path $Build).Path

$stage = Join-Path ([IO.Path]::GetTempPath()) ('gaius-pack-' + [guid]::NewGuid().ToString('N'))
$dir = Join-Path $stage $Name
New-Item -ItemType Directory -Force $dir | Out-Null

Copy-Item (Join-Path $build 'gaius_viewer.exe') (Join-Path $dir 'gaius.exe')
$sdl = Join-Path $build 'SDL2.dll'
if (Test-Path $sdl) { Copy-Item $sdl $dir }
Copy-Item (Join-Path $here 'README.txt') $dir
Copy-Item (Join-Path $root 'LICENSE') $dir
Copy-Item (Join-Path $root 'THIRD_PARTY_NOTICES.txt') $dir

$zip = Join-Path (Get-Location).Path ($Name + '.zip')
if (Test-Path $zip) { Remove-Item $zip }
# Entry names with forward slashes, whatever the PowerShell (Windows PowerShell 5.1 would write backslashes).
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem $dir -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($dir.Length).TrimStart([char]92, [char]47).Replace([string][char]92, '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, "$Name/$relative",
                                                                    [IO.Compression.CompressionLevel]::Optimal)
    }
} finally {
    $archive.Dispose()
}
Remove-Item -Recurse -Force $stage
Write-Output $zip
