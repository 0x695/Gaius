# Gaius — Android

The whole game for Android phones and tablets: the same `apps/viewer/main.cpp`
and libraries as the desktop build, compiled as SDL's `libmain.so` by the
repository's own CMake project (`GAIUS_GAME_LIBRARY`, see
`app/jni/src/CMakeLists.txt`). Package `org.gaius.game`, ABIs `arm64-v8a`
(devices) and `x86_64` (the emulator), landscape, fullscreen.

## Playing

Gaius ships no game files. On first start it asks for them: **Import** opens
Android's folder picker (in the Download folder, where a game copied to the phone
usually is); choose the Caesar folder, or the GOG folder that holds it. Gaius looks
in what you chose for the folder with Caesar's US files (GOG keeps them in a folder
called `US`, beside the international release, which Gaius cannot play), and copies
that folder into its own storage, `Android/data/org.gaius.game/files/game`. If it
finds none it says so. Files can also be copied there over USB, in the US folder or
a folder holding it. No storage permission is asked for, which is also why Gaius
cannot search the phone by itself: Android lets an app see only what you hand it.

A touch screen gets a one-time page on the controls:

| Gesture | Does |
|---|---|
| Tap | Choose a building, press a button. On the map with a building chosen, the first tap shows it there with its cost (a finger cannot hover); a tap on that building builds it, a tap elsewhere moves the preview |
| One finger drag | Scroll the map; with Road, Wall, Plaza or Clear Area (or a province road, wall or highway), lay it along the drag, after which the tool puts itself away so the next drag scrolls |
| **Undo** (appears after a built drag) | Takes the drag back and refunds it, as the original's right button during a drag |
| Two fingers drag / pinch | Scroll / zoom |
| Two finger tap | Back (the right mouse button) |
| Back button | Settings; again, leaves them |

Settings (speed, sound, load, save, language, controls) are the same screen as
on the desktop, with a Back button on every tab. The toolbar is the original's
one-row bar on a phone-sized screen (the 2x flat list is kept for the Steam Deck and
for smaller screens, and the Settings screen's UI scale chooses any), and a finger
may press anywhere in a button's slot. Gamepads work as on the Steam Deck
(`packaging/linux/STEAM_DECK.md`).

Verified on the `Medium_Phone` emulator (x86_64, 2400 x 1080): the import
of a GOG top folder through the folder picker (the US folder inside it found and
copied alone), a new career, the one-row toolbar, a building previewed by one tap
and built by the second, a touch-laid road with its Undo and the tool put away
after it, and Settings opened and left from the start screen.
Not yet on a physical arm64 device.

## Prerequisites

- Android SDK with `cmdline-tools`, `platform-tools`, and an AVD or a device.
- NDK `29.0.14206865` (`sdkmanager --install "ndk;29.0.14206865"`), pinned in
  `app/build.gradle`.
- **JDK 17** on `JAVA_HOME` for Gradle 8.1.1 (a newer bundled JBR fails in
  ways that look like build configuration problems).
- `android/app/jni/SDL/` populated by `fetch_sdl2.ps1` (not committed).

## Build

```powershell
powershell -File android/fetch_sdl2.ps1   # one-time: SDL2 source into app/jni/SDL/

$env:JAVA_HOME = "<path to a JDK 17>"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
cd android
.\gradlew.bat assembleDebug               # app/build/outputs/apk/debug/app-debug.apk
.\gradlew.bat assembleRelease bundleRelease
```

A release is signed when `GAIUS_KEYSTORE` names a keystore, with
`GAIUS_KEYSTORE_PASSWORD`, `GAIUS_KEY_ALIAS` and `GAIUS_KEY_PASSWORD`; without
them `app-release-unsigned.apk` and the bundle `app-release.aab` are unsigned
(sign them with `apksigner` / `jarsigner` before installing or uploading).

## Installing, and Play Protect

An APK from outside the Play Store is "sideloaded", and Google Play Protect
checks it: it may ask to scan the app, or warn that the developer is unknown (tap
*More details*, then *Install anyway*). It does this hardest for an app signed
with a key it has never seen. The APK the release workflow builds without the
signing secrets is signed with a debug key that is new on every run, so each build
looks like a new, unknown developer (and cannot update the one before: uninstall
first, which also removes the imported game files). A release signed with one
stable key (docs/RELEASING.md, "Android signing") is recognised from the second
build on. Play Protect's verdict on a new key is not something a build can set; the
Play Store (or F-Droid) is the way to no warning at all.

## Run

```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n org.gaius.game/org.gaius.game.GaiusActivity
adb exec-out screencap -p > screenshot.png
```

## Layout

```text
android/
  app/jni/SDL/          fetched by fetch_sdl2.ps1, gitignored -- SDL2 source, built from source
  app/jni/CMakeLists.txt        SDL's jni CMakeLists (from the SDL2 android-project template, unmodified)
  app/jni/src/CMakeLists.txt    Gaius: adds the repository's CMake project with GAIUS_GAME_LIBRARY
  app/src/main/java/org/libsdl/app/   SDL2's Java glue, verbatim from the template
  app/src/main/java/org/gaius/game/GaiusActivity.java   the activity: libraries, the folder import
  app/src/main/AndroidManifest.xml    edited: GaiusActivity, landscape
```

The language files (`lang/`) are built into `libmain.so` (`ui/embedded_lang.hpp`),
not shipped as assets. The rest (`build.gradle`, `gradlew`, `gradle/`) is
the stock SDL2 `android-project` template, edited: namespace and application
id, `ndkVersion`, CMake instead of ndk-build, ABIs, signing.

The launcher icon is Gaius's own (`packaging/icon/make_icon.py` writes the five sizes under `app/src/main/res/mipmap-*`).

## Third-party licensing

Everything under `app/src/main/java/org/libsdl/app/`, `app/jni/CMakeLists.txt`,
`app/jni/Android.mk`, `app/jni/Application.mk`, `gradlew`, `gradlew.bat` and
`gradle/` originates from **SDL2**, distributed under the **zlib license** —
Copyright (C) 1997-2024 Sam Lantinga. That license permits redistribution in
modified form provided the origin is not misrepresented and altered versions
are marked as such; the "Layout" section above and this note serve that
purpose. SDL2's full license text ships in the source tree `fetch_sdl2.ps1`
downloads (`app/jni/SDL/LICENSE.txt`).

This is unrelated to the project's rule about original Caesar assets (see
`CLAUDE.md`): SDL2 is freely redistributable; game assets are neither and are
never committed or bundled.
