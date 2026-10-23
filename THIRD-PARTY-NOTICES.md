# Third-party notices

Pedalboard 3 itself is licensed under the GNU Affero General Public License v3.0;
the full text is in [LICENSE](LICENSE). This file records everything else the app
is built from, which licence each piece carries, and where that licence lives.

Versions are the ones pinned in `app-v3/CMakeLists.txt`. Where a dependency offers
a choice of licences, the one Pedalboard 3 takes is stated.

## Compiled into the released app

### JUCE 8.0.16 — AGPLv3 **or** commercial

> "The JUCE Framework modules are dual-licensed under the AGPLv3 and the
> commercial JUCE licence." — JUCE's `LICENSE.md`

Pedalboard 3 takes the **AGPLv3** option, which is why the app is AGPLv3 and not
something more permissive. <https://github.com/juce-framework/JUCE>

JUCE bundles its own dependencies, and the ones for the modules and platforms we
build end up inside the binary. JUCE's `LICENSE.md` is the authoritative list;
the ones that reach a Pedalboard 3 build are zlib, pnglib and jpeglib (zlib /
Independent JPEG Group), FLAC and Ogg Vorbis (BSD), HarfBuzz (Old MIT),
SheenBidi (Apache 2.0), CHOC including QuickJS (ISC / MIT), pslextensions
(public domain), and the two plugin-format SDKs below.

### VST3 SDK (bundled in JUCE) — MIT

JUCE 8.0.16 bundles **VST3 SDK 3.8.0 under the MIT License** ((c) 2025 Steinberg
Media Technologies GmbH), and JUCE's own `LICENSE.md` lists it that way. The
same MIT text covers the SDK root and each of its parts (`base/`,
`pluginterfaces/`, `public.sdk/`).

This changed with the bump. Up to JUCE 8.0.4 the bundled SDK was the 2024 one,
offered as the proprietary Steinberg VST3 License **or** GPLv3, and Pedalboard 3
took the GPLv3 option — so VST3 hosting was one of the things holding the
shipped app copyleft. Under MIT it no longer is: VST3 imposes nothing beyond
attribution. The app stays AGPLv3 because **JUCE** is taken under AGPLv3.

### LV2 SDK (bundled in JUCE) — ISC

Linux and macOS only (`JUCE_PLUGINHOST_LV2=1` is set for those platforms).
Copyright 2007-2012 Steve Harris, David Robillard, and others.

### Fonts — SIL Open Font License 1.1

Barlow Condensed and Barlow Semi Condensed (Jeremy Tribby) and JetBrains Mono
(JetBrains) are embedded in the binary. Each family's `OFL.txt` lives beside its
`.ttf` files in `app-v3/resources/fonts/`, and the packaging scripts copy all
three into `licences/` inside every release archive.

### ASIO SDK (bundled in JUCE) — Steinberg ASIO License **or** GPLv3

Low-latency Windows audio, **in every released Windows build**. Pedalboard 3
takes the **GPLv3** option, which is why the Windows binary is as copyleft as
the rest.

Since JUCE 8.0.9 the three ASIO headers ship inside JUCE itself
(`modules/juce_audio_devices/native/asio/`), so `app-v3/CMakeLists.txt` defines
`JUCE_ASIO=1` unconditionally on Windows and nothing has to be vendored or
fetched. Before the 8.0.16 bump the SDK was kept out of this repository and
`build:windows` pulled it from the project's package registry; that gate and
that fetch are gone.

The bundled `LICENSE.txt` ((c) 2025 Steinberg Media Technologies GmbH) offers
the proprietary-or-GPLv3 choice, and the proprietary option requires a License
Agreement signed by Steinberg. Some individual helper files in the full SDK
carry their own BSD 3-Clause header instead.

A developer may still pin their own copy of `iasiodrv.h` under
`app-v3/asio_sdk/` (gitignored), which switches the build to
`JUCE_ASIO_USE_EXTERNAL_SDK=1`; the same terms apply to it.

## Build and test only — not in the released app

| Component | Version | Licence | Why it isn't shipped |
| --- | --- | --- | --- |
| Catch2 | 3.7.1 | Boost Software License 1.0 | Links only into the test binary |
| CPM.cmake | 0.40.2 | MIT | A CMake script; runs at configure time |

## Off by default — not in any official build

### CLAP

`PB3_ENABLE_CLAP` is `OFF` in every preset in `app-v3/CMakePresets.json`, so the
CLAP SDK, clap-helpers and `juce_clap_hosting` are never fetched or compiled. Their
licences need checking before CLAP is switched on.

## Reference only — not compiled, not shipped

### Pedalboard2 (`source code/`) — GPLv3 or later

Copyright (c) 2009 Niall Moody. The original Pedalboard2 tree is kept in this
repository, read-only, as the reference for what version 3 has to match. Nothing in
`app-v3/` includes it and nothing in it is built; it carries its own licence
headers ("either version 3 of the License, or (at your option) any later version").
Pedalboard 3 is an independent reimplementation, and AGPLv3 keeps the result under
compatible terms.

## If a build is ever closed

Pedalboard 3 is AGPLv3 and is meant to stay that way. Anything sold on top has to
be licensed separately, and each of these would have to be settled first:

- **JUCE** — a commercial JUCE licence instead of the AGPLv3 option.
- **VST3** — nothing to settle any more. JUCE 8.0.16 bundles SDK 3.8.0 under
  MIT, which a closed binary may use with attribution. Up to 8.0.4 this was a
  blocker, and the decision on #47 was taken while it was one.
- **ASIO** — still a blocker: a License Agreement signed by Steinberg, because
  the Windows build takes the GPLv3 option. ASIO support cannot simply be
  carried over into a closed build.

So one of the two SDK obstacles is gone and one remains. JUCE's own licence is
the load-bearing reason the application is AGPLv3, and the paid work is still
planned as a separate process rather than as code inside it.
