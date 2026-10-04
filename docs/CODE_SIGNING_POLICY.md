# Code signing policy

Gaius is open source, and its Windows program is built from this repository by a public, automated build. This page says what is signed, by whom, and what the program does and does not do with the network. It is the policy the [SignPath Foundation](https://signpath.org/) asks of a project that applies for its free code-signing certificate.

> **Status:** nothing is signed yet. Until the certificate is granted the Windows downloads are unsigned and Windows SmartScreen may ask you to confirm. Once it is, the attribution below goes into the README, which then links here.

## Attribution (once the certificate is granted)

Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

## What is signed

- **`gaius.exe`**, the Windows program of each release, offered alone and inside the Windows zip.
- Nothing else is signed with this certificate. The Linux tarball, the web page and its WebAssembly program have no signature to give (the checksums in each release's `SHA256SUMS` cover them), and the Android app is signed with the project's own Android key.

## How a release is built and signed

1. A release is a commit of this repository tagged `v<version>` (see [`RELEASING.md`](RELEASING.md)). Pushing the tag starts [`.github/workflows/release.yml`](../.github/workflows/release.yml) on GitHub's own runners.
2. The workflow builds `gaius.exe` from that commit's source with MSVC and the pinned vcpkg dependencies, and runs the tests (`gaius_tests`). No binary is built anywhere else for a release, and nothing from outside the repository is copied into it.
3. The workflow sends the built `gaius.exe` to SignPath, which signs it only for a build that GitHub Actions ran from this repository. **Each signing request has to be approved by hand** by the approver named below, who checks that it comes from the tagged release build, before the signature is made.
4. The signed file is returned to the workflow, which puts it in the release.

## Roles

Gaius has one maintainer, who fills every role:

| Role | Who |
|---|---|
| Committer and reviewer | 0x ([github.com/0x695](https://github.com/0x695)) |
| Approver of signing requests | 0x |

Contributors' changes reach a release only through the maintainer's merge of them.

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it. Gaius makes no network connections of its own: it reads the player's own copy of Caesar from disk, and writes its settings and saves to the player's own folder. The browser version runs entirely in the page, and keeps the player's files in the browser's own storage on their machine.

## What the program contains

Gaius is licensed GPL-3.0-or-later. It contains no part of the original game, whose files each player brings. Its third-party components and their licences are in [`THIRD_PARTY_NOTICES.txt`](../THIRD_PARTY_NOTICES.txt), which ships in every download.

## Reporting a problem

A signed file that should not be, or anything else about this policy: [open an issue](https://github.com/0x695/Gaius/issues).
