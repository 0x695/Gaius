# Making a release

A release is a commit tagged `v<version>`. Pushing the tag runs `.github/workflows/release.yml`, which builds every download, checks them, and publishes the GitHub release. Nothing else is done by hand.

## What a release contains

| Download | Built by | Notes |
|---|---|---|
| `gaius-<version>-windows-x64.zip` | `windows` job: MSVC, SDL 2 and the C++ runtime linked in (vcpkg `x64-windows-static`), `packaging/windows/make_zip.ps1` | one `gaius.exe` that needs nothing installed, plus `lang/`, a readme, `LICENSE`, `THIRD_PARTY_NOTICES.txt`. Not code-signed, so Windows SmartScreen may warn. |
| `gaius-<version>-linux-x86_64.tar.gz` | `linux` job (Ubuntu 22.04), `packaging/linux/make_tarball.sh` | the program, launcher, desktop entry, icon, Steam Deck notes. Needs the system's SDL 2. |
| `gaius-<version>-web.zip` | `web` job: Emscripten 6.0.11 | the page of [`web/`](../web/README.md), to host yourself; the hosted copy is the GitHub Pages site. |
| `gaius-<version>-android.apk` and `.aab` | `android` job | signed, if the repository has the keystore secrets (below); otherwise `gaius-<version>-android-debug.apk`, debug-signed and installable for testing. This job may fail without stopping the release. |
| `SHA256SUMS` | `publish` job | checksums of everything above. |

The release notes are the section of [`CHANGELOG.md`](../CHANGELOG.md) that carries the version, followed by a short list of downloads. A `0.x` version is published as a pre-release.

## Steps

1. **Choose the version** and put it, as `x.y.z`, in [`VERSION.txt`](../VERSION.txt). CMake, the Windows resources, the Android build, the Settings screen, `--version` and the web page all read that file.
2. **Write the notes.** In `CHANGELOG.md` rename `## [Unreleased]`'s content into a new `## [x.y.z] - YYYY-MM-DD` section (and leave an empty `## [Unreleased]` above it). The release stops, before building anything, if the version has no section.
3. **Commit** both files (`Release x.y.z`), and let the `build` workflow pass on `master`.
4. **Dry run** (optional but worth it for a new platform): *Actions > release > Run workflow* with *publish* off builds everything and keeps the files as the run's artifacts, publishing nothing. The version it prints is `x.y.z-dev+<commit>` since it is not on a tag.
5. **Tag and push:**

   ```sh
   git tag -a vX.Y.Z -m "Gaius X.Y.Z"
   git push origin vX.Y.Z
   ```

   The workflow refuses a tag that is not `v` followed by the contents of `VERSION.txt`. When it finishes the release is at *Releases* with the files and the notes.
6. **Afterwards:** check the download links, run the Windows zip on a machine without Visual Studio, and bump `VERSION.txt` to the next number when the next change lands (the builds from `master` then call themselves `x.y.z-dev+<commit>`).

A mistake in a published release: fix it, delete the release and the tag (`gh release delete vX.Y.Z --cleanup-tag`), and tag again; or re-run the workflow, which replaces the files of a release that already exists.

## Android signing (once)

Without a key the workflow builds a debug APK. For a signed release, make a keystore (`keytool -genkeypair -v -keystore gaius.keystore -alias gaius -keyalg RSA -keysize 2048 -validity 10000`), **keep it and its passwords somewhere safe outside the repository** (a lost key means a new app for the stores), and add these repository secrets (*Settings > Secrets and variables > Actions*):

| Secret | Holds |
|---|---|
| `GAIUS_KEYSTORE_BASE64` | the keystore file, base64: `base64 -w0 gaius.keystore` |
| `GAIUS_KEYSTORE_PASSWORD` | the store password |
| `GAIUS_KEY_ALIAS` | the key alias (`gaius`) |
| `GAIUS_KEY_PASSWORD` | the key password |

The Android `versionName` is the version and `versionCode` is `major * 10000 + minor * 100 + patch`, so every release is a higher number than the one before.

## The web site

The GitHub Pages site (`.github/workflows/pages.yml`) is rebuilt on every push to `master`, so it shows the newest work, not the last release. If you want a stable channel, deploy the site from the release workflow instead (or publish the `gaius-<version>-web.zip` to a second location).
