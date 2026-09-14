# Releases

Each release has its notes here as `v<version>.md`: `v1.0.0-beta.1.md` for the tag `v1.0.0-beta.1`. The release pipeline puts them on the GitLab Release, and in every package as `Release notes.md`.

## Cutting a release

1. **Set the version** in `app-v3/CMakeLists.txt`: the numbers in `project(... VERSION x.y.z)`, and the label in `PB3_VERSION_SUFFIX` (`beta.1`, `rc.1`, or empty for a plain release). Merge it like any other change.
2. **Write the notes** as `docs/releases/v<version>.md` and merge them. A tag without its notes fails the pipeline.
3. **Tag the merged commit on main** and push the tag: `git tag v<version>`, then `git push origin v<version>`. A maintainer does this; agents never push tags.
4. **The tag pipeline runs by itself:**
   - `release:linux`, `release:macos` and `release:windows` build, package, and upload to the project's package registry (`pedalboard3/<version>`). `release:windows` runs the unit tests as well. Each job checks that the tag matches the build's version.
   - `release:gitlab` makes the GitLab Release, with these notes and a download link for each package. GitLab has no pre-release flag, so the notes say it's a beta. The GitHub mirror (#89) will mark the release as a pre-release there.
5. **Try the downloads on a clean machine** (#87). On Windows, run the installer and the zip on a PC without Visual Studio.

## The packages

| Platform | File | What it is |
|---|---|---|
| Windows | `Pedalboard3-<version>-windows-x64-setup.exe` | The installer: the app goes in `C:\Program Files\Pedalboard 3`, with a Start menu entry and an uninstaller. |
| Windows | `Pedalboard3-<version>-windows-x64.zip` | The portable app: unzip it anywhere and run `Pedalboard 3.exe`. |
| macOS | `Pedalboard3-<version>-macos-universal.zip` | `Pedalboard 3.app`, for Apple Silicon and Intel. |
| Linux | `Pedalboard3-<version>-linux-x64.tar.gz` | `Pedalboard 3` and its plugin scanner helper. |

Every package also holds the third-party licences (`licences/`) and the release notes. Nothing is signed yet (#40), so Windows SmartScreen and macOS Gatekeeper warn the first time you open it.

To try packages without a tag:
- `./build.ps1 package` makes the Windows zip locally, and the installer too if Inno Setup is installed.
- `tools/package.sh build/<preset>` makes the macOS or Linux package.
- In a merge request, or in a pipeline started by hand on main, each release job is one click.
