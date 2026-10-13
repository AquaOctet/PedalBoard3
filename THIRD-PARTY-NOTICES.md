# Third-party notices

Pedalboard 3 itself is licensed under the GNU Affero General Public License v3.0;
the full text is in [LICENSE](LICENSE). This file records everything else the app
is built from, which licence each piece carries, and where that licence lives.

Versions are the ones pinned in `app-v3/CMakeLists.txt`. Where a dependency offers
a choice of licences, the one Pedalboard 3 takes is stated.

## Compiled into the released app

### JUCE 8.0.4 — AGPLv3 **or** commercial

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

### VST3 SDK (bundled in JUCE) — Steinberg VST3 License **or** GPLv3

Pedalboard 3 takes the **GPLv3** option, which an AGPLv3 work may do: GPLv3 §13
expressly permits the combination.

**This is not the MIT-licensed VST3 SDK.** Steinberg relicensed the SDK under MIT
with version 3.8, but JUCE 8.0.4 bundles the 2024 SDK, whose `LICENSE.txt` reads:

> "This Software Development Kit is licensed under the terms of the Steinberg VST3
> License, or alternatively under the terms of the General Public License (GPL)
> Version 3."

So VST3 hosting is one of the things that keeps the shipped app copyleft. A closed
build would need either a newer, MIT-licensed SDK or a signed agreement with
Steinberg — see "If a build is ever closed" below.

### LV2 SDK (bundled in JUCE) — ISC

Linux and macOS only (`JUCE_PLUGINHOST_LV2=1` is set for those platforms).
Copyright 2007-2012 Steve Harris, David Robillard, and others.

### Fonts — SIL Open Font License 1.1

Barlow Condensed and Barlow Semi Condensed (Jeremy Tribby) and JetBrains Mono
(JetBrains) are embedded in the binary. Each family's `OFL.txt` lives beside its
`.ttf` files in `app-v3/resources/fonts/`, and the packaging scripts copy all
three into `licences/` inside every release archive.

## Build and test only — not in the released app

| Component | Version | Licence | Why it isn't shipped |
| --- | --- | --- | --- |
| Catch2 | 3.7.1 | Boost Software License 1.0 | Links only into the test binary |
| CPM.cmake | 0.40.2 | MIT | A CMake script; runs at configure time |

## Off by default — not in any official build

### ASIO SDK — Steinberg ASIO License **or** GPLv3

Low-latency Windows audio. The SDK is **not in this repository**: `app-v3/asio_sdk/`
is gitignored, so no CI build and no published release has ever contained ASIO
support. A developer who drops the SDK there gets `JUCE_ASIO=1` in their own local
build and is bound by Steinberg's terms. Its `LICENSE.txt` ((c) 2025 Steinberg Media
Technologies GmbH) offers the same proprietary-or-GPLv3 choice as VST3; the
proprietary option requires a License Agreement signed by Steinberg. Some individual
helper files in the SDK carry their own BSD 3-Clause header instead.

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
- **VST3** — a VST3 SDK 3.8 or newer under MIT, or a signed Steinberg agreement.
  The SDK that JUCE 8.0.4 bundles cannot go into a closed binary.
- **ASIO** — a License Agreement signed by Steinberg, for the same reason.

This is why the paid work is planned as a separate process rather than as code
inside the AGPLv3 application.
