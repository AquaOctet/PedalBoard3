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
- **Plugin formats:** VST3 (MIT SDK), AU (macOS), CLAP (via vendored
  `juce_clap_hosting` fork), LV2 (Linux), VST2 (opt-in build flag)
- **Tests:** Catch2 v3 (unit), pluginval (integration), golden-WAV regression
- **CI:** GitLab CI. Linux on shared runners inside the silkOps factory
  image (`ci-cd/juce-linux`); Windows/macOS on GitLab SaaS runners with
  `allow_failure` until Phase 6.

## Build

Host-OS-aware via `just` — the default recipes pick `linux-dev` on Linux and
`macos-dev` on macOS:

```bash
just build       # configure + build host dev preset
just test        # ctest against the host dev build
just run         # launch the hello-world app (execs binary on Linux, `open`s .app on macOS)
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
open "build/macos-dev/src/Pedalboard 3_artefacts/Debug/Pedalboard 3.app"

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
```

`make` on Windows still needs a VS developer shell (it can't load the VS env);
prefer `build.ps1` there.

### Windows: ASIO (optional low-latency drivers, SDK-gated)

ASIO is auto-detected and gated on the Steinberg ASIO SDK. The SDK is GPLv3
(Steinberg's Oct 2025 relicence) but **kept out of git** by policy, so you
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

## What an agent may run without confirmation

- `cmake --preset linux-*`, `cmake --build`, `ctest`
- `just build`, `just test`, `just format`
- `git status`, `git diff`, `git log`
- `grep`, `rg`, `glob`
- Edits anywhere under `app-v3/`, `docs/`, `tests/`, `ci/`, `scripts/`

## What requires a human

- Touching `source code/` (read-only reference; never edit)
- `git push` of `main`, `git tag`, merging any MR, any `glab` / `gh` command
  that creates remote state other than the feature-branch push + MR that
  the `ship-mr` skill performs (see "Shipping and tracking" in `CLAUDE.md`)
- Any signing, notarization, or release pipeline invocation
- Installing or modifying runner credentials, certs, or keys
- Modifying `.gitlab-ci.yml` jobs that carry signing variables (none yet — Phase 6)

## Agent-native verification matrix

What an agent on this Linux dev server can verify, plus what a human can do
on a local Mac, vs. what still needs the Windows QA desktop:

| Verifiable on Linux dev (CI + agent) | Verifiable on macOS (local dev Mac) | Requires Windows QA |
|---|---|---|
| CMake configure + build | Everything in Linux col + AU/VST3 plugin scan, audible CoreAudio test tone | WASAPI / ASIO round-trip latency |
| Unit tests (Catch2) | Same ctest suite | Commercial VST compatibility |
| Offline render regression (golden WAV diff) | Same, plus live-device render | Real MIDI controller input from physical hardware |
| pluginval on Linux-native plugins (Surge XT / Dexed / Dragonfly) | pluginval on macOS-native AU + VST3 | Driver-specific audio bugs |
| Code style (clang-format) | — | Code signing / MSI installer smoke test |
| Architecture linting (grep-based RT-safety) | — | WebView2 editor embedding |
| `just build` + `just test` end-to-end | `just build` + `just test` + `just run` end-to-end | — |

**macOS is a first-class dev-QA target.** CoreAudio, AudioUnit, and VST3 all
auto-register via `juce::AudioPluginFormatManager::addDefaultFormats()`. A
local Mac can exercise the full Pedalboard 3 stack short of Windows-specific
audio drivers and the Phase 1.B.2 SEH code path.

## Conventions

- **Headers:** `.h` (not `.hpp`). Pragma-once, not include guards.
- **Namespace:** `pedalboard3::` at top level; subnamespaces match folder names
  (`pedalboard3::engine::`, `pedalboard3::session::`, etc.).
- **JUCE:** use `juce::` qualifier explicitly in headers; `using namespace juce;`
  permissible inside `.cpp` implementation files only.
- **Real-time code:** any function callable from `processBlock` must be
  marked with the comment `// RT: safe (no alloc, no lock, no I/O)`.
  Debug builds enforce this via RealtimeSanitizer (Phase 1).
- **Commits:** conventional-commits format (`feat(engine):`, `fix(ui):`,
  `chore(ci):`, `docs(plan):`). Keep the first line under 72 chars.
- **Tests:** mirror `src/` structure under `tests/unit/` with `_test.cpp` suffix.

## Repo layout (current)

```
.
├── AGENTS.md                # this file
├── CLAUDE.md                # alias → AGENTS.md (legacy)
├── README.md
├── .clang-format
├── .gitignore
├── docs/
│   └── plans/               # /ce-plan output
├── source code/             # READ-ONLY: original Pedalboard2 v2
├── documentation/           # READ-ONLY: original v2 end-user docs
├── app-v3/                  # NEW v3 code
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── src/
│   │   ├── main.cpp
│   │   ├── App.h / App.cpp
│   │   ├── engine/          # [Phase 1] AudioGraphRuntime, etc.
│   │   ├── engine/processors/  # [Phase 1 + 5] internal processors
│   │   ├── session/         # [Phase 2]
│   │   ├── session/v2_import/  # [Phase 2]
│   │   ├── control/         # [Phase 4]
│   │   ├── diagnostics/     # [Phase 1]
│   │   ├── ui/              # [Phase 3]
│   │   └── platform/
│   ├── external/            # CPM cache / git submodules
│   └── tests/
├── ci/                      # CI helper scripts (the Linux image recipe lives in ci-cd)
├── scripts/                 # provisioning + dev-loop helpers
└── justfile
```

## Current phase: road to v1.0.0

Phases 0–5 of the master rewrite plan are substantially landed (audio
engine, session/persistence, interactive canvas, control-layer logic,
first internal processors). The active track is the milestone sequence in
`docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md`: **M1
stability** (the crash-hunt backlog, GitLab issues #6–8) is in progress,
followed by wiring the MIDI/OSC control layer, multi-patch banks, mappings
UI, a public beta, the parity tail, hardening, and the Phase 6 signed
release. Windows / macOS / signing remain deferred to Phase 6.

### Dev toolchain note

This Linux dev server does not ship `cmake`, `ninja`, or `clang-format` on
PATH by default. Install them into the user site with pip and add
`~/.local/bin` to PATH:

```bash
python3 -m pip install --user --break-system-packages cmake ninja clang-format==18.1.8
export PATH="$HOME/.local/bin:$PATH"
```

Then the `cmake --preset linux-dev` / `cmake --build build/linux-dev` /
`ctest --test-dir build/linux-dev` loop works. `just` is not installed;
use the `make` wrappers or the raw `cmake` commands above.

## Memory

Structured memory lives outside the repo (`~/.claude/projects/.../memory/`).
Session-specific decisions (scope, CLAP policy, infra timing) are already
captured there as of 2026-04-12.
