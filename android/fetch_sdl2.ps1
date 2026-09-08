# Gaius — android/fetch_sdl2.ps1
#
# Downloads SDL2 source into app/jni/SDL/, which the Android build expects
# to find there (see app/jni/CMakeLists.txt's add_subdirectory(SDL)).
# Not vendored/committed -- same reasoning as desktop not vendoring SDL2
# (CMakeLists.txt finds it via vcpkg instead): keeps the repo lean, and
# the exact source needed depends on which SDL2 release you're building
# against. Pinned to the same release desktop currently builds against
# (vcpkg sdl2:x64-windows@2.32.10 as of this writing) so both platforms
# stay on the same SDL2 version -- bump both together, not independently.
#
# Usage: powershell -File android/fetch_sdl2.ps1

$ErrorActionPreference = "Stop"
$version = "2.32.10"
$url = "https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL2-$version.zip"
$androidRoot = $PSScriptRoot
$destJniSdl = Join-Path $androidRoot "app\jni\SDL"

if (Test-Path (Join-Path $destJniSdl "CMakeLists.txt")) {
    Write-Output "SDL2 source already present at $destJniSdl -- skipping. Delete that directory to re-fetch."
    exit 0
}

$tmpZip = Join-Path $env:TEMP "sdl2-android-src-$version.zip"
$tmpExtract = Join-Path $env:TEMP "sdl2-android-src-$version"

Write-Output "Downloading SDL2 $version source..."
Invoke-WebRequest -Uri $url -OutFile $tmpZip -UserAgent "Mozilla/5.0"

Write-Output "Extracting..."
if (Test-Path $tmpExtract) { Remove-Item $tmpExtract -Recurse -Force }
Expand-Archive -Path $tmpZip -DestinationPath $tmpExtract -Force

New-Item -ItemType Directory -Force -Path $destJniSdl | Out-Null
Get-ChildItem (Join-Path $tmpExtract "SDL2-$version") -Exclude "android-project" |
    Copy-Item -Destination $destJniSdl -Recurse -Force

Remove-Item $tmpZip -Force
Remove-Item $tmpExtract -Recurse -Force

Write-Output "Done: SDL2 $version source is at $destJniSdl"
