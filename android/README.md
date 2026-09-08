# Gaius — Android target

Closes the last open item from `GAIUS_ROADMAP.md` Phase 1: "CMake toolchain
files for Android (NDK) and confirm a clean build + 'hello sprite' run on at
least one non-desktop target."

## What this is (and isn't)

`app/jni/src/` builds `apps/android_hello/android_main.cpp` against the real,
unmodified `platform/window.cpp` + `platform/input.cpp` + `platform/paths.cpp`
-- not a reimplementation of them. It draws a moving gradient + a bouncing
colored square through `Window::present_rgb24`, the same call `apps/viewer`
uses on desktop. That's it -- this is a platform-layer smoke test, not a port
of `gaius_viewer`. Loading a real `EMPIRE2`/`.SAV` asset on Android needs
`AAssetManager`-based extraction first (Android can't `fopen()` into an APK
the way desktop can), which is real follow-up work, not done here.

Verified against the `Medium_Phone` AVD (x86_64, API level per its own
`config.ini`) with NDK 29.0.14206865: app builds, installs, launches, SDL
finds and runs `main()`, `platform::paths` resolves Android's app-private
storage correctly (confirmed via logcat), and a screenshot
(`adb exec-out screencap -p`) shows the letterboxed content rendering
correctly. `abiFilters` in `app/build.gradle` is currently `x86_64` only,
matching that emulator -- widen it (`arm64-v8a` at minimum) before targeting
a real device.

## Prerequisites

- Android SDK with `cmdline-tools`, `platform-tools`, and an AVD (or real
  device). This was set up against an existing SDK at
  `%LOCALAPPDATA%\Android\Sdk` that already had Android Studio, build-tools,
  and a `Medium_Phone` AVD -- adjust paths below if yours differs.
- NDK, installed via `sdkmanager --install "ndk;<version>"` (pin whatever
  version you install in `app/build.gradle`'s `ndkVersion`).
- A **JDK 17** on `JAVA_HOME` for Gradle -- not whatever JDK your IDE bundles
  if it's newer. Gradle 8.1.1 (this project's wrapper version) doesn't
  support arbitrarily new JDKs; a bundled JDK 20+ (e.g. Android Studio's JBR)
  can fail in ways that look like a build config problem but aren't. Eclipse
  Temurin 17 is what this was verified with.
- `android/app/jni/SDL/` populated by `fetch_sdl2.ps1` (below) -- not
  committed, see `.gitignore`.

## Build

```powershell
powershell -File android/fetch_sdl2.ps1   # one-time: fetches SDL2 source into app/jni/SDL/

$env:JAVA_HOME = "<path to a JDK 17>"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
cd android
.\gradlew.bat assembleDebug
```

Output: `app/build/outputs/apk/debug/app-debug.apk`.

## Run (emulator or device via adb)

```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n org.gaius.hello/org.libsdl.app.SDLActivity
adb exec-out screencap -p > screenshot.png   # sanity check without touching the emulator UI
```

## Layout

```text
android/
  app/jni/SDL/        fetched by fetch_sdl2.ps1, gitignored -- SDL2 source, built from source (no prebuilt Android SDL2 the way vcpkg gives desktop one)
  app/jni/CMakeLists.txt      -- SDL's own top-level jni CMakeLists (from the SDL2 android-project template, unmodified)
  app/jni/src/CMakeLists.txt  -- Gaius-specific: builds android_main.cpp + platform/*.cpp as libmain.so
  app/src/main/java/org/libsdl/app/   -- SDL2's stock Java glue (SDLActivity etc.), copied verbatim from the SDL2 android-project template
  app/src/main/AndroidManifest.xml    -- edited: activity name is fully-qualified (org.libsdl.app.SDLActivity) since the app namespace (org.gaius.hello) differs from that package
```

The rest (`build.gradle`, `gradlew`, `gradle/`) is the stock SDL2
`android-project` template from the SDL2 source release, lightly edited
(namespace/applicationId, `ndkVersion` pin, switched `externalNativeBuild`
from `ndkBuild` to `cmake`, `abiFilters` narrowed to `x86_64`).

## Third-party licensing

Everything under `app/src/main/java/org/libsdl/app/`, `app/jni/CMakeLists.txt`,
`app/jni/Android.mk`, `app/jni/Application.mk`, `gradlew`, `gradlew.bat` and
`gradle/` originates from **SDL2**, which is distributed under the **zlib
license** — Copyright (C) 1997-2024 Sam Lantinga. That license permits
redistribution in modified form provided the origin is not misrepresented and
altered versions are marked as such; the "Layout" table above and this note
serve that purpose. SDL2's full license text ships in the source tree that
`fetch_sdl2.ps1` downloads (`app/jni/SDL/LICENSE.txt`, gitignored along with
the rest of that checkout).

Note this is *unrelated* to the project's rule about original Caesar assets
(see `CLAUDE.md`) — SDL2 is freely redistributable and is vendored
deliberately; game assets are neither and are never committed.
