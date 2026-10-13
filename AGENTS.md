# AGENTS.md — Pedalboard 3

Guidance for coding agents (Claude Code, Codex, etc.) working in this repo.

## What this is

A modern cross-platform VST plugin host, rebuilt from scratch to honor
Niall Moody's original Pedalboard2 (JUCE/C++, GPLv3, 2013). See the full plan:

- `docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md`

New code lives under `app-v3/`. The `source code/` directory holds the original
Pedalboard2 v2 codebase and is **read-only** — reference for behavioral parity
and `.pdl` format only; do not edit.

## Stack

- **Engine & UI:** JUCE 8 + CMake, C++20 (targeting C++23 features where portable)
- **Plugin formats:** VST3 (MIT SDK); AU on macOS; LV2 on macOS and Linux.
  CLAP (vendored `juce_clap_hosting` fork, `PB3_ENABLE_CLAP`, off in every
  preset) and VST2 (`PB3_ENABLE_VST2`, needs your own SDK) are opt-in.
- **Tests:** Catch2 v3 unit tests, headless (`app-v3/tests/`)
- **CI:** GitLab CI; see the `workflow:` block in `.gitlab-ci.yml`.
  - Every merge request push runs a pipeline: lint, then build and unit tests
    on Linux, Windows and macOS.
  - On main a pipeline runs only when someone starts it by hand (Build >
    Pipelines > Run pipeline). It holds the release jobs, each a manual
    click: the platform builds, then release:gitlab, which tags the commit
    and publishes (`docs/releases/README.md`). A merge starts no pipeline,
    and neither does a pushed tag: releases are made by hand, from main only.
  - Linux runs on shared runners inside the silkOps factory image
    (`ci-cd/juce-linux`). Windows and macOS run on GitLab SaaS runners, with
    `allow_failure`.

## Build

Host-OS-aware via `just` — the default recipes pick `linux-dev` on Linux and
`macos-dev` on macOS:

```bash
just build       # configure + build host dev preset
just test        # ctest against the host dev build
just run         # launch the app (execs the binary on Linux, `open`s the .app on macOS)
just format      # clang-format -i on app-v3/src + tests
just release     # RelWithDebInfo on Linux, Universal RelWithDebInfo on macOS
```

Explicit preset-picking when you need to override:

```bash
# Linux Debug (headless / CI-style)
cmake --preset linux-dev -S app-v3
cmake --build build/linux-dev
ctest --test-dir build/linux-dev --output-on-failure

# macOS Debug (host arch; fast iteration)
cmake --preset macos-dev -S app-v3
cmake --build build/macos-dev
ctest --test-dir build/macos-dev --output-on-failure
open "build/macos-dev/src/Pedalboard3_artefacts/Debug/Pedalboard 3.app"

# macOS Release (universal x86_64 + arm64)
cmake --preset macos-universal -S app-v3
cmake --build build/macos-universal
```

Windows and Linux CI both build through `cmake --preset` as well; see
`.gitlab-ci.yml`. The Linux jobs run inside the silkOps factory image
`registry.gitlab.com/void-realm-solutions/ci-cd/juce-linux:<tag>` (recipe in
the `ci-cd` repo under `images/juce-linux/`); the tag is pinned in
`CI_LINUX_IMAGE` and bumped by a one-line MR.

If `just` isn't available, the repo root carries equivalent wrappers over the
same presets:

```bash
make build       # Linux/macOS: configure + build the host dev preset
make test        # ctest against the host dev build
make run         # launch the app (open .app on macOS, exec on Linux)
make release     # optimized preset for the host OS
```

```powershell
# Windows — auto-loads the VS developer environment (cl.exe + Ninja on PATH),
# so it works from any PowerShell, not only an "x64 Native Tools" prompt:
./build.ps1 build                  # Debug (windows-dev)
./build.ps1 build -Config release  # RelWithDebInfo (windows-ci)
./build.ps1 test                   # build + ctest
./build.ps1 run                    # launch the built app
./build.ps1 snapshots              # render every menu and dialog to build/ui-snapshots
./build.ps1 package                # release build, then the portable zip (and the installer, with Inno Setup)
./build.ps1 configure              # configure only
./build.ps1 clean                  # remove this config's build folder
```

`make` on Windows still needs a VS developer shell (it can't load the VS env);
prefer `build.ps1` there.

### macOS

- **Setup:** Xcode's command-line tools (`xcode-select --install`) and
  `brew install cmake ninja just pkg-config`, then `just build`, `just test`
  and `just run`. The handoff doc (`docs/status/2026-09-14-handoff.md`) has the
  whole list, including glab and the Claude Code plugins.
- **What differs on a Mac:**
  - The crash guard is off (M5 KD1), so a plugin that crashes takes the app
    down.
  - The app has a real menu bar (`MainWindow` is the MenuBarModel): File,
    Edit, Settings, Plugins and Window, with About in the app menu.
  - Audio Units, AUv3 included, scan inside the helper's message loop
    (`engine/ScanInMessageLoop.h`).
  - Fonts load through CoreText. See "Cross-platform gotchas".
- **UI snapshots:** after a build, run
  `build/macos-dev/src/pb3-ui-snapshots_artefacts/Debug/pb3-ui-snapshots build/ui-snapshots`.
- **The macOS package:** `just release`, then `tools/package.sh build/macos-universal`.

### Windows: ASIO (optional low-latency drivers, SDK-gated)

ASIO is auto-detected and gated on the Steinberg ASIO SDK. The SDK is dual-licensed
(the proprietary Steinberg ASIO License, or GPLv3) and **kept out of git** by policy, so you
provide it — `app-v3/CMakeLists.txt` finds the directory containing
`iasiodrv.h` anywhere under `app-v3/asio_sdk/` and then defines `JUCE_ASIO=1`.
Without the SDK, Windows builds use WASAPI/DirectSound only (no error).

- **Local dev:** download the ASIO SDK from Steinberg and extract it under
  `app-v3/asio_sdk/` (any internal layout), then reconfigure (`./build.ps1
  build`). ASIO then appears in Audio Settings.
- **CI:** the `build:windows` / `release:windows` jobs fetch the SDK from the
  project's GitLab generic package registry before configure (best-effort — a
  missing package leaves ASIO off and the pipeline green). A maintainer uploads
  it once (pin `<ver>` to the actual SDK version, matching the CI URL):

  ```bash
  curl --header "PRIVATE-TOKEN: <token>" \
    --upload-file ASIO-SDK_2.3.4_2025-10-15.zip \
    "https://gitlab.com/api/v4/projects/${CI_PROJECT_ID}/packages/generic/asio-sdk/2.3.4/ASIO-SDK_2.3.4_2025-10-15.zip"
  ```

  The `build:windows` job's fetch URL must match this package path/version. Verify
  it took: the next `build:windows` log prints
  `Pedalboard 3: ASIO SDK found … — enabling JUCE_ASIO=1`.

## Working agreement

The maintainer's standing rules. They apply on every machine. Agent memory
doesn't move between machines, so the rules live here.

- **A human merges.** Agents never merge an MR, push `main`, force-push,
  tag, release or sign.
- **The issue comes first.** File or find the issue before opening an MR, and
  put `Closes #N` in the MR's first description, because MRs are often
  merged within minutes.
- **GitLab changes other than the branch push and the MR need the
  maintainer's OK.** That covers new issues, notes on issues, milestones,
  labels and starting pipelines.
- **Ship through `ship-mr`** (see `CLAUDE.md`): push the feature branch, open
  or update the MR, then watch its pipeline.
- **Report every CI job.** A pipeline says "success" even when an
  `allow_failure` job (Windows, macOS) failed. List every job, retried ones
  included, before saying CI passed. Manual release jobs on main are
  expected, not errors.
- **Releases are made by hand, from main only.** Merge requests have no
  release jobs and there are no tag pipelines. Starting a main pipeline or
  clicking a release job needs the maintainer's say-so
  (`docs/releases/README.md`).
- **A merge to main starts no pipeline.** Judge a merged change by its MR's
  pipeline; don't wait for one on main.
- **Don't install software on the maintainer's machine without asking.**
  Installing in CI is fine.
- **Tests never touch real user data.** They use
  `tests/support/TempSettings.h`, and `RealSettingsGuard` fails a run that
  changes the real settings. If it fires, find the test that opened the real
  file; don't loosen the guard.
- **Stacked MRs:** one merged before its parent lands in the parent's
  branch, not main. Say so when handing over a stack. Merge main into a
  branch to update it; never rebase or force-push a pushed branch.
- **Don't click system dialogs the app raises** (a firewall prompt, for
  example). Report them to the maintainer.
- **On Windows, check that the app isn't running before a release rebuild**
  (`Get-Process 'Pedalboard 3'`). If it is, the link fails with `LNK1168`.
  Never kill the maintainer's copy.

## What an agent may run without confirmation

- `cmake --preset linux-*`, `cmake --build`, `ctest`
- `just build`, `just test`, `just format`
- `./build.ps1 build`, `./build.ps1 test`, `./build.ps1 snapshots`, `./build.ps1 package` (Windows)
- `git status`, `git diff`, `git log`
- `grep`, `rg`, `glob`
- Edits anywhere under `app-v3/` and `docs/`

## What requires a human

- Touching `source code/` (read-only reference; never edit)
- `git push` of `main`, `git tag`, merging any MR, any `glab` / `gh` command
  that creates remote state other than the feature-branch push + MR that
  the `ship-mr` skill performs (see "Shipping and tracking" in `CLAUDE.md`)
- Any signing, notarization, or release pipeline invocation
- Installing or modifying runner credentials, certs, or keys
- Modifying `.gitlab-ci.yml` jobs that carry signing variables (none yet; signing is #40)

## Agent-native verification matrix

What CI and an agent can check on each kind of machine, and what needs a
person at a Windows PC:

| Verifiable on Linux dev (CI + agent) | Verifiable on macOS (local dev Mac) | Requires Windows QA |
|---|---|---|
| CMake configure + build | Everything in Linux col + AU/VST3 plugin scan, audible CoreAudio test tone | WASAPI / ASIO round-trip latency |
| Unit tests (Catch2) | Same ctest suite | Commercial VST compatibility |
| Plugin scans through pb3-scanner-helper (VST3 / LV2 installed locally) | The same, plus AU | Real MIDI controller input from physical hardware |
| RealtimeSanitizer build (`linux-rtsan` preset) | — | Driver-specific audio bugs |
| Code style (clang-format) | — | The installer on a clean PC; code signing (#40) |
| `just build` + `just test` end-to-end | `just build` + `just test` + `just run` end-to-end | — |

**macOS is a first-class dev-QA target.** CoreAudio, AudioUnit, and VST3 all
auto-register via `juce::AudioPluginFormatManager::addDefaultFormats()`. A
local Mac can exercise the full Pedalboard 3 stack short of Windows-specific
audio drivers and the Windows crash guard (structured exception handling,
`app-v3/src/platform/CrashGuard_windows.cpp`).

## Conventions

- **Headers:** `.h` (not `.hpp`). Pragma-once, not include guards.
- **Namespace:** `pedalboard3::` at top level; subnamespaces match folder names
  (`pedalboard3::engine::`, `pedalboard3::session::`, etc.).
- **JUCE:** use `juce::` qualifier explicitly in headers; `using namespace juce;`
  permissible inside `.cpp` implementation files only.
- **Real-time code:** anything callable from `processBlock` must not
  allocate, lock or do I/O (the rules at the top of
  `app-v3/src/engine/CMakeLists.txt`). The `linux-rtsan` preset builds with
  Clang's RealtimeSanitizer (`PB3_ENABLE_RTSAN`).
- **Commits:** conventional-commits format (`feat(engine):`, `fix(ui):`,
  `chore(ci):`, `docs(plan):`). Keep the first line under 72 chars.
- **Tests:** mirror `src/` structure under `tests/unit/` with `_test.cpp` suffix.

## Cross-platform gotchas

Code that builds on one platform can fail on another. Each of these cost a CI
round:

- **64-bit integer spellings.** `juce::int64` is `long long`, but
  `std::int64_t` is `long` on Linux. So `juce::jmin(reader->lengthInSamples,
  someInt64)` fails there. Spell out the type: `juce::jmin<std::int64_t>(...)`.
  The same goes for `jmax`, `jlimit`, `std::min` and `std::max`.
- **libc names.** A free function called `signal`, `time`, `index` or
  `select` collides with libc on Linux and macOS. Use distinct names.
- **clang-format versions.** CI's `lint:format` uses the Linux image's
  clang-format 18, and other versions format a few constructs differently.
  For example, declare `struct sigaction` through
  `using SigAction = struct sigaction;` rather than `struct sigaction name{};`.
  CI is the judge.
- **GCC folds `1 / x`.** To provoke SIGFPE in a test, make both operands
  `volatile`.
- **Typeface names.** On Windows, a memory-loaded font reports its WWS
  family: "Barlow" with style "Semi Condensed Bold". CoreText and FreeType
  keep "Barlow Semi Condensed". Match on `getName() + " " + getStyle()`.
- **Windows builds go through `./build.ps1`,** which loads the Visual Studio
  environment. A bare `cmake --build` fails, and ctest then runs an old
  binary.
- **MSVC `C2471: cannot update program database`** during a big rebuild
  means that target's debug database is stuck. Delete the target's
  `CMakeFiles/<target>.dir` folder under the build folder and build again.

## Repo layout (current)

```
.
├── AGENTS.md                # this file
├── CLAUDE.md                # points here, plus the shipping rules (ship-mr)
├── README.md
├── .clang-format
├── .gitignore
├── .gitlab-ci.yml
├── docs/
│   ├── plans/               # /ce-plan output
│   ├── releases/            # release notes, and how to cut a release
│   ├── reviews/
│   └── status/              # dated handoff and status notes
├── source code/             # READ-ONLY: original Pedalboard2 v2
├── legacy/                  # Pedalboard2 binaries, end-user docs, readme, to-do
├── vercel-ui-demo/          # UI mockup (design source)
├── app-v3/                  # the v3 code
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── cmake/               # CPM.cmake (dependency fetching)
│   ├── resources/fonts/     # embedded UI fonts (SIL OFL)
│   ├── src/
│   │   ├── main.cpp
│   │   ├── App.h / App.cpp
│   │   ├── engine/          # graph runtime, plugin host and scanner, transport
│   │   ├── engine/processors/  # built-in processors
│   │   ├── session/         # patches, banks, Session, .pdl files, Preferences
│   │   ├── session/v2_import/  # Pedalboard2 .pdl import
│   │   ├── control/         # MIDI / OSC mappings and input devices
│   │   ├── ui/
│   │   ├── platform/        # the crash guard, per OS
│   │   └── tools/           # pb3-scanner-helper, pb3-ui-snapshots
│   └── tests/
│       ├── unit/            # mirrors src/
│       └── support/         # shared test helpers
├── installer/windows/       # the Inno Setup script for the Windows installer
├── tools/                   # packaging scripts (package.ps1, package.sh)
├── build.ps1                # Windows build / test / run / snapshots / package
└── justfile, Makefile       # build wrappers (the Linux CI image recipe lives in ci-cd)
```

## Current state: road to v1.0.0

The milestones in `docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md`
up to M6 have merged: stability (M1), live MIDI/OSC control (M2), multi-patch
banks (M3), the mappings UI (M4), hardening (M5) and the parity tail (M6). So
have the finish pass, the UI refresh and the UI chrome pass. Next is the first
public beta, 1.0.0-beta.1 (M7), then the signed v1.0.0 release (M8). The
GitLab milestones and issues track what's open.

From 2026-09-14 development continues on a MacBook.
`docs/status/2026-09-14-handoff.md` has the state, the next steps in order
and the open decisions. The GitLab board is the source of truth for open
work: milestones M7 (public beta), M8 (v1.0.0) and 1.x.

### Linux dev server toolchain

The Linux dev server does not ship `cmake`, `ninja`, or `clang-format` on
PATH by default. Install them into the user site with pip and add
`~/.local/bin` to PATH:

```bash
python3 -m pip install --user --break-system-packages cmake ninja clang-format==18.1.8
export PATH="$HOME/.local/bin:$PATH"
```

Then the `cmake --preset linux-dev` / `cmake --build build/linux-dev` /
`ctest --test-dir build/linux-dev` loop works. If `just` isn't installed,
use the `make` wrappers or the raw `cmake` commands above.

## Memory

Agent memory is kept per machine (`~/.claude/projects/.../memory/`) and
doesn't travel with the repo. Anything a future session on another machine
needs belongs in the repo:
- the rules: this file and `CLAUDE.md`;
- design decisions: the plans in `docs/plans/`;
- where things stand: `docs/status/`;
- open work: the GitLab board.
