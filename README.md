# Pedalboard 3

A plugin host for playing live. You build a chain of VST3s, save it as a
patch, put the patches in a bank, and switch between them with a footswitch
while you're on stage.

It's a ground-up rebuild of Niall Moody's
[Pedalboard2](https://www.niallmoody.com/work/pedalboard2/) (2013), which I
still use and which still works, but which predates VST3 and doesn't get
built for current machines any more.

![The main window](docs/screenshots/window-board.png)

Left is the bank — your patches, each on a program change number. Middle is
the board. Right is the plugin browser. The strip along the bottom is where
MIDI and OSC mappings live, and the bar at the top has the transport, the
input and output meters, and the latency, CPU and memory readouts.

## What it does

- **Banks of patches.** A bank is a set of patches on PC numbers. Send
  Program Change 3 and you're on patch 3. That's the whole point of the
  program.
- **MIDI and OSC mappings.** Point a CC, note, program change or pitch bend
  at any plugin parameter, or hit Learn and move the knob. OSC too, if you
  drive the rig from a tablet.
- **Built-in nodes.** Level, VU meter, output toggle, file player, recorder,
  metronome, looper. The things you need around the plugins and don't want to
  go find a plugin for.
- **A transport.** Tempo, time signature and a play head the plugins can see,
  so tempo-synced delays line up.
- **Audio in and out metering**, separately, so when something goes quiet you
  can tell at a glance which side of the board it went quiet on.

Plugin formats are VST3 everywhere, AU on macOS, LV2 on macOS and Linux. CLAP
and VST2 are build flags (`PB3_ENABLE_CLAP`, `PB3_ENABLE_VST2`) and off by
default; VST2 needs your own copy of the SDK.

## It gets out of the way on a small screen

Laptop on a music stand is the normal case, so the top bar gives up its
whitespace first and only then starts folding things away. The panels
collapse to a strip.

![The same board in a narrow window](docs/screenshots/window-narrow.png)

## Setting up audio

![Audio devices](docs/screenshots/dialog-audio-devices.png)

WASAPI and DirectSound on Windows (ASIO too, if you build with the SDK —
see below), CoreAudio on macOS, ALSA and JACK on Linux. The test tone plays
a 440 Hz sine straight to the output so you can check the cable before you
start blaming the board.

If you unplug an interface and plug it back in, it comes back. The output
does not silently fall back to laptop speakers, which is the behaviour you
want when you're the one holding the guitar.

## Turning off what you don't use

![The settings menu](docs/screenshots/menu-settings.png)

A board with no MIDI mappings doesn't need a mappings strip in the way. Each
feature is switched on app-wide in Settings and then per board, so opening
someone else's bank asks before it turns anything on. Tempo, the metronome
and the looper start off; MIDI starts on, OSC off.

Cables are separate from that: a plugin only gets a MIDI cable drawn to it if
it actually has MIDI pins, so a board of plain audio effects shows two cables
per connection instead of three.

## Status

Milestones M1 to M6 are merged: stability, live MIDI/OSC control, banks with
Program Change switching, the mappings UI, the crash guard and `.pdl` safety,
and the parity tail (transport, file player, recorder, looper, metronome).
The finish pass, the UI refresh and the chrome pass are in too.

Next up is the first public beta, 1.0.0-beta.1. The version, the release
pipeline, the Windows installer and the portable zip all exist. What's left
is hands-on use and a handful of decisions.

- Where things stand:
  [`docs/status/2026-09-14-handoff.md`](docs/status/2026-09-14-handoff.md)
- Open work: milestones M7 (public beta), M8 (v1.0.0) and 1.x on GitLab
- Releases are cut by hand off main — [`docs/releases/README.md`](docs/releases/README.md)

The roadmap is
[`docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md`](docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md)
and the original rewrite plan is
[`docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md`](docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md).

## Building it

JUCE 8, CMake, C++20, Catch2. Windows is the primary target; macOS and Linux
build and are covered by CI.

### Windows

Any PowerShell will do — the script finds the Visual Studio developer
environment itself. You need Visual Studio with the C++ workload.

```powershell
./build.ps1 build                  # configure + build, Debug
./build.ps1 build -Config release  # RelWithDebInfo
./build.ps1 test                   # build, then run the unit tests
./build.ps1 run                    # launch it
./build.ps1 snapshots              # render every window, menu and dialog to build/ui-snapshots
./build.ps1 package                # release build + portable zip (+ installer, with Inno Setup)
./build.ps1 clean
```

The screenshots in this file came out of `./build.ps1 snapshots`, which is
also how the UI gets reviewed without clicking through it.

ASIO is optional; see "Windows: ASIO" in [AGENTS.md](AGENTS.md).

### macOS and Linux

`just` picks the right preset for the host.

```bash
just build
just test
just run
just release
```

The `Makefile` has the same targets for machines without `just`. Or drive
CMake yourself:

```bash
cmake --preset linux-dev -S app-v3
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

macOS needs Xcode's command-line tools (`xcode-select --install`) and
`brew install cmake ninja just pkg-config`.

Linux needs CMake 3.25+, Ninja, a C++20 compiler (CI uses clang-18) and
JUCE 8's X11 and audio libraries:

```
build-essential pkg-config clang-18
libasound2-dev libjack-jackd2-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
libxext-dev libxcomposite-dev libxrender-dev
libfreetype-dev libfontconfig1-dev
libgl-dev libglu1-mesa-dev mesa-common-dev
```

It builds with `JUCE_USE_CURL=0` and `JUCE_WEB_BROWSER=0`, so you don't need
libcurl or WebKit even though the CI image carries them.

## Where things are

```
app-v3/               the v3 source tree
  src/                engine/, session/, control/, ui/, platform/, tools/
  tests/              Catch2 — unit/ mirrors src/, support/ holds the helpers
  resources/          embedded fonts (SIL OFL)
docs/
  plans/              product plans
  design/             design notes (the icon system, etc.)
  releases/           release notes, and how to cut one
  status/             dated handoff notes
  screenshots/        the images in this file
installer/windows/    Inno Setup script
tools/                packaging scripts
legacy/               Pedalboard2's end-user docs, kept for reference
source code/          the original Pedalboard2 source — READ-ONLY, never built
vercel-ui-demo/       the UI mockup the design follows
AGENTS.md             conventions, including for coding agents
```

## Licence

**AGPLv3** — see [LICENSE](LICENSE). Copyright (c) 2026 OpenZephyr LLC.

Use it, study it, change it, share it; anything built on it stays under the
same licence. The "Affero" part adds one thing on top of the GPL: run a
modified version as a network service and its users are entitled to the
source too.

That wasn't a free choice. JUCE 8's open-source track is AGPLv3, the VST3 SDK
JUCE bundles is Steinberg-proprietary or GPLv3, and Pedalboard2 was
GPLv3-or-later. AGPLv3 is the one licence all three fit under.

Everything else — JUCE, the VST3 and LV2 SDKs, the fonts, the build and test
tooling, the optional ASIO SDK — is listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), including which option is
taken where a dependency offers a choice.

The original Pedalboard2 in `source code/` keeps its own GPLv3-or-later
licence (Niall Moody, 2009–2013). Nothing in it is compiled and nothing in it
ships.

### Contributing

Contributions keep your copyright and come in under the AGPLv3 with a
[Developer Certificate of Origin](CONTRIBUTING.md) sign-off — `git commit -s`,
not a CLA. Nobody gets the right to relicense your code.

Any paid features later on get built as separate components next to this
application rather than as code inside it, so what's in this repository stays
AGPLv3 and stays complete.
