# Pedalboard 3

A modular, live-performance VST plugin host — a ground-up rebuild of
Niall Moody's [Pedalboard2](https://www.niallmoody.com/work/pedalboard2/)
(JUCE/C++, GPLv3, 2013) with modern tooling and plugin formats.

- **Stack:** JUCE 8, CMake, C++20, Catch2, GitLab CI
- **Plugin formats:** VST3; AU on macOS; LV2 on macOS and Linux. CLAP
  (`PB3_ENABLE_CLAP`, off in every preset) and VST2 (`PB3_ENABLE_VST2`, needs
  your own SDK) are opt-in build flags.
- **Targets:** Windows (primary), macOS, Linux

## Status

The work of milestones M1 to M6 has merged: stability, live MIDI/OSC control,
multi-patch banks with Program Change switching, the mappings UI, hardening
(crash guard, `.pdl` safety) and the parity tail (transport, File Player,
Recorder, Looper, Metronome). So have the finish pass (file workflow, window
memory, undo), the UI refresh and the UI chrome pass. Next is the first public
beta, 1.0.0-beta.1 (M7). The version, the release pipeline, the Windows
installer and the portable zip are in place. The beta now waits on the
hands-on pass and a few decisions.

- **Where things stand and what's next:**
  [`docs/status/2026-09-14-handoff.md`](docs/status/2026-09-14-handoff.md)
- **Open work:** the GitLab milestones M7 (public beta), M8 (v1.0.0) and 1.x.
- **Releases:** made by hand, from main only. See
  [`docs/releases/README.md`](docs/releases/README.md).

The roadmap is
[`docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md`](docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md),
and the master plan is
[`docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md`](docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md).

## Build

### Windows

From any PowerShell. The script loads the Visual Studio developer environment
itself; it needs Visual Studio with the C++ workload.

```powershell
./build.ps1 build                  # configure + build, Debug (windows-dev)
./build.ps1 build -Config release  # RelWithDebInfo (windows-ci)
./build.ps1 test                   # build, then run the unit tests
./build.ps1 run                    # launch the built app
./build.ps1 snapshots              # render every menu and dialog to build/ui-snapshots
./build.ps1 package                # release build, then the portable zip (and the installer, with Inno Setup)
./build.ps1 configure              # configure only
./build.ps1 clean                  # remove this config's build folder
```

ASIO is optional: see "Windows: ASIO" in [AGENTS.md](AGENTS.md).

### Linux and macOS

`just` picks the host's preset: `linux-dev` on Linux, `macos-dev` on macOS.
The `Makefile` has the same build, test, run, release, format and clean
targets for machines without `just`. On macOS you need Xcode's command-line
tools (`xcode-select --install`) and `brew install cmake ninja just pkg-config`.

```bash
just build          # configure + build the host's dev preset
just test           # run the unit tests with ctest
just run            # launch the app
just release        # RelWithDebInfo on Linux, universal RelWithDebInfo on macOS
```

Or with CMake directly:

```bash
cmake --preset linux-dev -S app-v3
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

On Linux you need CMake 3.25+, Ninja, a C++20 compiler (CI uses clang-18) and
JUCE 8's Linux libraries. CI builds inside the silkOps factory image
(`ci-cd/juce-linux`), which installs these for it (Ubuntu package names):

```
build-essential pkg-config clang-18
libasound2-dev libjack-jackd2-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
libxext-dev libxcomposite-dev libxrender-dev
libfreetype-dev libfontconfig1-dev
libgl-dev libglu1-mesa-dev mesa-common-dev
```

The image also carries libcurl and WebKit, which this app doesn't need: it
builds with `JUCE_USE_CURL=0` and `JUCE_WEB_BROWSER=0` (`app-v3/CMakeLists.txt`).

## Repo layout

```
.
├── AGENTS.md                # conventions for coding agents
├── CLAUDE.md                # points to AGENTS.md, plus the shipping rules
├── app-v3/                  # the v3 source tree
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── cmake/               # CPM.cmake (fetches JUCE and the other dependencies)
│   ├── resources/           # embedded fonts (SIL OFL)
│   ├── src/                 # engine/, session/, control/, ui/, platform/, tools/
│   └── tests/               # Catch2: unit/ mirrors src/, support/ holds helpers
├── docs/
│   ├── plans/               # product plans (ce-plan output)
│   ├── releases/            # release notes, and how to cut a release
│   ├── reviews/
│   └── status/              # dated handoff and status notes
├── legacy/                  # Pedalboard2 end-user docs, readme, to-do
├── source code/             # READ-ONLY original Pedalboard2 v2 code
├── vercel-ui-demo/          # UI mockup (design source)
├── .gitlab-ci.yml
├── installer/windows/       # the Inno Setup script for the Windows installer
├── tools/                   # packaging scripts (package.ps1, package.sh)
├── build.ps1                # Windows build / test / run / snapshots / package
├── justfile, Makefile       # build wrappers (Linux / macOS)
└── CMakePresets.json
```

## Licence

Pedalboard 3 is free software under the **GNU Affero General Public License,
version 3** — see [LICENSE](LICENSE). Copyright (c) 2026 OpenZephyr LLC.

That means you can use, study, change and share it, and anything you build on it
has to stay under the same licence. The "Affero" part adds one condition beyond
the GPL: if you run a modified version as a network service, its users are
entitled to its source too.

AGPLv3 is not an arbitrary pick. JUCE 8's open-source track is AGPLv3, the VST3
SDK that JUCE 8.0.4 bundles is Steinberg-proprietary or GPLv3, and the original
Pedalboard2 was GPLv3-or-later. AGPLv3 is the licence all three fit under.

- **What everything else is licensed under:**
  [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). It covers JUCE, the VST3 and
  LV2 SDKs, the embedded fonts, the test and build tooling, and the optional
  ASIO SDK, and records which option is taken where a dependency offers a choice.
- **The original Pedalboard2** (`source code/`) stays under its own GPLv3-or-later
  licence (Niall Moody, 2009-2013). It is kept for reference only: nothing in it
  is compiled and nothing in it is shipped.

### Contributing, and paid features

Contributions keep their own copyright and are taken under the AGPLv3 with a
[Developer Certificate of Origin](CONTRIBUTING.md) sign-off — a one-line
`git commit -s`, not a CLA. Nobody gets the right to relicense your code.

Future paid features are built as separate components alongside this
application, not as code inside it, so the app in this repository stays AGPLv3
and complete. Where a closed component would need different licensing from
Steinberg or JUCE, that is spelled out in the third-party notices.
