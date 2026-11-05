# Releases

Each release has its notes here, named `v<version>.md`; for example, `v1.0.0-beta.1.md` is the notes for the tag `v1.0.0-beta.1`. The release pipeline puts them on the GitLab Release, and in every package as `Release notes.md`.

Releases are made by hand, from main only. No merge request or pushed tag starts one.

## Cutting a release

1. **Set the version** in `app-v3/CMakeLists.txt`: the numbers in `project(... VERSION x.y.z)`, and the label in `PB3_VERSION_SUFFIX` (`beta.1`, `rc.1`, or empty for a plain release). Merge it like any other change.
2. **Write the notes** as `docs/releases/v<version>.md` and merge them. The publish step refuses to run without them.
3. **Start a pipeline on main**: Build > Pipelines > Run pipeline, branch `main`. It holds only the release jobs, and each one waits for a click.
4. **Click `release:linux`, `release:macos` and `release:windows`.** Each builds, packages and keeps its packages as job artifacts. `release:windows` also runs the unit tests. You can try the packages from here before publishing.
5. **Once all three have passed, click `release:gitlab`.** It does these steps in order:
   - checks that the notes and all four packages are there;
   - uploads the packages to the project's package registry (`pedalboard3/<version>`);
   - creates the tag `v<version>` on that main commit;
   - makes the GitLab Release, with the notes and a download link for each package.

   GitLab has no pre-release flag, so the notes say it's a beta. `release:github` marks the release as a pre-release there.
6. **Click `release:github`**, if the mirror is set up (see below). It copies the release to GitHub with the same notes and the same seven downloads.
7. **Try the downloads on a clean machine** (#87). On Windows, run the installer and the zip on a PC without Visual Studio.

A maintainer does steps 3 to 6. The tag comes from `release:gitlab`, never from a pushed tag.

## The GitHub mirror

GitLab is the source of truth: the code, the pipelines and the releases are made here. GitHub is a copy, so people who look for the app there find it.

Two halves, because GitLab's push mirroring copies commits and tags but not Releases or their downloads:

- **The code.** GitLab push-mirrors `main` and the tags into the GitHub repository, set up once under Settings > Repository > Mirroring repositories.
- **The releases.** `release:github` makes the matching GitHub Release through the API and uploads the same packages to it.

Every release is mirrored, betas included. A version with a pre-release suffix (`1.0.0-beta.1`) is marked as a pre-release on GitHub; a plain `1.0.0` is a full release.

Because of that, **the README links to the releases index, not `/releases/latest`.** GitHub defines "latest" as the most recent release that is *not* a pre-release, so while every release carries a suffix that URL answers 404 and the browser lands on the index anyway. Once a plain version ships, `/releases/latest` starts resolving and becomes the better link.

The job only appears when both of these CI variables exist, so a fork or a clone that doesn't mirror never sees it and never fails because of it:

| Variable | What it holds |
|---|---|
| `GITHUB_REPO` | The mirror, as `owner/name`. Protected. |
| `GITHUB_TOKEN` | A fine-grained GitHub PAT with **Contents: read and write** on that repository, and nothing else. Masked and protected. |

Clicking `release:github` again is safe: it updates the release it already made and replaces any download of the same name, rather than making a second one. Because push mirroring runs on its own schedule, the job first waits up to five minutes for the release commit to reach GitHub, and says so if it doesn't.

## The packages

| Platform | File | What it is |
|---|---|---|
| Windows | `Pedalboard3-<version>-windows-x64-setup.exe` | The installer. It puts the app in `C:\Program Files\Pedalboard 3` and adds a Start menu entry and an uninstaller. |
| Windows | `Pedalboard3-<version>-windows-x64.zip` | The portable app. Unzip it anywhere and run `Pedalboard 3.exe`. |
| macOS | `Pedalboard3-<version>-macos-universal.zip` | `Pedalboard 3.app`, for Apple Silicon and Intel. |
| Linux | `Pedalboard3-<version>-linux-x64.tar.gz` | `Pedalboard 3` and its plugin scanner helper. |

Each platform also publishes a debug-symbols archive beside its package: `-symbols.zip` on Windows, `-symbols.tar.gz` on macOS and Linux. They are what makes a crash report from that build readable, and they only work for that build, because symbols are matched by the PDB's GUID, the dSYM's UUID or the ELF build id. They are kept out of the app packages, since most people never need them. The Windows archive is large: the PDBs are around 500 MB before compression.

Every package also holds the licence (`LICENSE.txt`, AGPLv3), the third-party notices (`THIRD-PARTY-NOTICES.md`), the embedded fonts' licences (`licences/`) and the release notes. Nothing is signed yet (#40), so Windows SmartScreen and macOS Gatekeeper warn the first time you open the app.

To make packages locally, without releasing anything:
- `./build.ps1 package` makes the Windows zip, and the installer too if Inno Setup is installed.
- `tools/package.sh build/<preset>` makes the macOS or Linux package.
