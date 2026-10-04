# Making a release

A release is a commit tagged `v<version>`. Pushing the tag runs `.github/workflows/release.yml`, which builds every download, checks them, and publishes the GitHub release. Nothing else is done by hand.

## What a release contains

| Download | Built by | Notes |
|---|---|---|
| `gaius-<version>-windows-x64.exe` and `.zip` | `windows` job: MSVC, SDL 2 and the C++ runtime linked in (vcpkg `x64-windows-static`), `packaging/windows/make_zip.ps1` | one `gaius.exe` that needs nothing installed (the language files are built into it), alone and in the zip with a readme, `LICENSE`, `THIRD_PARTY_NOTICES.txt`. Signed when the repository has a signing certificate (below); otherwise unsigned, and Windows SmartScreen warns. |
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

## Windows signing (once)

An unsigned `gaius.exe` is shown by Windows as from an "Unknown publisher" and SmartScreen asks the player to confirm. A signed one shows the publisher's name, and SmartScreen's warning fades as the certificate gains reputation. Gaius uses the [SignPath Foundation](https://signpath.org/)'s free certificate for open-source projects: the publisher Windows shows is "SignPath Foundation", the signing is done by SignPath (the private key never leaves it), and the release workflow only has to send the built exe and wait for the signature.

**To set it up** (the application is the owner's; SignPath's own documentation, [docs.signpath.io](https://docs.signpath.io/), covers each screen):

1. **Release first, unsigned.** SignPath wants a project that is already released: tag `v0.9.0` as usual (steps above) and let it go out unsigned.
2. **Apply** at [signpath.org](https://signpath.org/) with the repository, the release and [`CODE_SIGNING_POLICY.md`](CODE_SIGNING_POLICY.md), which is the policy page they ask for (it names `0x` as committer, reviewer and approver). Their conditions: an OSI-approved licence (GPL-3.0-or-later), no proprietary component, no malware, binaries built from source by an automated build, and each signing request approved by hand. The one thing they may query is the music driver, which follows the source of the Audio Interface Library 2.14 driver, released by its author as freeware (see `THIRD_PARTY_NOTICES.txt`).
3. **In SignPath**, once accepted: create the project (slug `gaius`), link *GitHub.com* as its trusted build system and install the SignPath GitHub App on the repository, paste [`packaging/windows/signpath-artifact-configuration.xml`](../packaging/windows/signpath-artifact-configuration.xml) as the artifact configuration, add a signing policy for releases (slug `release-signing`) with yourself as the approver, and make an API token for a submitter.
4. **In the repository** (*Settings > Secrets and variables > Actions*): the secret `SIGNPATH_API_TOKEN` (that token) and the variable `SIGNPATH_ORGANIZATION_ID`. The variables `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG` and `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` are only needed if you chose other names than `gaius` and `release-signing`, or want a configuration other than the project's default.
5. **Then update the README** with the attribution that `CODE_SIGNING_POLICY.md` carries ("Free code signing provided by SignPath.io, certificate by SignPath Foundation") and a link to that page.

From then on a release (a pushed tag, or a manual run with *publish* on) uploads the built `gaius.exe` to SignPath and **waits up to an hour for you to approve the request** in SignPath; open the link in the job's log, check that it is the release you meant, and approve it. A dry run (publish off) never asks for a signature and stays unsigned. The signature's status is printed at the end of the `windows` job either way.

**Another certificate.** A certificate you hold as a `.pfx` file (an organisation's own, or one a CA lets you export) works through two secrets, used when SignPath is not set up: `GAIUS_SIGN_PFX_BASE64` (`base64 -w0 gaius.pfx`) and `GAIUS_SIGN_PFX_PASSWORD`. `packaging/windows/sign.ps1` does the signing (signtool, SHA-256, an RFC 3161 timestamp so the signature outlives the certificate) and works the same on your machine: `pwsh packaging/windows/sign.ps1 -File gaius.exe -Pfx gaius.pfx -Password ...`. Most certificate authorities now keep the private key on hardware, which a `.pfx` cannot express; [Azure Artifact Signing](https://azure.microsoft.com/products/artifact-signing/) (about US$10 a month; individuals in the USA and Canada, organisations in the USA, Canada, the EU and the UK) is the paid alternative to SignPath and would be a step of its own in the `windows` job, not wired.

Keep any certificate, token and password outside the repository except as a secret, like the Android keystore.

## The web site

The GitHub Pages site (`.github/workflows/pages.yml`) is rebuilt on every push to `master`, so it shows the newest work, not the last release. If you want a stable channel, deploy the site from the release workflow instead (or publish the `gaius-<version>-web.zip` to a second location).
