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
- **CI:** GitLab CI. Linux on shared runners; Windows/macOS are self-hosted
  and **not yet provisioned** — CI jobs for those platforms `allow_failure`
  until Phase 6.

## Build

```bash
# Linux dev build (debug, sanitizers off)
cmake --preset linux-dev
cmake --build --preset linux-dev

# Linux release
cmake --preset linux-release
cmake --build --preset linux-release

# Tests
ctest --preset linux-dev --output-on-failure
```

Shortcuts via `just`:

```bash
just build       # cmake --build --preset linux-dev
just test        # ctest on the dev preset
just format      # clang-format -i on app-v3/src
just run         # launch the built hello-world app
```

## What an agent may run without confirmation

- `cmake --preset linux-*`, `cmake --build`, `ctest`
- `just build`, `just test`, `just format`
- `git status`, `git diff`, `git log`
- `grep`, `rg`, `glob`
- Edits anywhere under `app-v3/`, `docs/`, `tests/`, `ci/`, `scripts/`

## What requires a human

- Touching `source code/` (read-only reference; never edit)
- `git push`, `git tag`, any `glab` / `gh` command that creates remote state
- Any signing, notarization, or release pipeline invocation
- Installing or modifying runner credentials, certs, or keys
- Modifying `.gitlab-ci.yml` jobs that carry signing variables (none yet — Phase 6)

## Agent-native verification matrix

What an agent on this Linux dev server can verify vs. what requires the
Windows QA desktop (not yet provisioned):

| Verifiable on Linux dev | Requires Windows QA |
|---|---|
| CMake configure + build (Linux) | WASAPI / ASIO round-trip latency |
| Unit tests (Catch2) | Commercial VST compatibility |
| Offline render regression (golden WAV diff) | Real MIDI controller input |
| pluginval against Surge XT / Dexed / Dragonfly on Linux | Driver-specific audio bugs |
| Code style (clang-format) | Code signing / installer smoke test |
| Architecture linting (`grep`-based RT-safety checks) | WebView2 editor embedding |

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
├── ci/                      # Dockerfiles, scripts — Phase 6
├── scripts/                 # provisioning + dev-loop helpers
└── justfile
```

## Current phase: Phase 0 — Infrastructure skeleton

Focus is a buildable Linux hello-world JUCE 8 app plus a CI skeleton that
runs `cmake` + `ctest` on every push. Windows / macOS / signing are all
deferred to Phase 6.

## Memory

Structured memory lives outside the repo (`~/.claude/projects/.../memory/`).
Session-specific decisions (scope, CLAP policy, infra timing) are already
captured there as of 2026-04-12.
