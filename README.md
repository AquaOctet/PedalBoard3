# Pedalboard 3

A modular, live-performance VST plugin host — a ground-up rebuild of
Niall Moody's [Pedalboard2](https://www.niallmoody.com/work/pedalboard2/)
(JUCE/C++, GPLv3, 2013) with modern tooling and plugin formats.

- **Stack:** JUCE 8, CMake, C++20, Catch2, GitLab CI
- **Plugin formats:** VST3, AU (macOS), CLAP, LV2 (Linux), VST2 (opt-in)
- **Targets:** Windows (primary), macOS, Linux

## Status

**Phase 0 — infrastructure skeleton.** A buildable Linux hello-world JUCE 8
app + a Linux CI pipeline. Windows / macOS / signing / auto-update are all
on the Phase 6 roadmap.

The full plan lives at
[`docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md`](docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md).

## Build (Linux)

```bash
just build          # configure + build the linux-dev preset
just test           # run Catch2 unit tests via ctest
just run            # launch the hello-world window
```

Or manually:

```bash
cmake --preset linux-dev -S app-v3
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

System dependencies for a JUCE 8 Linux build (Ubuntu 22.04+):

```
build-essential cmake ninja-build pkg-config clang-18
libasound2-dev libjack-jackd2-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
libxext-dev libxcomposite-dev libxrender-dev
libfreetype-dev libfontconfig1-dev
libgl-dev libglu1-mesa-dev
libcurl4-openssl-dev libwebkit2gtk-4.1-dev
```

## Repo layout

```
.
├── AGENTS.md                # conventions for coding agents
├── app-v3/                  # the v3 source tree
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── resources/           # embedded fonts (SIL OFL)
│   ├── src/
│   └── tests/
├── docs/plans/              # product plans (ce-plan output)
├── legacy/                  # Pedalboard2 binaries, docs, readme, to-do
├── source code/             # READ-ONLY original Pedalboard2 v2 code
├── vercel-ui-demo/          # UI mockup (design source)
├── build.ps1                # Windows build / test / run / snapshots
├── justfile, Makefile       # build wrappers (Linux / macOS)
└── CMakePresets.json
```

## Licence

- Code: **AGPLv3** (matches JUCE 8's open-source licence track; Pedalboard2
  upstream was GPLv3).
- Original Pedalboard2 source (`source code/`) retained for reference under
  its own GPLv3 licence (Niall Moody, 2013).
- VST3 SDK: MIT (Steinberg, Oct 2025 relicence).
- ASIO SDK: GPLv3 (Steinberg, Oct 2025 dual-licence).
