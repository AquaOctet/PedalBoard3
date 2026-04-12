---
title: Pedalboard 3 — From-scratch modern rewrite of Pedalboard2
type: feat
status: active
date: 2026-04-12
---

# Pedalboard 3 — From-scratch modern rewrite of Pedalboard2

## Enhancement Summary

**Deepened on:** 2026-04-12
**Research agents run:** 10 in parallel (framework-docs: JUCE 8 AudioProcessorGraph / juce_clap_hosting / GitLab CI; best-practices: crash isolation, lock-free audio FIFOs, remote Linux→Windows dev VM; Explore: Pedalboard2 `.pdl` schema deep-read; reviewers: architecture, performance, simplicity, security, data-integrity)

### Key improvements baked into this revision

1. **Patch-switch mechanism fully specified** with a state machine (§4.1.1), a concrete code skeleton for `AudioGraphRuntime::swapPreparedGraph` + worker-thread preload + all-notes-off flush (§Appendix A.1), and a trash-FIFO deferred-delete pattern to keep the RT thread allocation-free (§Appendix A.5).
2. **CLAP hosting risk is real.** `juce_clap_hosting` upstream has had zero commits since Oct 2022; Windows HWND embedding is stubbed; 10 Hz message-thread timer is the only main-thread dispatch. **CLAP is downgraded from a v3.0 must-have to a v3.1 goal.** v3.0 ships VST3 + AU only (see revised §Open Questions and §10 Risks R1).
3. **Crash isolation is in-process, best-effort, with teeth.** Windows SEH in a thin C-style shim around `processBlock`/`prepareToPlay`; POSIX signal-handler is opt-in on Linux, off on macOS; NaN/Inf guard is SSE2/NEON-vectorized at master output bus only (not per-node); post-hoc watchdog (3× block-budget overrun = dead). Rust-style "trash FIFO" retires old plugin instances off the audio thread. See §Appendix A.2.
4. **Lock-free RT messaging is inventoried.** 10 discrete SPSC lanes named with per-lane primitive choice (JUCE `AbstractFifo` for event streams, `std::atomic<T>` for latest-wins meters, raw `atomic<Graph*>` + trash-FIFO for patch-swap, `choc::VariableSizeFIFO` for RT logging, `choc::DirtyList` for RT→UI dirty flags). See §Appendix A.3 with a recommendation table.
5. **RealtimeSanitizer is mandatory in debug builds.** Clang `-fsanitize=realtime` on a `linux-rtsan` CMake preset, with `[[clang::nonblocking]]` annotations on the audio callback. This turns §4.1 invariant #1 from a hope into a guarantee. Added as a new Phase 1 deliverable.
6. **`.pdl` security and durability hardened.** Plugin references resolve by `PluginIdentity` against `KnownPluginList` tuple lookup only — never by file path (AC-SE-5..6). XML parser is XXE-safe with size limits (AC-SE-7..8). Atomic save path explicitly specified: write-temp → `fsync`/`FlushFileBuffers` → `rename`/`MoveFileEx(WRITE_THROUGH)` → dir-fsync (AC-FS-6). Unrestored plugin state blobs are preserved verbatim on re-save (AC-FS-9). Forward-compat policy made explicit: `schemaMajor`/`schemaMinor` split, unknown elements preserved through round-trip OR file opens read-only (AC-FS-7).
7. **OSC default is 127.0.0.1 loopback only.** LAN bind is explicit opt-in with first-use warning modal; optional source-IP allowlist and shared-secret auth (AC-SE-1 revised, AC-SE-9..11). "Loopback + LAN" default from the original plan is a live-performance griefing vector on backstage WiFi.
8. **Auto-update hardened.** Signed appcast (not just signed payloads), no-downgrade enforcement, two-key rotation (current + next baked into each release), offline multi-site key backups (AC-SE-12..17). Alternative: **ship v3.0 without auto-update**, add in v3.0.1 — removes the single-point-of-total-compromise risk from the first release window.
9. **Data/observability module added.** New `diagnostics/` subsystem (§4.2.F) owns the RT-safe event log, structured logger, NaN-guard rate limiter, crash breadcrumbs, and minidump symbolication pipeline. Previously implicit / distributed; now a first-class module.
10. **Remote Linux + Windows dev-VM topology made concrete.** Recommendation: **Proxmox Win11 VM on owned hardware** as primary (~$0/mo) with **Azure B2s_v2 Windows, 8h×20d/mo (~$16/mo)** as fallback. GitHub Codespaces Windows does not exist as of 2026. A 20-line PowerShell bootstrap script (`scripts/provision/bootstrap-winvm.ps1`) provisions Build Tools + CMake + Ninja + Git + WebView2 + VB-CABLE + SSH + sccache. See §Appendix B.
11. **GitLab CI pipeline concretized.** Full `.gitlab-ci.yml` skeleton with parallel build matrix, Azure Trusted Signing PowerShell invocation, macOS keychain-create/notarytool/stapler flow, manual production gate, Generic Package Registry upload, Sparkle/WinSparkle appcast regeneration, and three runbooks (cert rotation, runner outage, updater-key loss). See §Appendix C.
12. **Performance guardrails added.** Per-plugin CPU instrumentation via `std::chrono::steady_clock` into atomic counters (no-alloc); FilePlayer streaming via per-file ring buffer ≥500 ms + 2 s preload (AC-FP-1a/1b); Looper per-instance hard cap (AC-LP-1..2); `mimalloc` as global allocator on Windows/Linux to prevent long-session fragmentation (new R17); 1-hour memory soak test added to Phase 1; startup <500 ms first-paint with background plugin scan (AC-ST-1..3).
13. **"Ruthless v3.0" alternative documented.** An 11-13 week minimum-viable scope is described in §14 as an explicit counter-plan for consideration if calendar pressure forces cuts: VST3+AU only, no CLAP, no undo, no PDC, no auto-update, no per-node CPU meters, 3 modules instead of 5, manual updates for v3.0 release.

### New considerations discovered

- JUCE 8's `AudioProcessorGraph` **does compute PDC automatically at rebuild**, but **does not recompute when a plugin's latency changes at runtime**. Must hook `AudioProcessorListener::audioProcessorChanged(..., latencyChanged=true)` to trigger `graph.rebuild()` on the message thread.
- `KnownPluginList` is **not thread-safe for reads** — wrap in `juce::ReadWriteLock` or snapshot-on-demand.
- VST3 plugin state blobs can exceed 10+ MB (Kontakt sample mappings, convolution IRs); state restore on the audio thread is disqualifying — the worker-thread preload pattern is mandatory, not optional.
- Steinberg's VST3 SDK **went MIT on Oct 20 2025**; ASIO **went GPLv3 on Oct 15 2025**. Removes the last licensing friction for an AGPLv3 Pedalboard 3 build.
- Windows self-hosted `gitlab-runner` **must run with a GUI session** (not LocalSystem, not SSH-only) for audio-plugin tests to work correctly.
- macOS hardened runtime for a plugin host **requires** `com.apple.security.cs.disable-library-validation` + `cs.allow-jit` + `cs.allow-unsigned-executable-memory` + `NSMicrophoneUsageDescription` — each weakens the sandbox but is non-negotiable for hosting third-party plugins (AC-SE-36..38).

> Full research briefs for each section live in **§Appendix A–C** at the end of this document. In-line Research Insights blocks are marked **📎 Research Insights** under the sections they enrich.

## Overview

Pedalboard 3 is a ground-up rewrite of Niall Moody's **Pedalboard2** (JUCE/C++, GPLv3, ~24k LOC, last active ~2013) as a modern live-performance **VST plugin host** targeting Windows (primary), macOS, and Linux. Development happens on a remote Linux server; QA and release validation happen on a Windows desktop with real commercial VSTs. The rebuild preserves v2's defining character — modular drag-to-connect plugin graph, rapidly switchable patches, MIDI/OSC mapping of every parameter — while adding modern plugin format coverage (VST3 / CLAP / AU, with optional VST2), modern code-signing and auto-update pipelines, a GitLab CI release pipeline, and a deliberately-constrained robustness contract appropriate for on-stage use.

The recommended stack after research is **JUCE 8 + CMake (C++23)** with `AudioProcessorGraph` as the engine and `juce_clap_hosting` for CLAP. The AGPLv3-track JUCE license is compatible with Pedalboard2's GPLv3 heritage and the recent (Oct 2025) **Steinberg VST3 → MIT** and **ASIO → GPLv3** relicensing removes the last legal friction that would have argued for a Rust stack. Runner-up Rust+Tauri is rejected for a *host* app because VST3 and AU hosting in Rust remain DIY-FFI work that would dominate the project (see §6 Alternative Approaches).

All new code lives in `app-v3/`. The `source code/` directory is referenced read-only as behavior spec.

## Problem Statement

Pedalboard2 is a beloved live-use tool but is effectively unmaintainable in 2026:

1. **Codebase is frozen at a ~2012-era JUCE** (Projucer, VST2-only, no CLAP, no AU auto-detection on modern macOS, no VST3 hosting).
2. **VST2 is legally and practically dead.** Steinberg discontinued the SDK program years ago; the plugin ecosystem has moved to VST3/CLAP/AU. A host that only loads VST2 is a museum piece.
3. **Known stability bugs persist in the 2013 codebase** — the `ChangeLog.txt` lists multiple patch-switch deadlocks, pure-virtual-at-shutdown crashes during MIDI close, and threading issues around mapping deletion. Any live-use tool that crashes on stage is disqualified.
4. **Cross-platform distribution has moved on.** Code-signing in 2026 requires HSM-backed private keys; macOS requires notarization+stapling; Windows users expect MSI + SmartScreen-clean signing; Linux users expect AppImage with auto-update. None of this existed for v2.
5. **Modern plugin formats are not optional** — users have plugins they want to load: VST3 (now MIT-licensed, universal), AU (macOS ecosystem), CLAP (growing fast since Bitwig/u-he adoption), plus long-tail VST2 for compatibility.
6. **The dev workflow constraint is unusual** — development on a remote Linux server, QA on Windows, with the audio-host cross-compile story being genuinely hard. A deliberate CI and workflow design is needed; it cannot be inferred from tradition.

## Proposed Solution

**A from-scratch JUCE 8 + CMake rewrite** that preserves Pedalboard2's live-performance mental model (patches, drag-to-connect, MIDI/OSC mapping, internal processors) and its feature vocabulary where users will notice absence, while modernizing everything beneath the surface:

- Engine: `AudioProcessorGraph`-based audio graph with an explicit **patch-switch model** (preload-and-swap with atomic graph exchange on buffer boundary, all-notes-off on outgoing MIDI-receiving nodes — see §4.2).
- Formats: **VST3 (first-class)**, **AU (macOS)**, **CLAP (via `juce_clap_hosting`)**, **LV2 (Linux)**, **VST2 (user-compile-only, build-gated, never in official macOS release)**.
- Control: **`juce_osc`** for OSC in/out (retire v2's custom `NiallsOSCLib`); **`MidiInput`/`MidiOutput`** + new MIDI Output node (ship new feature v2 never had).
- UI: Native JUCE component tree, OpenGL canvas for the graph, custom `LookAndFeel_V4` with colour-scheme tokens that import from v2 `.colourscheme` files.
- Robustness: NaN/Inf output guard; plugin crash list with sandboxed scan; in-process plugin hosting in v3.0 with a clear upgrade path to out-of-process sandboxing in v3.1.
- Platform: Linux (dev + optional JACK/PipeWire user support), Windows (primary user platform with ASIO/WASAPI), macOS (CoreAudio + AU).
- CI: GitLab CI with Linux shared runner + self-hosted Windows runner (on the QA box) + self-hosted Mac mini. Native per-platform builds; no cross-compile for release artifacts. Azure Trusted Signing (Windows), Developer ID + notarytool (macOS), GPG AppImage (Linux). Sparkle/WinSparkle/AppImageUpdate for auto-update.
- Dev loop: Linux edit → `cmake --build` Linux → offline-render + pluginval integration tests → **remote Windows dev VM** (RDP/SSH from Linux) for WASAPI/ASIO exercise → rsync to Windows QA box for real-hardware smoke tests → GitLab CI on tag for release.

## Technical Approach

### 4.1 Architecture

```
+-------------------+       +---------------------------+
|  User (stage)     |       |  Controller (MIDI/OSC)    |
+---------+---------+       +-------------+-------------+
          | mouse/kbd                     | CC / OSC / Note / PC
          v                               v
+-----------------------------+   +----------------------+
|  UI Thread (message thread) |   |  Input Thread(s)      |
|  - Component tree            |   |  - MidiInput callback |
|  - MenuBarModel              |   |  - OscReceiver thread |
|  - Drag-to-connect canvas    |   +----------+-----------+
|  - Patch Organiser           |              |
+------+----------------+------+              | lock-free SPSC
       |                |                     v
       | graph edits    |             +----------------------+
       |                |             |  Dispatch (message)   |
       |                |             |  ParameterMapping     |
       |                |             |  ApplicationCommand   |
       |                |             +----------+-----------+
       v                v                        |
+-----------------------------+                  v
|  Session / PatchRegistry    |<----------+------+
|  - current/queued patch      |         param updates
|  - .pdl serialization        |               |
|  - default.pdl / autosave    |               |
+---------+-------------------+               |
          | current patch                     |
          v                                    v
+-------------------------------------------------------------+
|  AudioGraphRuntime (real-time thread, JUCE AudioProcessorGraph)|
|  - AudioInputNode, AudioOutputNode, MidiInputNode, MidiOutputNode |
|  - BypassableInstance wrappers (with ramped bypass, NaN guard)    |
|  - Internal processors (Level, VU, FilePlayer, Recorder, Looper, Metronome, OutputToggle) |
|  - PDC: automatic latency alignment across parallel paths          |
+---------+----------------------------------+------------------+
          |                                   |
          v                                   v
 Audio Device (JUCE AudioDeviceManager)   Plugin editor windows
  Windows: ASIO / WASAPI / DirectSound    (hosted via PluginEditorWindow)
  macOS:   CoreAudio
  Linux:   ALSA / JACK / PipeWire
```

**Key architectural invariants** (the "live performance robustness contract"):

1. **No allocation, no locking, no I/O on the audio thread.** All RT→UI and UI→RT messaging goes via lock-free SPSC queues (JUCE's `AbstractFifo` is adequate; upgrade to `rtrb`-equivalent if needed). No `std::mutex`, no `new/delete`, no file access.
2. **Patch switch is atomic at a buffer boundary.** The outgoing graph receives all-notes-off on its last sample, then the incoming graph (pre-built on a worker thread, all plugins `prepareToPlay`'d) is swapped in by atomic pointer exchange. Target SLA: **zero buffer underruns at 256 samples @ 48kHz** across 1000 consecutive switches on QA hardware. No crossfade in v3.0.
3. **Plugin crash does not crash the host.** v3.0 uses in-process hosting with SEH/signal handlers on the audio thread that mark a node as "dead" and substitute silence; the host keeps running. Out-of-process sandbox is a v3.1 goal.
4. **NaN/Inf guard on the master output.** A final-stage node clamps any sample of `|x| > 1.5` or `NaN/Inf` to 0; logs the event once per second to the Event Log.
5. **`.pdl` saves are atomic.** Write to `.pdl.tmp`, fsync, rename — classic durable-write pattern.
6. **All state mutations from outside the RT thread go through a `Session` object** that owns the current graph, current patch index, queued patch index, and dirty flag. Session is the single source of truth for save/load and undo.
7. **RT-safety is enforced by tooling, not discipline alone.** Debug builds use Clang's RealtimeSanitizer (`-fsanitize=realtime`) with `[[clang::nonblocking]]` on the audio callback; violations (`malloc`, `pthread_mutex_lock`, any syscall) abort at runtime. CI runs a `linux-rtsan` preset on every merge.

> 📎 **Research Insights — invariants #1, #3, #4**
>
> - `AbstractFifo` ordering: relaxed on own-side index; acquire on reading the other thread's index; release on publishing own update. Never `seq_cst` on the audio thread. See §Appendix A.3.
> - NaN guard: scalar `std::isfinite` costs ~5 cycles/sample — fine for a 2-channel master output bus at 48 kHz (~0.25%/core). **Do not** apply per-node — a 20-plugin patch would burn 5% of one core. Use SSE2 `_mm_cmpord_ps` + branch-free mask when you need 8+ channel masters. §Appendix A.2 has code.
> - SEH on Windows is table-based on x64 — near-zero cost on the happy path. **Must live in a C-style shim function** because MSVC forbids `__try`/`__except` in the same function as any C++ object with a destructor. See §Appendix A.2 for the shim and the `_resetstkoflw()` stack-overflow recovery detail.
> - Do **not** use `_set_se_translator` + `/EHa` — inhibits optimizer, allocates on throw, unwinds plugin stack through potentially-corrupted frames.
> - POSIX `SIGSEGV` via `sigsetjmp`/`siglongjmp` is technically possible (async-signal-safe per POSIX.1-2008 TC2) but brittle on macOS. Ship Linux-only opt-in; off on macOS (v3.1 bridge process covers it).

### 4.1.1 Patch-switch state machine

States, transitions, guards. Single state variable `patchSwitchState` lives on the `Session` (message thread); audio thread reads via `std::atomic<Graph*> live` only.

```
                     ┌────────────────────────────────────────────────┐
                     │                                                │
     queueSwitch(P)  │                                                │
 ┌───────────────────┴────────┐                                       │
 │         IDLE                │──────► if preloaded cache hit ─┐     │
 │   live = graphA             │                                 │     │
 │   queued = nullptr          │◄─────────────────────────────┐  │     │
 └───────────────┬─────────────┘                              │  │     │
                 │ queueSwitch(P), cache miss                 │  │     │
                 ▼                                            │  │     │
 ┌──────────────────────────────┐ worker.buildPreparedGraph   │  │     │
 │      PREPARING               │◄────────────────────────────┘  │     │
 │   live = graphA              │                                 │     │
 │   queued = <building>        │──► build failure               │     │
 │   worker thread active       │   (plugin ctor crashed,         │     │
 └───────────────┬──────────────┘    prepareToPlay timeout)       │     │
                 │ build complete                                  │     │
                 │ (plugins prepared, state restored)              │     │
                 ▼                                                 │     │
 ┌──────────────────────────────┐                                 │     │
 │      READY_TO_SWAP            │  queueSwitch(P2) during          │     │
 │   live = graphA               │  PREPARING → coalesce:            │     │
 │   queued = graphB (ready)     │  discard queued, start P2.        │     │
 │   waiting for audio thread    │  (depth-1 queue, latest-wins)    │     │
 │   to observe swap             │                                   │     │
 └───────────────┬──────────────┘                                   │     │
                 │ audio thread: atomic exchange                     │     │
                 │ (flush all-notes-off on graphA first)             │     │
                 ▼                                                   │     │
 ┌──────────────────────────────┐                                   │     │
 │      CLEANUP                  │   queueSwitch(P3) during         │     │
 │   live = graphB               │   CLEANUP → defer until           │     │
 │   queued = nullptr            │   CLEANUP completes, then         │     │
 │   graphA → trash FIFO         │   transition IDLE → PREPARING.    │     │
 │   trash drained on msg thread │                                   │     │
 └───────────────┬──────────────┘                                   │     │
                 │ trash FIFO emptied, graphA destroyed               │     │
                 └───────────────────► IDLE ─────────────────────────┘     │
                                                                            │
 Recovery paths:                                                            │
                                                                            │
 PREPARING ──build failure──► IDLE (graphA still live; error surfaced in UI)
 READY_TO_SWAP ──user cancels──► IDLE (graphB returned to trash FIFO)
 CLEANUP ──trash drain stalls > 5s──► force destroy (log warning)
```

**Invariants.**

- Audio thread never observes a `live` pointer that is not `prepareToPlay`-ready.
- At most one `PREPARING` in flight; depth-1 queue with latest-wins coalescing (rapid Program Change bursts collapse to the last one).
- `live` swap happens on a buffer boundary, inside `AudioGraphRuntime::processBlock` at block start after the all-notes-off flush of the outgoing graph.
- Outgoing graph's `releaseResources()` runs on the **message thread** only, after the audio thread has acked observation of the new `live` pointer (one-block wait or generation counter).
- A `.pdl` load in progress blocks new switches until load completes (AC-PS-2).

> 📎 **Research Insights — patch swap**
>
> - JUCE's `AudioProcessorGraph` has `UpdateKind::{sync, async, none}` on `addNode`/`addConnection`. For worker-thread graph builds: use `UpdateKind::none` for every mutation and call `graph.rebuild()` once at end. Saves N-1 rebuilds.
> - `juce::AudioProcessorPlayer::setProcessor()` is the canonical single swap point — but it calls `prepareToPlay` on the calling thread. For live performance with 20-plugin heavy patches, we swap *inside* a custom `AudioProcessor` (which JUCE's `AudioProcessorPlayer` drives) rather than replacing the player's processor. See §Appendix A.1.
> - All-notes-off must iterate 16 channels × `{allNotesOff, allSoundOff, allControllersOff}` on the outgoing graph's final `processBlock` with a silent audio buffer. Some synths ignore `allNotesOff`; belt-and-braces would track live notes and send per-note `Note-Off`, but Pedalboard2 didn't and no one complained.

### 4.2 Subsystems

**A. Audio engine (`app-v3/src/engine/`)**

- `AudioGraphRuntime` — wraps JUCE `AudioProcessorGraph` with patch-swap support (`swapPreparedGraph(std::unique_ptr<AudioProcessorGraph>)` on audio thread).
- `BypassableInstance` — port v2's concept: ramped bypass (fade-in/fade-out, no click), NaN guard per-node, CPU metering per-node.
- `PluginHost` — wraps `AudioPluginFormatManager`; loads VST3/AU/CLAP/LV2/(VST2 optional).
- `PluginScanner` — sandboxed out-of-process scan (JUCE `PluginDirectoryScanner` with `scanForPluginInProcess=false`); maintains a `crashed_plugins.json` list.
- `LatencyCompensator` — walks graph, aligns parallel paths to the slowest leaf; per-patch opt-out toggle.
- Internal processors (one file each): `AudioInputNode`, `AudioOutputNode`, `MidiInputNode`, **`MidiOutputNode` (new)**, `LevelProcessor`, `VuMeterProcessor`, `FilePlayerProcessor`, `AudioRecorderProcessor`, `LooperProcessor`, `MetronomeProcessor`, `OutputToggleProcessor`.
- `MainTransport` — global play/stop/RTZ broadcaster (port v2's ChangeBroadcaster pattern to JUCE's `ChangeBroadcaster` or a modern `AsyncUpdater` equivalent).

**B. Session / persistence (`app-v3/src/session/`)**

- `Session` — holds current `PatchRegistry`, current-patch pointer, dirty flag, patch-switch state machine (§4.1.1). **Does NOT own `Preferences` or `UndoHistory`** — those are siblings, not children, to prevent god-object drift.
- `Preferences` — user-scoped settings (audio device choice, OSC bind address, last-used paths, window geometry). Persisted separately in `UserDataDir/preferences.xml`; lifetime spans sessions.
- `UndoHistory` — per-session bounded stack of graph-topology + mapping snapshots. Plugin-internal-parameter changes are NOT undoable (users use plugin presets).
- `PatchRegistry` — ordered array of `Patch` objects; import/reorder/copy operations.
- `Patch` — graph snapshot + connections + mappings + custom plugin names + per-patch transport state.
- `AtomicFileWriter` — shared durable-write primitive: write-temp → `fsync`/`FlushFileBuffers` → `rename`/`MoveFileEx(MOVEFILE_REPLACE_EXISTING | WRITE_THROUGH)` → POSIX dir-`fsync`. Used by `PdlSerializer`, `Preferences`, `ColourScheme`, `PluginScanner` crash list, and user preset library. No direct `FileOutputStream` writes to user-data files.
- `PdlSerializer` — writes v3 `.pdl` (XML, `schemaMajor="3" schemaMinor="0"`). Round-trip preservation of unknown elements OR read-only mode for future-major files (AC-FS-7). XXE-safe, size-bounded parser (AC-SE-7..8).
- `PluginIdentity` — immutable format-native identity key: `(format, classId/UID, name, vendor)` tuple. **Distinct from `PluginLocation`** (runtime file path, regenerated by scanner) and `PluginDescriptor` (display metadata cache). Resolution goes through `KnownPluginList` only; `.pdl` files never carry file paths (AC-SE-5..6).
- `UserDataDir` — per-platform path helper, **not a singleton** — passed through an `AppContext` struct from `App`:
  - Windows: `%APPDATA%\Pedalboard3\`
  - macOS: `~/Library/Application Support/Pedalboard3/`
  - Linux: `~/.config/Pedalboard3/`
- **Migration:** on first launch, detect `Pedalboard2/` data dir; offer one-click import of colour schemes, app-mappings, user presets. Never modify v2 files. `default.pdl` is copied-with-tagging (adds `<IMPORTED_FROM_V2 originalPath timestamp />`), not moved.

**B.1 v2 import subsystem (`app-v3/src/session/v2_import/`)** — isolated from the v3 session layer so it can be retired cleanly once the v2 corpus is migrated.

- `PdlV2Importer` — reads Pedalboard2 XML (`<Pedalboard2PatchFile>/<Patch>/<FILTERGRAPH>/<FILTER>/<PLUGIN>` + `<CONNECTION>` + `<Mappings>/<MidiMapping>/<OscMapping>` + `<UserNames>/<Name>`). Lossy-with-warning on plugin state blobs.
- `PluginIdentityRemapper` — reads community-curated `plugin_aliases.json` to map VST2 UIDs to VST3 Class IDs / CLAP IDs / AU components. Interactive substitute-on-load UI.
- `ColourSchemeV2Importer` — reads v2 `.colourscheme` XML (19 tokens per `source code/ColourSchemeColours.txt`) and maps to v3 named tokens (`bg.window`, `connection.audio`, etc.).
- `AppMappingsV2Importer` — reads v2 `AppMappings.xml` with MIDI CC / OSC / keyboard → command ID mappings; translates command IDs to v3's named command namespace.

> 📎 **Research Insights — v2 import**
>
> Plan: §4.5 `<IDENTITY>` element already has the right fields. Additions from research:
>
> - v2's `.pdl` root tag is `<Pedalboard2PatchFile>` (not `<FILTERGRAPH>`); each `<Patch>` has `name`/`tempo` attributes; the graph root inside is `<FILTERGRAPH>`. (ref: `source code/src/MainPanel.cpp:1523,1560,1579`)
> - v2's plugin state is a base64 CDATA child of `<FILTER>` named `<STATE>`. (ref: `source code/src/FilterGraph.cpp:410-412,460-463`)
> - Special MIDI channel number in v2 connections: `FilterGraph::midiChannelNumber = 0x1000` (4096). Any v2 CONNECTION with `srcChannel=4096` is MIDI. (ref: `source code/src/FilterGraph.cpp:66`)
> - v2's `oscMIDIAddress` attribute lives on `<FILTER>` itself, not in Mappings — an awkward v2 choice. v3 migrates these to proper `<OSC_MAPPING>` rows.
> - v2 has a hidden `Trigger` parameter on FilePlayer (any value >0.5 → RTZ + play) — must preserve (AC-FP-2).
> - v2's `overridePluginMidi` toggle is **UI-only, not serialized** — add it to v3 schema (this is a user-visible bug fix, not parity loss).
> - See §Appendix D for the full attribute mapping table and the 19 colour token map.

**C. Control (`app-v3/src/control/`)**

- `MidiRouter` — single source of MIDI ingest; routes to `MidiMappingManager`, to graph `MidiInputNode`, and to `ApplicationCommandDispatcher`. MIDI Learn flow.
- `MidiMapping` — CC / Note / Program Change / Pitchbend → parameter mapping, with latch, inverted range, per-channel filter, "override plugin's built-in MIDI" toggle (v2 parity).
- `OscServer` — `juce::OSCReceiver` + `juce::OSCSender`; configurable bind interface (default: loopback + local LAN; 0.0.0.0 is opt-in); multicast support.
- `OscMapping` — address → parameter (with wildcard support as open question Q30; TBD).
- `OscOutputPolicy` — opt-in per mapping, symmetric addressing (same address for in/out), fires only on user-UI and MIDI-driven changes (not on automation echo) to prevent feedback.
- `ApplicationCommandDispatcher` — maps MIDI/OSC/keyboard → commands (`patch_next`, `patch_prev`, `patch_select N`, `transport_play`, `transport_stop`, `transport_rtz`, `tap_tempo`, `recorder_toggle`, `bypass_all`, `show_window`, etc.).
- `MidiAppFifo`-equivalent — lock-free SPSC queue for audio-thread → message-thread command handoff.

**D. UI (`app-v3/src/ui/`)**

- `MainWindow` (`DocumentWindow`) — menus, main panel.
- `PluginFieldView` — the drag-to-connect canvas (port v2's `PluginField`). OpenGL-backed for smooth pan/zoom. Node rendering, connection rendering with Bezier curves, pin hit-testing, lasso-select.
- `PluginNodeView` — the card for each plugin node; title label, input/output/parameter pins, edit/mappings/bypass/delete buttons.
- `PluginConnectionView` — Bezier curves, selectable-for-delete.
- `PluginEditorWindow` — hosts the plugin's own editor with a `PresetBar` strip above.
- `TransportBar`, `PatchBar`, `CpuMeterView`, `EventLogView`.
- Dialogs: `AudioSettingsDialog`, `PluginListDialog`, `PreferencesDialog`, `ColourSchemeEditor`, `ApplicationMappingsEditor`, `PatchOrganiserDialog`, `MappingsDialog`, `TapTempoBox`, `OscMonitorWindow` (new — live incoming-OSC display for debugging).
- `PedalboardLookAndFeel` subclass of `LookAndFeel_V4`.
- `ColourScheme` — 19 v2 tokens preserved + new tokens for added UI elements, with importers for v2 `.colourscheme` files.
- High-contrast scheme shipped built-in; all dialogs Tab/Shift-Tab/Enter/Esc navigable; keyboard-reachable plugin-field focus/selection.
- `TrayIcon` — Windows/Linux only; macOS uses `NSStatusItem` via JUCE `SystemTrayIconComponent` (with macOS-appropriate defaults: no close-to-tray, menu-bar-only).

**E. Platform glue (`app-v3/src/platform/`)**

- Windows: ASIO wrapper (ships in official build under GPLv3 ASIO license), WASAPI, DirectSound. Code-signing verification of installer at runtime.
- macOS: CoreAudio, AU component manager integration. Hardened-runtime entitlements (`cs.disable-library-validation`, `cs.allow-jit`, `cs.allow-unsigned-executable-memory`, `NSMicrophoneUsageDescription`) documented per-entitlement in `docs/ci.md` (AC-SE-36..38).
- Linux: ALSA, JACK (optional, runtime-detected), PipeWire (via PulseAudio/JACK API).

**F. Diagnostics / observability (`app-v3/src/diagnostics/`)** — new module. Owns every cross-cutting signal so the other modules don't grow ad-hoc logging.

- `EventLog` — RT-safe append-only ring of structured events. Writers use a lock-free `choc::VariableSizeFIFO` (or `rtlog-cpp`-style deferred formatting — the audio thread pushes `(enum EventKind, uint64 ts, char payload[128])`; the message thread drains and renders strings). Consumed by `EventLogView` for user-visible log plus a rotated disk log.
- `StructuredLogger` — sinks JSON Lines for agent consumption; wraps `EventLog` for human-readable formatting.
- `NaNGuardRateLimiter` — deduplicates NaN-guard events to 1/s per node (AC-SE-4 compliance without log spam).
- `CrashReporter` — opt-in (AC-SE-27): installs SEH (Windows) / signal (POSIX) handlers, writes minidumps to `UserDataDir/crashes/<timestamp>.dmp`. Message-thread Timer scans the folder for dumps left by previous runs, prompts user to upload.
- `SymbolUploadService` — CI-side tool that `dump_syms` the Windows PDBs, macOS dSYMs, Linux debuginfo into Breakpad `.sym` format and uploads to a symbol store (self-hosted or Sentry). Counterpart `minidump-stackwalk` on the dev Linux server resolves user-uploaded dumps.
- `RtMailbox<T>` — shared lock-free SPSC ring primitive used by every RT lane. Uniform metrics (drops, high-water mark) per instance for debuggability.
- `MetricsRegistry` — lightweight per-plugin CPU/time counters sampled at 10 Hz by a UI Timer; powers AC-NEW-6 without per-sample RT overhead.

> 📎 **Research Insights — diagnostics**
>
> - RT log formatting happens on the consumer thread, not the audio thread. `std::string` construction from the audio thread is an allocation; push raw `(format_token, args...)` tuples instead.
> - Per-plugin CPU measurement via `std::chrono::steady_clock` bracketing each `processBlock` call is ~40 ns overhead per block × 20 plugins × 187 blocks/s = ~150 µs/s ≈ 0.015% of one core. Free; use `std::atomic<uint64_t>` accumulators with `memory_order_relaxed`.
> - Minidump symbolication on Linux via `rust-minidump` + `dump_syms`: Windows PDBs → `.sym` files in CI; Linux dev server holds the symbol store; `minidump-stackwalk crash.dmp ./symbols` produces a readable stack.
> - Crash reports contain plugin-state blobs = potential PII (sample-library paths, DRM tokens). AC-SE-28 mandates scrubbing before upload.

### 4.3 Implementation Phases

#### Phase 0 — Infrastructure skeleton (2-3 weeks)

**Goal: prove the hardest infra before writing audio code.**

- `app-v3/CMakeLists.txt` + `CMakePresets.json` with presets: `linux-dev`, `linux-release`, `windows-ci`, `windows-cross-check` (clang-cl+xwin, sanity only), `macos-universal`.
- `justfile` or `Makefile.dev` with one-liners: `just build`, `just test`, `just scan-plugins`, `just render-golden`, `just cross-check-windows`, `just push-to-qa`.
- Hello-world JUCE GUI app that produces signed artifacts on all three platforms.
- GitLab CI `.gitlab-ci.yml` with all stages wired (lint → test → build → sign → release → publish) — even the signing stubs, so credential rotation lives in CI from day 1.
- Self-hosted Windows runner registered on QA box; self-hosted Mac mini runner provisioned; Linux shared runner validated.
- Azure Trusted Signing enrolled (or SignPath Foundation if open-source); Developer ID + App Store Connect API key provisioned; GPG key generated and backed up offline.
- `AGENTS.md` / `CLAUDE.md` in repo root documenting build, test, push-to-QA, what an agent can verify from Linux vs what requires Windows.
- **Decision:** provision a dedicated Windows dev VM (separate from QA machine) for WASAPI/ASIO iteration from Linux via RDP/SSH. (Resolves Q7 / C7.)

**Deliverable:** `v0.0.0-infra` tag produces signed installers on all three platforms, each installing and launching a window that says "Pedalboard 3."

#### Phase 1 — Audio engine core (4-6 weeks)

- `AudioGraphRuntime` with atomic patch-swap.
- `BypassableInstance` with ramped bypass + NaN guard.
- `PluginHost` loading VST3 + AU (macOS) + CLAP via `juce_clap_hosting` + LV2 (Linux).
- Sandboxed `PluginScanner` with crash list.
- Audio I/O via `AudioDeviceManager` (ASIO/WASAPI/CoreAudio/ALSA).
- Internal processors: `AudioInputNode`, `AudioOutputNode`, `MidiInputNode`, `LevelProcessor`, `VuMeterProcessor` (minimum viable set).
- **Offline render test harness** — feeds audio in from WAV, renders graph to WAV, diffs against committed golden WAV using an in-tree deterministic test plugin (not Surge/Dexed — their versions churn).
- **Pluginval integration test** — CI job runs pluginval against Surge XT / Dexed / Dragonfly at pinned versions.

**Deliverable:** load a VST3, connect input→plugin→output, render audio, verify output offline. Measurable patch-swap benchmark (even without UI yet).

#### Phase 2 — Session, patches, persistence (3-4 weeks)

- `Session`, `PatchRegistry`, `Patch`, `PdlSerializer` (write v3 `.pdl`).
- v2 `.pdl` **read** support: topology + mappings + custom names + transport state; plugin state defaulted with per-plugin warning.
- `PluginIdentity` abstraction + UI for substitute-on-load prompts.
- `default.pdl` autoload; save-as-default command.
- Atomic save (write-temp + rename + fsync).
- Window layout persistence (plugin editor positions, main window geometry) per-.pdl with sensible defaults.

**Deliverable:** open a v2 `.pdl` from a real user's corpus, see its patches and connections materialize correctly; save as v3 `.pdl`, reopen, graph is bit-identical.

#### Phase 3 — UI (4-6 weeks)

- `MainWindow`, `PluginFieldView` with drag-to-connect + lasso-select + keyboard navigation.
- `PluginNodeView`, `PluginConnectionView`, `PluginEditorWindow`, `PresetBar`.
- `TransportBar`, `PatchBar`, `CpuMeterView`, `EventLogView`.
- Menus: File / Edit / Options / Help.
- Core dialogs: Audio Settings, Plugin List, Preferences, Patch Organiser.
- Undo/redo for graph topology + mappings only (plugin internals not undoable).
- `PedalboardLookAndFeel` + colour-scheme system + high-contrast built-in scheme.
- Tray icon (Windows/Linux) / menu-bar (macOS).

**Deliverable:** a user can load plugins, wire them, save a patch, switch patches — without MIDI or OSC yet.

#### Phase 4 — Control surface: MIDI, OSC, mappings (3-4 weeks)

- `MidiRouter`, `MidiMapping` (CC / Note / PC / Pitchbend, latch, inverted range, per-channel filter, override-plugin-MIDI).
- MIDI Learn flow + MIDI CC indicator toast on first unmapped CC (v2's `MidiCcAlertWindow` pattern).
- `OscServer`, `OscMapping`, `OscOutputPolicy`.
- `OscMonitorWindow` (new — live incoming-OSC display).
- `ApplicationCommandDispatcher` + mapping of MIDI/OSC/keyboard → commands.
- `MappingsDialog`, `ApplicationMappingsEditor`.
- v2 parity features: MIDI over OSC (both `,m` typetag and int/float triplet forms), OSC multicast, MIDI Program Change → patch switch, MMC → transport.

**Deliverable:** a user can map a fader on a MIDI controller to a plugin parameter, map a footswitch to next-patch, and drive the entire app from TouchOSC.

#### Phase 5 — Remaining internal processors + polish (3-4 weeks)

- `FilePlayerProcessor` (with hidden `Trigger` parameter for sampler-style use, streaming from disk for files >30s).
- `AudioRecorderProcessor` (post-record file-unload safety; threaded writer).
- `LooperProcessor` + `LooperEditor` (configurable max loop length, default 10 min stereo; patch-scoped).
- `MetronomeProcessor` (click audio files, tempo sync).
- `OutputToggleProcessor`.
- **`MidiOutputNode` (new)** — MIDI routing in the graph.
- PDC automatic alignment across parallel paths; per-patch opt-out.
- Drag-drop support: `.pdl` files, sound files (auto-create FilePlayer), plugin binaries.
- Colour scheme import from v2 `.colourscheme` files.

**Deliverable:** feature-parity with Pedalboard2 plus MIDI out and PDC.

#### Phase 6 — Release hardening, auto-update, distribution (2-3 weeks)

- WiX v5 MSI for Windows; bundle VC++ redist via burn bootstrapper; sign with Azure TS.
- DMG for macOS via `create-dmg`; codesign (hardened runtime) → notarytool submit+wait → stapler.
- AppImage + `.deb` + `.rpm` for Linux; GPG-sign the AppImage; `dpkg-sig` for deb; `rpm --addsign` for rpm.
- Sparkle (macOS) + WinSparkle (Windows) + AppImageUpdate (Linux); all EdDSA-signed; private keys in GitLab CI protected variables, **backed up offline** (losing the key bricks auto-update for all existing installs).
- Auto-update **suppressed while audio device is open** (both Sparkle and WinSparkle).
- Release pipeline tag-triggered (`v\d+\.\d+\.\d+`), with a manual approval gate before publish, bound to a protected `production` environment.
- Generic Package Registry as artifact store; GitLab Release as landing page.

**Deliverable:** `v3.0.0` tag → signed, notarized, stapled installers on all three platforms + auto-update feed.

### 4.4 File layout (initial)

```
app-v3/
├── CMakeLists.txt
├── CMakePresets.json
├── JuceLibraryCode/        # juce_add_gui_app generated
├── external/
│   ├── JUCE/               # submodule, pinned
│   ├── juce_clap_hosting/  # submodule, pinned
│   ├── clap/               # submodule, pinned (for host-side headers)
│   └── asio_sdk/           # optional, GPLv3 variant
├── src/
│   ├── main.cpp
│   ├── App.h / App.cpp     # JUCEApplication subclass; owns AppContext
│   ├── engine/             # AudioGraphRuntime, BypassableInstance, PluginHost, PluginScanner, LatencyCompensator
│   ├── engine/processors/  # Internal processors (one file each)
│   ├── session/            # Session, Preferences, UndoHistory, PatchRegistry, Patch, PdlSerializer, PluginIdentity
│   ├── session/v2_import/  # PdlV2Importer, PluginIdentityRemapper, ColourSchemeV2Importer, AppMappingsV2Importer
│   ├── control/            # MidiRouter, MidiMapping, OscServer, OscMapping, ApplicationCommandDispatcher
│   ├── diagnostics/        # EventLog, StructuredLogger, CrashReporter, RtMailbox, MetricsRegistry
│   ├── ui/                 # MainWindow, PluginFieldView, dialogs, LookAndFeel, ColourScheme
│   └── platform/           # Platform-specific glue (tray icon, ASIO, etc.)
├── tests/
│   ├── unit/               # Catch2
│   ├── integration/
│   │   ├── golden-render/  # WAV in, WAV out, diff vs golden
│   │   ├── pluginval/      # pluginval CI integration
│   │   └── v2-pdl-corpus/  # real v2 .pdl files from users (with plugins stubbed)
│   └── fixtures/
│       └── test-plugins/   # in-tree deterministic test plugins
├── packaging/
│   ├── windows/            # WiX .wxs, installer.iss, signing scripts
│   ├── macos/              # entitlements.plist, dmg scripts, notarize.sh
│   └── linux/              # AppImage recipe, debian/, rpm/
├── scripts/
│   ├── plugin-scan.sh
│   ├── run-pluginval.sh
│   ├── render-and-diff.sh
│   └── push-to-qa.sh
├── docs/
│   └── v2-migration.md     # user-facing migration guide
└── .gitlab-ci.yml
```

### 4.5 Data model — Patch serialization (XML, schema v3.0)

**Schema evolution policy** (AC-FS-7): `schemaMajor` and `schemaMinor` are separate integers. v3.x readers accept files where `schemaMajor == 3` regardless of `schemaMinor`, **preserving unknown elements and attributes verbatim through load→save round-trip** so future-minor fields are not silently dropped. A file with `schemaMajor > 3` opens in read-only mode with a user banner. Defaults are append-only per major line: changing a default ships as `schemaMajor = 4`. Every attribute with a default is documented in `docs/pdl-schema.md` with the `appVersion` it was introduced in.

```xml
<PEDALBOARD schemaMajor="3" schemaMinor="0" appVersion="3.0.0"
            createdWithAppVersion="3.0.0" lastSavedWithAppVersion="3.0.0">
  <PATCH name="Clean Lead" order="0">
    <NODES>
      <NODE id="1" kind="internal" type="AudioInputNode" x="40" y="100" />
      <NODE id="2" kind="plugin" x="200" y="100" bypass="false" customName="Compressor">
        <IDENTITY format="VST3" classId="1234...5678" name="FabFilter Pro-C 2" vendor="FabFilter" />
        <STATE encoding="base64">...plugin getStateInformation blob...</STATE>
      </NODE>
      <NODE id="3" kind="internal" type="AudioOutputNode" x="500" y="100" />
    </NODES>
    <CONNECTIONS>
      <CONN src="1" srcChan="0" dst="2" dstChan="0" />
      <CONN src="1" srcChan="1" dst="2" dstChan="1" />
      <CONN src="2" srcChan="0" dst="3" dstChan="0" />
      <CONN src="2" srcChan="1" dst="3" dstChan="1" />
    </CONNECTIONS>
    <MAPPINGS>
      <MIDI cc="74" channel="all" latch="false" invert="false"
            nodeId="2" paramIndex="3" rangeLo="0.0" rangeHi="1.0"
            overridePluginMidi="false" />
      <OSC address="/pedalboard/compressor/threshold" outputEcho="true"
           nodeId="2" paramIndex="3" rangeLo="0.0" rangeHi="1.0" />
    </MAPPINGS>
    <TRANSPORT tempo="120.0" />
  </PATCH>
  <APPLICATION_MAPPINGS>
    <MIDI cc="64" channel="1" command="patch_next" />
    <OSC address="/pedalboard/patch/prev" command="patch_prev" />
    <KEY code="space" command="transport_toggle" />
  </APPLICATION_MAPPINGS>
  <PREFERENCES>
    <AUDIO saveInPdl="false" />
    <OSC bind="127.0.0.1" port="8000" multicast="" />
    <COLOUR_SCHEME name="default" />
  </PREFERENCES>
</PEDALBOARD>
```

### 4.6 ERD (Mermaid)

```mermaid
erDiagram
    SESSION ||--|| PATCH_REGISTRY : owns
    PATCH_REGISTRY ||--o{ PATCH : contains
    PATCH ||--o{ NODE : contains
    PATCH ||--o{ CONNECTION : contains
    PATCH ||--o{ MIDI_MAPPING : contains
    PATCH ||--o{ OSC_MAPPING : contains
    NODE }o--|| PLUGIN_IDENTITY : references
    NODE ||--o| PLUGIN_STATE_BLOB : has
    NODE ||--o{ PLUGIN_PARAMETER : exposes
    CONNECTION }o--|| NODE : from
    CONNECTION }o--|| NODE : to
    MIDI_MAPPING }o--|| PLUGIN_PARAMETER : targets
    OSC_MAPPING }o--|| PLUGIN_PARAMETER : targets
    SESSION ||--|| PREFERENCES : owns
    SESSION ||--o{ APPLICATION_MAPPING : contains
    APPLICATION_MAPPING }o--|| COMMAND : triggers
```

## Alternative Approaches Considered

### A1. Rust + Tauri + clack-host + vst3-sys + egui (rejected)

Attractive for modern tooling, memory-safe RT code, and web-based UI theming. **Rejected because format coverage for a *host* is the whole game**, and in April 2026:

- **VST3 hosting in Rust is DIY FFI** — `vst3-sys` is low-level, no equivalent of JUCE's battle-hardened `VST3PluginFormat` (thousands of lines handling parameter mapping, bus arrangements, component/edit-controller duality, persistence).
- **AU hosting in Rust is essentially nothing prebuilt.**
- **CLAP hosting via `clack-host` is genuinely good** — it would win for a CLAP-only host. But that's a different product.
- Tauri's IPC serialization is fine for a node-graph editor's event rate; it's not a blocker. The UI story is actually nicer than JUCE for theming.
- No precedent for a serious audio-host app shipping on Tauri. Being the test pilot for host-class apps *and* porting a VST3/AU format layer is too much scope for one rewrite.

**When this would win:** if Pedalboard 3 were deliberately CLAP-only and positioned as a forward-looking modern-format host.

### A2. Stay on JUCE but update in place (rejected)

The original idea of "just update JUCE in the existing source tree." Rejected because:

- The ~2012-era code uses Projucer (`.jucer`) configuration rather than CMake; JUCE 8's modern workflow is CMake-first.
- `NiallsOSCLib` and `NiallsSocketLib` are custom predecessors of JUCE's native OSC/socket support; carrying them forward is maintenance burden.
- `AudioProcessorGraph` API has evolved; `FilterGraph.cpp` needs substantial rewrite anyway.
- The known threading and patch-switch bugs are architectural; fixing them requires touching most files.
- The user explicitly asked for a ground-up rewrite.

### A3. Tracktion Engine (JUCE-based, higher level)

`tracktion_graph` is a production-grade graph engine with automatic PDC and multicore rendering. **Considered and held in reserve** — if Phase 1's custom PDC + graph scheduling proves costly, switching to `tracktion_graph` is a viable mid-project pivot because it sits within the JUCE ecosystem.

### A4. iPlug2, plain VST3 SDK + Qt, CPLUG, etc.

Not suitable. iPlug2 is plugin-dev only, no hosting surface. Qt + raw VST3 SDK means reimplementing what JUCE provides. CPLUG is a thin C plugin wrapper, not a host.

## System-Wide Impact

### Interaction graph

When a user moves a MIDI controller on stage:

1. OS MIDI callback fires on the **audio thread** (or a JUCE MIDI-input thread, depending on driver).
2. `MidiRouter::handleIncomingMidiMessage` dispatches: (a) into the audio graph as `MidiBuffer` for `MidiInputNode`, (b) into `MidiMappingManager::midiCcReceived` which scans multimap for matching mappings and calls `setParameter` on target BypassableInstances, (c) into `MidiAppFifo` for `ApplicationCommand`s (next patch, tap tempo, etc.) to be drained on the message thread.
3. If the message is a Program Change matching a patch-switch mapping, it enters the **queued-patch-switch** state machine: a worker thread instantiates the incoming graph, `prepareToPlay`s all plugins, restores state; on completion, it posts to the audio thread to swap on the next buffer boundary.
4. During the swap, outgoing MIDI-receiving plugins receive all-notes-off; BypassableInstance wrappers fade out; new graph is atomic-swapped in; BypassableInstance wrappers fade in.
5. UI thread `Timer` polling the `MidiAppFifo` updates the Patch combobox; `ChangeBroadcaster` fires on `Session::dirty`; plugin editor windows for the old graph's plugins are closed if the plugin is not in the new graph.

### Error propagation

- **Audio-thread errors** (plugin crash, NaN output) are caught by SEH/signal-handler, marked on the `BypassableInstance`, do not propagate. User sees a non-blocking Event Log entry; node is visually flagged.
- **Message-thread errors** (file save failure, OSC bind failure) surface as in-app notifications; do not block live performance.
- **Plugin-load errors** (scan crash, state restore failure) leave the graph in a valid state (placeholder node or default-init plugin) with user-visible warning.
- **CI-side errors** in signing, notarization, runner outage fail the release pipeline loudly; never publish unsigned artifacts.

### State lifecycle risks

- **Partial patch-switch failure** (one plugin in the incoming patch fails to load): incoming graph is discarded, outgoing graph stays live, user sees error, no glitch. Tested by injecting a crash on `prepareToPlay`.
- **Save-during-crash**: atomic temp-rename means the on-disk `.pdl` is never truncated mid-write.
- **Audio-device-swap during queued patch switch**: serialized via `Session`'s state machine (both operations are message-thread initiated).
- **MIDI port removed during mapping**: mapping stays, port shows as "offline"; when replugged, mapping auto-reconnects.

### API surface parity

- **JUCE's `AudioPluginHost` sample** is the direct reference — much of `PluginFieldView`, `PluginNodeView`, `PluginConnectionView` can be adapted from `GraphEditorPanel.cpp`/`InternalPlugins.cpp`.
- **v2 `.pdl` compatibility** is the primary API-surface obligation on the import side.
- **OSC namespace** should follow conventions established by v2 where users may have existing TouchOSC rigs; document the v3 namespace additions.

### Integration test scenarios

1. **Load a real v2 `.pdl` from a user corpus** (collected from still-active Pedalboard 2 users), verify topology + mappings preserve; render audio through the graph offline; diff against a reference.
2. **1000 consecutive random patch switches at 5 switches/second** at 256-sample buffer @48kHz on QA hardware; assert zero buffer underruns via loopback capture.
3. **Plugin-crash injection during live audio** — load a test plugin that aborts on block 1000; assert audio thread recovers, node is flagged, host stays up.
4. **Audio-device hot-swap** (unplug USB audio interface mid-playback); assert no crash, default device substituted, tempo-synced processors recompute timings.
5. **10kHz OSC traffic on a single address** for 60s; assert no buffer underruns and UI stays responsive.

## Acceptance Criteria

### Functional (v2 parity + new)

- [ ] **AC-PS-1** 1000 consecutive random patch switches at 5/s produce zero audio-output buffer underruns at 256 samples @48kHz on QA hardware.
- [ ] **AC-PS-2** Queued patch switch received during `.pdl` load applies after load completes; never dropped.
- [ ] **AC-PS-3** MIDI PC to non-existent patch index → non-blocking notification; current patch continues.
- [ ] **AC-PS-4** Outgoing MIDI-receiving plugins receive all-notes-off on last sample before disconnect.
- [ ] **AC-PS-5** Switching to a patch with a missing plugin completes; placeholder node with preserved mappings.
- [ ] **AC-FS-1** `.pdl` save uses write-temp + atomic-rename; forced kill mid-save leaves previous file intact.
- [ ] **AC-FS-2** v2 `.pdl` imports with 100% of patch names, topology, connections, custom plugin names, MIDI/OSC/keyboard mappings, and global tempo preserved; plugin states defaulted with per-plugin warning.
- [ ] **AC-FS-3** `.pdl` written by newer v3 either opens (forward-compat fields ignored) or fails with clear message; never silently loses data.
- [ ] **AC-FS-4** `default.pdl` load failure falls back to empty graph within 2s.
- [ ] **AC-FS-5** `.pdl` with 500 nodes + 2000 connections loads under 10s with UI progress.
- [ ] **AC-IO-1** MIDI input during plugin scan does not deadlock or drop messages (v2 regression fix).
- [ ] **AC-IO-2** App closed while receiving 1000 msg/s MIDI exits cleanly (v2 regression fix).
- [ ] **AC-IO-3** OSC port bind conflict → non-blocking error + retry; app functional.
- [ ] **AC-IO-4** MIDI learn cancelled → no stale mapping fires on next CC.
- [ ] **AC-IO-5** MIDI CC Latch toggles 100 rapid presses with no missed toggles.
- [ ] **AC-IO-6** MIDI over OSC accepts both `,m` typetag and int/float triplet forms (v2 parity).
- [ ] **AC-IO-7** OSC multicast join validated for 224.0.0.0-239.255.255.255 range.
- [ ] **AC-IO-8** Inverted parameter range produces inverted behavior (v2 parity).
- [ ] **AC-IO-9** Per-plugin MIDI channel filter + override-plugin-MIDI toggle work correctly under load.
- [ ] **AC-PL-1** Plugin crash: audio continues as silence/last-known-good; app remains responsive.
- [ ] **AC-PL-2** Plugin scan skips previously-crashed plugins; user has button to clear crash list.
- [ ] **AC-PL-3** Plugin GUI close-button tolerant of plugins that hide rather than close.
- [ ] **AC-PL-4** Plugin state-restore failure → plugin in default state, user warned; save does not overwrite unrestored blob.
- [ ] **AC-AD-1** Audio device hot-swap: graph rebuilds, channel-mismatched connections red-flagged, no crash.
- [ ] **AC-AD-2** Sample-rate change: tempo-delays and FilePlayer positions recomputed without click.
- [ ] **AC-AD-3** Buffer-size change: no crash, no click, latency reporting updated.
- [ ] **AC-TR-1** 1000 transport play+stop in 60s: no drift >1 sample in any synced FilePlayer.
- [ ] **AC-TR-2** Tap tempo uses moving average of last 4 taps, resetting if gap >2s.
- [ ] **AC-TR-3** Metronome sync-to-transport toggles without click.
- [ ] **AC-TR-4** AudioRecorder via MIDI→MainTransport always starts (v2 regression fix).
- [ ] **AC-FP-1** All 19 v2 colour-scheme tokens import losslessly.
- [ ] **AC-FP-2** FilePlayer hidden `Trigger` parameter (>0.5 → RTZ+play) preserved.
- [ ] **AC-FP-3** AudioRecorder post-record file-unload safety behavior preserved.
- [ ] **AC-FP-4** "Save As Default" → `default.pdl` autoload on launch.
- [ ] **AC-FP-5** "Save audio settings in `.pdl`" toggle symmetric at save/load.
- [ ] **AC-FP-6** Drag-drop sound file → auto-create FilePlayer.
- [ ] **AC-FP-7** Drag-drop `.pdl` → load (with unsaved-changes prompt).
- [ ] **AC-FP-8** `Home` key assignable to application mapping (v2 bug fixed).
- [ ] **AC-NEW-1** MIDI Output node available; MIDI routing in the graph works.
- [ ] **AC-NEW-2** Automatic PDC across parallel paths in a patch; per-patch opt-out toggle.
- [ ] **AC-NEW-3** Undo/redo for graph topology + mappings; 50-step history.
- [ ] **AC-NEW-4** OSC Monitor window shows incoming OSC addresses/values with 1s decay.
- [ ] **AC-NEW-5** CLAP plugins load and run via `juce_clap_hosting`.
- [ ] **AC-NEW-6** Plugin CPU usage visible per-node in graph UI.

### Non-functional

- [ ] Audio latency (round-trip): ≤ 10ms at 256-sample buffer @48kHz on reference Windows audio interface.
- [ ] App cold-start to usable graph: ≤ 3s on QA box.
- [ ] Plugin scan of 500 VST3 plugins completes in ≤ 3 min on QA box.
- [ ] Memory footprint: < 250 MB with empty patch; < 2 GB with 20 active plugins + 10-minute stereo looper.
- [ ] CPU overhead (host-only, no plugins): < 5% on QA box at 256-sample @48kHz.
- [ ] High-contrast scheme passes 4.5:1 contrast on text labels.
- [ ] Keyboard-navigable all menus/dialogs (Tab/Shift-Tab/Enter/Esc).

### Security / robustness

- [ ] **AC-SE-1 (REVISED)** OSC server default bind is **`127.0.0.1` only**. Any non-loopback bind (interface IP, `0.0.0.0`) requires explicit user action in `PreferencesDialog` and persists per-install (not per-`.pdl`).
- [ ] **AC-SE-2** Windows firewall prompt on opt-in non-loopback OSC bind expected and documented.
- [ ] **AC-SE-3** No user-supplied path used in shell/eval context.
- [ ] **AC-SE-4** NaN/Inf guard: no sample to audio device with `|x|>1.5` or `NaN/Inf`.

#### .pdl file parsing
- [ ] **AC-SE-5** `.pdl` files contain no plugin file paths; `PluginIdentity` resolution exclusively via `KnownPluginList` `(format, classId, name, vendor)` tuple.
- [ ] **AC-SE-6** `PdlSerializer` rejects unknown path-like attributes on plugin nodes; strict allowlist schema validation.
- [ ] **AC-SE-7** XML parser does not resolve external entities or DTDs; unit tests cover XXE / billion-laughs / external-DTD payloads.
- [ ] **AC-SE-8** `.pdl` parser enforces hard limits: 32 MB file, 10K nodes, 50K connections, 1K mappings/patch, 16 MB state blob/node.
- [ ] **AC-SE-30** `setStateInformation` wrapped in the same SEH/signal guard as `processBlock`; failure marks node dead and preserves original blob (per AC-FS-9).
- [ ] **AC-SE-31** Plugin state blob byte count validated vs declared size before invocation; mismatch → corrupt-`.pdl` error, per-plugin warning.
- [ ] **AC-SE-32** FilePlayer paths: relative to configured media root OR user-approved absolute paths; unseen absolute paths trigger allow-once / always / never prompt.
- [ ] **AC-SE-33** UNC paths, URL-scheme paths (`smb://`, `afp://`, `ftp://`), and `..` traversal rejected at `.pdl` parse time.
- [ ] **AC-SE-34** AudioRecorder destination never loaded from `.pdl`; always user-configured via file dialog at record time.

#### OSC network exposure
- [ ] **AC-SE-9** Non-loopback OSC bind triggers first-use confirmation modal with explicit network-exposure warning.
- [ ] **AC-SE-10** Optional source-IP allowlist in `PreferencesDialog`; non-allowlisted packets dropped silently.
- [ ] **AC-SE-11** Optional shared-secret auth header on OSC messages; off by default; documented for multi-device rigs.

#### Auto-update (if enabled in v3.0; optional deferral to v3.0.1 is under consideration — see §Open Questions)
- [ ] **AC-SE-12** Sparkle/WinSparkle appcast XML itself is EdDSA-signed; unsigned appcasts rejected by the updater without user prompt.
- [ ] **AC-SE-13** Updater enforces version monotonicity; rollback requires manual reinstall.
- [ ] **AC-SE-14** WinSparkle verifies Authenticode on MSI before launch; Sparkle codesign verification enabled; AppImageUpdate verifies zsync + GPG.
- [ ] **AC-SE-15** Binaries embed two EdDSA public keys (`current` + `next-rotation`); releases signed with both.
- [ ] **AC-SE-16** Update signing keys stored in ≥2 geographically separated offline backups (printed air-gapped paper + hardware token).
- [ ] **AC-SE-17** Appcast served over HTTPS with HSTS; no plaintext fallback; cert pinned to issuing CA.

#### CI secret hygiene
- [ ] **AC-SE-18** All signing secrets (`AZURE_TS_*`, `APPLE_P12_*`, `APPLE_API_KEY`, `SPARKLE_*`, `WINSPARKLE_*`, `GPG_*`) marked `protected: true` + `masked: true`; referenced only from protected-branch / protected-tag pipelines.
- [ ] **AC-SE-19** CI lint job fails if `CI_DEBUG_TRACE` or `CI_DEBUG_SERVICES` is ever enabled.
- [ ] **AC-SE-20** No signing secret passed as command-line argv; stdin or `0600` temp-file only, deleted in `after_script` even on failure.
- [ ] **AC-SE-21** Signing-key-rotation runbook at `docs/ci-signing-rotation.md`; annual dry-run exercised.
- [ ] **AC-SE-22** Fork / external-MR pipelines cannot access signing variables; enforced by CI lint and protected-branch policy.

#### Plugin scan consent
- [ ] **AC-SE-23** First-run plugin scan is **never automatic**; explicit user consent with paths shown/editable before any plugin binary is loaded.
- [ ] **AC-SE-24** `crashed_plugins.json` stored with `0600` ACL; strict schema parsing; corruption triggers re-scan prompt, not silent reset.
- [ ] **AC-SE-25** macOS plugin-scan child process runs under most restrictive sandbox profile compatible with hosting (no `~/Documents`, no `~/Desktop`, no network).
- [ ] **AC-SE-26** Windows plugin-scan child process runs in a Job Object with kill-on-close; AppContainer as v3.1 goal.

#### Crash reports
- [ ] **AC-SE-27** Crash reporting is **opt-in at install and per-session**; default off.
- [ ] **AC-SE-28** Dumps scrubbed of file paths outside app bundle, env vars, plugin state blobs (replaced with `[REDACTED size=N]`), and MIDI device names before upload; user can preview.
- [ ] **AC-SE-29** HTTPS upload with pinned cert; ≤90 d retention; deletion-on-request in `docs/privacy.md`.

#### MIDI
- [ ] **AC-SE-35** SysEx >64 KB truncated-with-warning, never buffered unboundedly; truncated SysEx never forwarded to the graph.

#### macOS entitlements
- [ ] **AC-SE-36** `entitlements.plist` contents enumerated in `docs/ci.md` with per-entitlement rationale; minimum set: `cs.disable-library-validation`, `cs.allow-jit`, `cs.allow-unsigned-executable-memory`, `device.audio-input`.
- [ ] **AC-SE-37** `Info.plist` includes `NSMicrophoneUsageDescription` with truthful copy.
- [ ] **AC-SE-38** CI post-build runs `codesign -d --entitlements :-` and diffs against a committed expected-entitlements file; mismatch fails the build.

#### Supply chain
- [ ] **AC-SE-39** All submodules (JUCE, `juce_clap_hosting` if used, CLAP SDK, ASIO SDK, VST3 SDK) pinned by exact commit SHA; updates require commit-documented review.
- [ ] **AC-SE-40** Test-fixture plugin downloads in CI verified against committed `tests/fixtures/SHA256SUMS`.
- [ ] **AC-SE-41** Test/integration CI jobs have no signing-secret access; enforced via per-job variables allowlist.

#### Observability / operational
- [ ] **AC-SE-42** `EventLogView` shows plugin display name only; full paths only in off-by-default debug log.
- [ ] **AC-SE-43** Release page shows GPG fingerprint; AppImage filename includes full version (`Pedalboard3-3.0.0-x86_64.AppImage`).
- [ ] **AC-SE-44** Pre-release AV-vendor submission (Microsoft Defender + top 3 others); publish stage blocked until clearance recorded.

### Data integrity

- [ ] **AC-FS-6** `.pdl` and every file in `UserDataDir` writes via `AtomicFileWriter`: write-temp → `fsync` / `FlushFileBuffers` → `rename` / `MoveFileEx(WRITE_THROUGH)` → POSIX dir-`fsync`. Fault-injection test kills process between steps and asserts old OR new file is intact (never truncated).
- [ ] **AC-FS-7** v3.x readers preserve unknown elements/attributes through load→save round-trip for files with matching `schemaMajor`; files with higher `schemaMajor` open read-only with user banner.
- [ ] **AC-FS-8** Opening a v2 `.pdl` shows a modal describing lossy-import; after dismissal, Save is disabled and only Save As... is enabled. UI test asserts original v2 bytes unchanged.
- [ ] **AC-FS-9** Unrestored plugin state blobs (identity mismatch, plugin missing, restore crash/silent-default) are preserved verbatim and written back on next save. Test: load `.pdl` with unavailable plugin → save → bytes identical.
- [ ] **AC-FS-10** `.pdl` written on one OS resolves FilePlayer sample paths on others via three-tier search: relative-to-`.pdl`, user-library root, absolute. Missing files → locate prompt, never silent empty-player.
- [ ] **AC-FS-11** Per-blob soft cap 50 MB (warning), hard cap 500 MB (refuses save with actionable error); per-file soft cap 250 MB, hard cap 2 GB.
- [ ] **AC-FS-12** Second Pedalboard 3 instance detects first via advisory lock on `UserDataDir/.instance.lock`; second instance focuses first OR runs with Preferences in read-only mode; no Preferences corruption.
- [ ] **AC-FS-13** Every attribute with a default in `.pdl` is listed in `docs/pdl-schema.md` with the `appVersion` it was introduced in; defaults are append-only per v3 major line.

### Performance guardrails

- [ ] **AC-RT-1** Debug build uses Clang `-fsanitize=realtime` with `[[clang::nonblocking]]` on the audio callback; CI `linux-rtsan` preset fails on any alloc/lock/syscall violation. Violations from `juce::VST3PluginInstance::processBlock` and other known upstream paths suppressed with documented `__rtsan::ScopedDisabler` regions.
- [ ] **AC-RT-2** 1-hour memory soak test in CI: load a reference `.pdl`, switch patches every 3 s, monitor RSS; fail if growth exceeds 100 MB/hr after 5 min warm-up.
- [ ] **AC-RT-3** `mimalloc` is the global allocator on Windows and Linux; macOS uses system allocator. R17 mitigation.
- [ ] **AC-PS-6** Queued patch switch preload begins within 1 ms of trigger fire on worker thread.
- [ ] **AC-NEW-2a** When computed PDC compensation exceeds threshold T (configurable, default 5 ms), emit event-log warning with path identification and offer one-click disable.
- [ ] **AC-NEW-2b** Per-node latency reported by plugin is visible in `PluginNodeView` tooltip.
- [ ] **AC-NEW-6a** Per-node CPU metering overhead <0.1% of one core at 20 plugins × 256 samples @ 48kHz.
- [ ] **AC-NEW-7** Plugin scan parallelizes across `hardware_concurrency()/2` helper processes; helper binary signed alongside main app.
- [ ] **AC-FP-1a** FilePlayer maintains per-file ring buffer ≥500 ms; first 2 s pre-cached at patch load.
- [ ] **AC-FP-1b** I/O stall in one FilePlayer does not affect others; RT thread never blocks on disk.
- [ ] **AC-LP-1** Looper enforces per-instance max length (default 10 min stereo, configurable up to 60 min); over-cap action user-selectable (stop vs loop-over).
- [ ] **AC-LP-2** Total looper+FilePlayer memory is bounded and reported in `CpuMeterView`.
- [ ] **AC-UI-1** Canvas render at 100 nodes + 200 connections sustains 60 fps on QA hardware; drops to 10 fps on idle.
- [ ] **AC-UI-2** VU meter updates decoupled from canvas repaint rate.
- [ ] **AC-ST-1** First paint (window visible + menu interactable) under 500 ms.
- [ ] **AC-ST-2** Audio device open completes or falls back to default within 2 s.
- [ ] **AC-ST-3** Plugin scan never blocks UI; runs in background after first paint with progress indicator.

### Distribution & signing

- [ ] **AC-DS-1** Windows MSI signed with Azure Trusted Signing; installs cleanly on Win10/11 with SmartScreen; uninstall leaves no user data.
- [ ] **AC-DS-2** macOS: Developer ID signed + notarized + stapled; Gatekeeper passes on fresh machine.
- [ ] **AC-DS-3** Linux: AppImage GPG-signed; deb/rpm have sane deps; AppImageUpdate delta works.
- [ ] **AC-DS-4** Auto-update suppressed while audio device is open.
- [ ] **AC-DS-5** CI simulates expired cert → release job fails within 1 min; never publishes unsigned.

### CI / dev loop

- [ ] **AC-CI-1** Linux-only push: Linux build + tests + pluginval passes ≤ 15 min.
- [ ] **AC-CI-2** Windows runner offline: pipeline retries 3× with 10m backoff; does not block unrelated merges.
- [ ] **AC-CI-3** Release tag triggers all three platforms in parallel; artifacts published atomically.
- [ ] **AC-CI-4** Offline-render tests use in-tree deterministic test plugin (not external plugins).

### Quality gates

- [ ] Catch2 unit tests cover engine, session, control subsystems; target ≥ 70% line coverage.
- [ ] Pluginval at strictness 5 passes against Surge XT, Dexed, Dragonfly Reverb on all three platforms (at pinned versions).
- [ ] Golden-WAV regression passes on every merge to main.
- [ ] Manual approval gate before `release:publish` stage (protected `production` environment).
- [ ] `CHANGELOG.md` updated for every tagged release.
- [ ] `docs/v2-migration.md` updated when `.pdl` schema or v2-import logic changes.

## Success Metrics

- **Stability:** Zero crash reports per 100 hours of logged live-performance use within 3 months of v3.0.0.
- **Compatibility:** 100% of a curated 10-`.pdl` v2-user corpus opens in v3 with correct topology + mappings; plugin state preservation rate > 80% (measured: v3 auto-loaded the same plugin with the same params).
- **Performance:** Median patch-switch audible-glitch rate < 1 in 1000 switches under SLA conditions.
- **Adoption:** v3.0.0 downloads ≥ 500 in first month; ≥ 5 published TouchOSC templates or YouTube walkthroughs in community within 6 months.
- **Dev velocity:** time from Linux edit to "Windows QA result" ≤ 5 min on inner loop (via rsync-to-QA) and ≤ 20 min on CI milestone path.

## Dependencies & Prerequisites

### External dependencies

- **JUCE 8** (submodule, pinned to a tested tag). Currently ~8.0.12 as of research date; re-pin before Phase 1.
- **`juce_clap_hosting`** (Jatin Chowdhury, community module). Community-maintained — pin to a known-good commit; budget time for JUCE-upgrade reconciliation.
- **CLAP SDK** (MIT, headers only, submodule).
- **ASIO SDK** — GPLv3 variant (Oct 2025 relicense) for official build. For closed-source builds, existing proprietary agreement still applies.
- **VST3 SDK** — MIT (Oct 2025 relicense). Submodule.
- **Optional VST2 SDK** — user-supplied, build-gated, never in official binary.
- **Catch2 v3** (unit tests).
- **Pluginval** (Tracktion, integration tests).
- **Test fixture plugins:** Surge XT, Dexed, Dragonfly Reverb (pinned versions downloaded in CI, not redistributed).

### Toolchain

- CMake ≥ 3.28 with `CMakePresets.json`.
- C++23 compiler: MSVC 19.38+ (VS 2022), Clang 17+, GCC 13+.
- Rust toolchain on the Linux dev box only for auxiliary tools (`rust-minidump` for symbolicating Windows crash dumps on Linux).

### Infrastructure

- GitLab project created.
- Self-hosted Windows runner on QA desktop (`gitlab-runner` as a Windows service, **not** LocalSystem — a dedicated service account with the runner user's desktop session).
- Self-hosted Mac mini (M2 or M4) running `launchctl bootstrap gui/<uid>` with auto-login and FileVault disabled for runner user.
- Dedicated Windows dev VM (separate from QA box) for WASAPI/ASIO iteration — RDP or SSH accessible from the Linux dev server.
- Linux shared runners on GitLab.com (free tier).
- Apple Developer account ($99/year).
- Windows signing: Azure Trusted Signing subscription (~$10/mo) **or** SignPath Foundation for OSS.
- GPG key pair for Linux AppImage signing; offline backup.

### People

- One developer (user) + AI coding assistant on the Linux side.
- One QA operator (could be same person) on Windows desktop with at least 20 commercial VST3 + CLAP + VST2 plugins covering synths, effects, complex editors, and high-latency plugins.

## Risk Analysis & Mitigation

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | `juce_clap_hosting` upstream is abandoned (zero commits since Oct 2022). Windows HWND embedding stubbed; 10 Hz main-thread dispatch; bus count hard-coded 2-in/2-out. | **High** | **High** | **Primary mitigation: defer CLAP to v3.1**. If included in v3.0: vendor private fork (start from `areadenialgames/juce_clap_hosting@926ba25` — JUCE 8 + MIDI + transport + macOS GUI); pin CLAP SDK 1.2.7 + clap-helpers; budget 1-2 wk to fix Windows HWND embedding; pluginval integration test covers Surge XT CLAP / Vital CLAP / Diva CLAP. See §Appendix A.4. |
| R2 | Patch-switch SLA (zero underruns at 1000 switches) unachievable with in-process hosting | Medium | High | Phase 1 ends with a patch-switch benchmark; if missed, either (a) raise buffer size floor, (b) move plugin instantiation to a dedicated worker thread, (c) pivot to `tracktion_graph`. |
| R3 | Plugin crashes cannot be recovered from in-process | Medium | High | Ship NaN-guard + SEH-guard in v3.0; if real-world crash reports prove in-process hosting insufficient, defer out-of-process sandboxing to v3.1. Document the limitation. |
| R4 | Windows QA runner offline during release | High | Medium | Release pipeline retries 3× with backoff; manual re-run gate; runner status on a status-page. |
| R5 | Code-signing cert expires mid-release | Medium | High | CI job checks expiry ≥30 days in advance and opens a reminder issue; Azure Trusted Signing's 3-day cert auto-rotation minimizes this for Windows. |
| R6 | macOS notarization rejects embedded helpers (Sparkle, etc.) | High | Medium | Every Mach-O in the bundle signed with hardened runtime; `codesign --deep` + explicit helper signing; first macOS CI run in Phase 0 exercises the full notarize path. |
| R7 | JUCE 8 + `libwebkit2gtk-4.1` vs `-4.0` Linux compatibility | Medium | Low | Build Linux AppImage on Ubuntu 22.04 base for max glibc back-compat; test on 24.04. |
| R8 | v2 `.pdl` corpus test plugins unavailable on CI | High | Low | Provide anonymized `.pdl` corpus with plugin references stubbed to in-tree test plugins; users can bring real corpus locally. |
| R9 | yabridge Wine compatibility breaks (useful for Linux dev VST loading) | Medium | Low | yabridge is a dev-convenience only; losing it doesn't block release since QA runs on real Windows. |
| R10 | Losing the Tauri/Sparkle/WinSparkle updater signing key | Low | Catastrophic | Offline backups; at least two copies in separate secure locations; rotation drill documented. |
| R11 | Plugin editor window embedding differs Wayland vs X11 | Medium | Medium | Target X11 primarily; document Wayland as best-effort; test on both at Phase 3. |
| R12 | AGPLv3 JUCE incompatible with a future closed-source commercial fork | N/A | N/A | Acknowledged: for any closed-source spinoff, purchase JUCE Indie/Pro license; open-source Pedalboard 3 is fine on AGPLv3. |
| R13 | ASIO GPLv3 license propagates to any closed-source build | N/A | N/A | Same as R12 — only affects hypothetical future closed-source build. |
| R14 | Steinberg changes VST3 license again | Low | Low | Pin to VST3 SDK 3.8.0 (MIT) tag; future licenses only affect upgrades. |
| R15 | cargo-xwin cross-check breaks | Medium | Low | Cross-check is sanity-only; CI always has native Windows runner for truth. Tolerable failure mode. |
| R16 | User bricked by lost auto-update key | Low | High | Offline key backup in **two geographically separated sites**; two-key rotation (current + next) baked into every release (AC-SE-15); annual dry-run (AC-SE-21). **Fallback: defer auto-update to v3.0.1**, ship v3.0.0 with manual download only — removes the risk entirely from the first release window. |
| R17 | Heap fragmentation under long-session patch churn (6-hour gig = millions of heap ops; default Windows heap and glibc malloc both fragment) | Medium | High | `mimalloc` as global allocator on Windows/Linux (AC-RT-3); 1-hour memory soak test in CI (AC-RT-2); RSS telemetry in Event Log every 60 s during live-use mode. |
| R18 | `AudioProcessorGraph` PDC does not auto-recompute on runtime latency changes (plugin toggles oversample/lookahead mid-session) | Medium | Medium | Hook `AudioProcessorListener::audioProcessorChanged(..., latencyChanged=true)` on every plugin; trigger `graph.rebuild()` on message thread; expose latency per node in UI tooltip (AC-NEW-2b). |
| R19 | `KnownPluginList` reads not thread-safe in JUCE 8 | Medium | Low | Wrap in `juce::ReadWriteLock` or snapshot-on-demand; `control/PluginListRegistry` encapsulates. |
| R20 | First-run plugin scan auto-executes malicious plugin | High | Critical | AC-SE-23: first-run scan is never automatic; explicit user consent with editable path list before any plugin loads. |

## Resource Requirements

### Time estimate (calendar)

Assuming one primary developer with AI assistance + part-time QA:

| Phase | Weeks | Key deliverable |
|---|---|---|
| 0 — Infra | 2–3 | Signed hello-world on all 3 platforms |
| 1 — Audio engine | 4–6 | VST3 loaded, graph rendered, patch-swap benchmark |
| 2 — Session / `.pdl` | 3–4 | v2 `.pdl` import + v3 save/load |
| 3 — UI | 4–6 | Full graph-editing UI |
| 4 — MIDI/OSC | 3–4 | Full control-surface parity |
| 5 — Processors + polish | 3–4 | Feature parity + MIDI out + PDC |
| 6 — Release | 2–3 | v3.0.0 tagged and published |
| **Total** | **21–30 weeks** | v3.0.0 shipped |

### Infrastructure costs (approximate, monthly)

- Apple Developer: $99/year = ~$8/mo
- Azure Trusted Signing: ~$10/mo (or SignPath Foundation $0 if OSS)
- Mac mini (one-time ~$500 for used M2) + power: negligible
- Windows QA box (already owned) + power: negligible
- GitLab: Free tier adequate; Premium ($29/user/mo) only if more compute minutes needed.
- Hosting (GitLab Pages for updater feeds / landing): $0

### Human

- 1 developer full-time equivalent for 6 months.
- 1 QA operator (can be same person) ~10 h/week for regression testing and v2-corpus validation.

## Future Considerations

- **v3.1 — Out-of-process plugin sandbox.** Each plugin in a sandboxed child process; IPC over shared-memory ring buffers; plugin crash = child process respawn. Model: Bitwig's bit-bridge, Reaper's bridge. Significant engineering effort (~8-12 weeks).
- **v3.1 — MIDI output routing in the graph + MIDI plugin processing.** Beyond the simple `MidiOutputNode`, a full MIDI graph (MIDI filters, MIDI effects as plugin processors).
- **v3.2 — Multicore graph scheduling.** Parallel paths rendered on worker threads; requires careful PDC re-analysis. Consider `tracktion_graph` adoption at this point.
- **v3.2 — VSTi / synth-focused features.** Polyphonic aftertouch, MPE, per-voice modulation — more relevant once the host stability bar is proven.
- **v3.x — Web-based remote control.** A companion web UI (not embedded in the host) that connects over WebSocket to the host, similar to how Bitwig exposes controller scripts. This is where a React/Tauri-style UI stack could live *alongside* the native host, without contaminating the audio thread.
- **v3.x — Plugin preset library & tagging.** Centralized preset browser with tags and search.
- **v3.x — Screen-reader accessibility.** AX roles, labels, hierarchy — out of scope for v3.0 but worth scoping as a v3.2/v3.3 effort.
- **v3.x — Linux audio polish.** First-class PipeWire, JACK graph integration (Pedalboard 3 as a first-class JACK client).
- **v3.x — Live snapshots / performance presets.** Per-patch "performance presets" that snapshot parameter states for rapid within-patch switching, distinct from whole-patch switches.

## Documentation Plan

- `README.md` — overview, platforms, how to install.
- `CHANGELOG.md` — user-facing release notes, semver.
- `docs/v2-migration.md` — v2 user onboarding: what imports, what doesn't, how to recreate.
- `docs/osc-namespace.md` — complete OSC address reference (parity with v2's docs plus new additions).
- `docs/built-in-processors.md` — each internal processor, its inputs/outputs/params, parity notes with v2.
- `docs/patches.md` — patches, preset, default-patch workflow.
- `docs/mapping.md` — MIDI + OSC mapping guide, MIDI learn, latch, inverted range.
- `docs/colour-schemes.md` — colour-scheme tokens, import from v2.
- `docs/architecture.md` — internal architecture (audio thread, message thread, FIFOs, patch-switch state machine) for contributors.
- `docs/building.md` — build on Linux, cross-check on Linux, native Windows/macOS build.
- `docs/ci.md` — GitLab CI pipeline reference, runner setup, signing runbook, cert rotation runbook.
- `AGENTS.md` / `CLAUDE.md` — agent-facing instructions: commands, what's verifiable from Linux, what requires Windows QA.
- Inline documentation on `BypassableInstance`, `AudioGraphRuntime`, patch-switch state machine — the three places a future maintainer will most need guidance.

## Open Questions (to resolve before or during Phase 0)

From the SpecFlow analysis; defaults are documented inline but should be confirmed with the user:

- **Q1. Patch-switch SLA numeric target** — default: zero underruns at 256-sample @48kHz over 1000 switches on QA box.
- **Q2. Plugin crash isolation model for v3.0** — default: in-process best-effort + NaN-guard; out-of-process is v3.1.
- **Q3. v2 `.pdl` import fidelity** — default: topology + mappings + custom names fully; plugin state defaulted with warning.
- **Q4. VST2 in official binary** — default: user-compile-only on Windows/Linux; excluded on macOS.
- **Q5. Crossfade vs atomic swap on patch switch** — default: atomic swap + all-notes-off.
- **Q6. Looper cross-patch behavior** — default: patch-scoped (v2 parity).
- **Q7. Remote dev story for Windows audio code** — default: dedicated Windows dev VM separate from QA box.
- **Q8. v2 data dir migration policy** — default: detect, offer one-click import, never modify.
- **Q9. MIDI output in scope for v3.0** — proposed: yes (new `MidiOutputNode` in Phase 5).
- **Q10. Undo/redo scope** — default: graph topology + mappings; plugin internals not undoable.
- **Q11. PDC policy** — default: automatic alignment across parallel paths; per-patch opt-out.
- **Q12. Windows runner availability target** — default: retries + does-not-block-merge.
- **Q13. Multi-thread plugin processing** — default: single-threaded in v3.0.
- **Q14. MIDI-learn message type coverage** — default: CC, Note, Program Change, Pitchbend (no NRPN/SysEx in v3.0).
- **Q15. OSC output topology** — default: opt-in per mapping, symmetric addressing, user-driven only.
- **Q16. Screen-reader accessibility** — default: no screen-reader in v3.0; keyboard + high-contrast yes.
- **Q17. OSC learn UX** — proposed: port v2's passive-learn + add `OscMonitorWindow` for live inspection.
- **Q18. FilePlayer/Looper memory policy** — default: configurable max loop length (default 10 min stereo); stream from disk for FilePlayer files >30s.
- **Q19. Installer plugin path auto-discovery** — default: pre-populate OS-standard paths on first run + scan-now prompt.
- **Q20. v2 colour-scheme migration** — default: read v2 `.colourscheme` files, remap to v3 token set, write v3 `.pbscheme` going forward.

## 14. "Ruthless v3.0" alternative (11-13 week scope)

Documented for consideration if calendar pressure forces cuts. This is **not the chosen plan** — it is a fallback scope that maximizes the probability of shipping in 3 months at the cost of feature breadth.

**What it is.** A VST3 + AU plugin host that loads v2 `.pdl` patches, drag-to-connect, MIDI/OSC parameter mapping, rapid patch switching. Signed installers on Windows (Azure TS) and macOS (Developer ID + notarized). Linux AppImage as a dev-artifact, not a release SLA. **No CLAP** (defer to v3.1). **No undo/redo** (defer). **No automatic PDC** (manual delay nodes only). **No auto-update** (manual download via website). **No per-node CPU meters, no OSC Monitor window** (log filter instead). Three modules (`audio/`, `app/`, `ui/`) instead of five.

**Cuts and reasoning:**

- **CLAP** — `juce_clap_hosting` is a single-maintainer, 3.5-years-stale community module with Windows HWND embedding stubbed. Loss: ~3% of plugin universe. Gain: 1-1.5 wk + no ongoing JUCE-upgrade tax.
- **Undo/redo** — v2 shipped 13 years without it; "undo during a live show" is not a real workflow. Loss: 0. Gain: 1.5-2 wk.
- **Automatic PDC** — correctness rabbit hole (sample-rate changes, plugins lying about latency, sidechain paths). Users can insert manual delay nodes if they care. Loss: wet/dry phase alignment. Gain: 1.5-2 wk.
- **Auto-update** — lost-key brick risk (R16) is catastrophic; manual update via website is fine for first thousand users. Loss: download friction. Gain: R10/R16 entirely removed from v3.0 release window. Add in v3.0.1 once keys and rotation drill are battle-tested.
- **Per-node CPU meters / OSC Monitor window** — cosmetic. A single global host-CPU number and a filter on `EventLogView` cover 90% of the use case. Gain: ~1 wk combined.
- **High-contrast colour scheme** — ships as downloadable `.colourscheme` file, not built-in. Gain: 1-2 days.

**Calendar sketch:** Week 1 CI + hello-world. Weeks 2-5 engine + `.pdl` + v2 import. Week 5 = user can load v2 patch and hear audio. Weeks 6-8 UI (add Windows signing this window). Weeks 9-10 MIDI + OSC + mappings (add macOS notarization this window). Week 11 remaining internal processors + polish. Week 12 release. Week 13 QA headroom.

**Decision point:** by end of Phase 0, if infra bring-up took >3 weeks OR if the Windows dev VM story (§Appendix B) proves harder than expected, escalate to this scope. The chosen "full" plan assumes ~6 months; this alternative hard-caps at 3 months with a narrower product.

---

## Appendix A — Engine code skeletons (from JUCE 8 research brief)

These are abridged from a comprehensive JUCE 8 research brief executed against `docs.juce.com/master`, the `extras/AudioPluginHost` reference, and the JUCE forum canon. Full brief retained as research artifact; excerpts below are what go directly into the code.

### A.1 `AudioGraphRuntime::swapPreparedGraph`

```cpp
// engine/AudioGraphRuntime.h
class AudioGraphRuntime : public juce::AudioProcessor {
public:
    void prepareToPlay(double sr, int block) override {
        currentSampleRate.store(sr, std::memory_order_release);
        currentBlockSize .store(block, std::memory_order_release);
        if (auto* g = live.load(std::memory_order_acquire))
            g->prepareToPlay(sr, block);
    }

    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        const juce::ScopedNoDenormals nodn;
        auto* g = live.load(std::memory_order_acquire);
        if (g == nullptr) { buf.clear(); return; }
        g->processBlock(buf, midi);
        sanitiseOutput(buf);                         // NaN/Inf guard — §A.2
        generationAckedByAudio.fetch_add(1, std::memory_order_release);
    }

    // Message thread only. Worker has already built & preparedToPlay() the new graph.
    std::unique_ptr<juce::AudioProcessorGraph>
    swapPreparedGraph(std::unique_ptr<juce::AudioProcessorGraph> next)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        flushMidiReceivers(live.load(std::memory_order_acquire));  // all-notes-off
        auto* rawNext = next.release();
        auto* rawPrev = live.exchange(rawNext, std::memory_order_acq_rel);

        // Wait for audio thread to observe the new pointer (generation counter bump).
        const auto target = generationAckedByAudio.load() + 2;  // at least one full block
        while (generationAckedByAudio.load(std::memory_order_acquire) < target)
            juce::Thread::sleep(1);

        if (rawPrev) rawPrev->releaseResources();
        return std::unique_ptr<juce::AudioProcessorGraph>(rawPrev);   // caller defers delete
    }

private:
    std::atomic<juce::AudioProcessorGraph*> live { nullptr };
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };
    std::atomic<uint64_t> generationAckedByAudio { 0 };

    static void sanitiseOutput(juce::AudioBuffer<float>&) noexcept;
    static void flushMidiReceivers(juce::AudioProcessorGraph*);
};
```

Worker-side build (runs on a dedicated `juce::Thread`):

```cpp
std::unique_ptr<juce::AudioProcessorGraph>
PatchBuilder::buildPreparedGraph(const Patch& p, double sr, int block,
                                 juce::AudioPluginFormatManager& fmts)
{
    auto g = std::make_unique<juce::AudioProcessorGraph>();
    g->setPlayConfigDetails(2, 2, sr, block);

    juce::String errOut;
    for (const auto& nodeSpec : p.nodes) {
        auto instance = fmts.createPluginInstance(
            nodeSpec.identity.toDescription(), sr, block, errOut);
        if (instance == nullptr) { /* placeholder; log */ continue; }
        instance->enableAllBuses();
        instance->setPlayHead(playHead);
        instance->setStateInformation(nodeSpec.stateBlob.getData(),
                                      (int) nodeSpec.stateBlob.getSize());
        g->addNode(std::make_unique<BypassableInstance>(std::move(instance)),
                   juce::AudioProcessorGraph::NodeID { nodeSpec.graphId },
                   juce::AudioProcessorGraph::UpdateKind::none);    // defer rebuild
    }
    for (const auto& c : p.connections)
        g->addConnection({ { juce::AudioProcessorGraph::NodeID { c.src }, c.srcChan },
                           { juce::AudioProcessorGraph::NodeID { c.dst }, c.dstChan } },
                         juce::AudioProcessorGraph::UpdateKind::none);
    g->rebuild();                     // single rebuild after batch
    g->prepareToPlay(sr, block);      // heavy; stays off audio thread
    return g;
}
```

All-notes-off helper:

```cpp
static void flushMidiReceivers(juce::AudioProcessorGraph* g)
{
    if (g == nullptr) return;
    juce::MidiBuffer flush;
    for (int ch = 1; ch <= 16; ++ch) {
        flush.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
        flush.addEvent(juce::MidiMessage::allSoundOff(ch), 0);
        flush.addEvent(juce::MidiMessage::allControllersOff(ch), 0);
    }
    juce::AudioBuffer<float> silence(g->getTotalNumOutputChannels(), g->getBlockSize());
    silence.clear();
    g->processBlock(silence, flush);
}
```

### A.2 Plugin-crash isolation and NaN/Inf guard

Windows SEH shim (mandatory: `__try`/`__except` cannot coexist with C++ RAII in the same function):

```cpp
// platform/seh_call.h — Windows only, compiled as C++ but no RAII in __try body
#if JUCE_WINDOWS
#include <windows.h>

enum class SehResult { Ok, StackOverflow, OtherSeh };

inline int sehFilter(unsigned code, EXCEPTION_POINTERS*) noexcept {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
        return EXCEPTION_EXECUTE_HANDLER;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

inline SehResult callProcessBlockSeh(
    void (*fn)(void*) noexcept, void* ctx) noexcept
{
    __try {
        fn(ctx);
        return SehResult::Ok;
    } __except (sehFilter(GetExceptionCode(), GetExceptionInformation())) {
        if (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW) {
            _resetstkoflw();                              // required or next overflow kills us
            return SehResult::StackOverflow;
        }
        return SehResult::OtherSeh;
    }
}
#endif
```

`BypassableInstance::processBlock` wraps the hosted plugin:

```cpp
void BypassableInstance::processBlock(juce::AudioBuffer<float>& buf,
                                      juce::MidiBuffer& midi) noexcept
{
    if (dead_.load(std::memory_order_relaxed)) { buf.clear(); midi.clear(); return; }
    const auto startTicks = juce::Time::getHighResolutionTicks();

#if JUCE_WINDOWS
    struct Args { juce::AudioPluginInstance* p; juce::AudioBuffer<float>* b; juce::MidiBuffer* m; };
    Args a{ inner_.get(), &buf, &midi };
    auto r = callProcessBlockSeh([](void* v) noexcept {
        auto* a = static_cast<Args*>(v);
        a->p->processBlock(*a->b, *a->m);
    }, &a);
    if (r != SehResult::Ok) {
        markDead(r);
        buf.clear(); midi.clear();
    }
#else
    inner_->processBlock(buf, midi);    // POSIX signal handler path optional, off by default on macOS
#endif

    // Post-hoc watchdog: 3 consecutive block-budget overruns → mark dead.
    const auto elapsedNs = juce::Time::highResolutionTicksToSeconds(
        juce::Time::getHighResolutionTicks() - startTicks) * 1e9;
    const double budgetNs = (double) buf.getNumSamples() / sampleRate_ * 0.75 * 1e9;
    if (elapsedNs > budgetNs) {
        if (++overruns_ >= 3) markDead(SehResult::Ok);
    } else { overruns_ = 0; }

    // CPU metering for AC-NEW-6a
    cpuNsAccumulator_.fetch_add(static_cast<uint64_t>(elapsedNs),
                                std::memory_order_relaxed);
    cpuBlockCount_.fetch_add(1, std::memory_order_relaxed);
}

void BypassableInstance::markDead(SehResult r) noexcept {
    dead_.store(true, std::memory_order_release);
    deathEventFifo_.pushNonBlocking(DeathEvent{ nodeId_, r,
        juce::Time::getHighResolutionTicks() });   // lock-free SPSC, preallocated
}
```

NaN/Inf guard at the master output bus (scalar fallback + SSE2 vectorization):

```cpp
// engine/processors/OutputSanitiser.cpp
void OutputSanitiser::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    const juce::ScopedNoDenormals nodn;
    bool sawBad = false;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float* d = buffer.getWritePointer(ch);
#if defined(__SSE2__) || defined(_M_X64)
        sawBad |= sanitizeSSE(d, buffer.getNumSamples());
#else
        sawBad |= sanitizeScalar(d, buffer.getNumSamples());
#endif
    }
    if (sawBad) badSampleFlag_.store(true, std::memory_order_release);
}

static bool sanitizeScalar(float* d, int n) {
    bool hit = false;
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(d[i]) || std::fabs(d[i]) > 1.5f) { d[i] = 0.0f; hit = true; }
    }
    return hit;
}

#if defined(__SSE2__) || defined(_M_X64)
static bool sanitizeSSE(float* d, int n) {
    const __m128 hi = _mm_set1_ps( 1.5f), lo = _mm_set1_ps(-1.5f);
    bool hit = false;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        __m128 v = _mm_loadu_ps(d + i);
        __m128 ord = _mm_cmpord_ps(v, v);                  // 1s where not NaN
        __m128 inR = _mm_and_ps(_mm_cmple_ps(v, hi), _mm_cmpge_ps(v, lo));
        __m128 keep = _mm_and_ps(ord, inR);
        _mm_storeu_ps(d + i, _mm_and_ps(keep, v));          // 0 bad lanes
        if (_mm_movemask_ps(keep) != 0xF) hit = true;
    }
    for (; i < n; ++i) {
        if (!std::isfinite(d[i]) || std::fabs(d[i]) > 1.5f) { d[i] = 0.0f; hit = true; }
    }
    return hit;
}
#endif
```

### A.3 Lock-free RT lane inventory

| Lane | Producer → Consumer | Primitive | Sizing | Policy |
|---|---|---|---|---|
| MIDI ingest → learn callback | RT → message | `juce::AbstractFifo<MidiLearnEvent>` | 256 slots | drain at 30 Hz by `MidiLearnController::Timer` |
| MIDI ingest → graph buffer | RT internal (no cross-thread) | `juce::MidiBuffer` | per block | fed to `MidiInputNode` directly |
| Audio-thread CPU % → UI | RT → message | `std::atomic<float>` per `BypassableInstance` | 1 | relaxed both sides; UI polls at 20-30 Hz |
| Patch-swap handoff | worker → RT → message (trash) | `std::atomic<AudioProcessorGraph*>` + `choc::fifo::SingleReaderSingleWriterFIFO<Graph*>` | trash cap 8 | RT `exchange` + `push`, alloc-free; message drains trash & deletes |
| OSC ingest → param write | OSC thread → RT (latest-wins) + `AbstractFifo<OscCmd>` for discrete | per-param `std::atomic<float>` + cmd FIFO 256 | — | parameter writes idempotent; commands discrete |
| App commands (next patch, tap tempo) | RT-ish (MIDI cb) → message | `juce::AbstractFifo<CommandID>` | 1024 slots | direct v2 `MidiAppFifo` port |
| RT log messages | RT → message | `choc::VariableSizeFIFO` | 128 KB ring | deferred formatting on message thread; feeds `EventLogView` |
| RT → UI dirty flags | RT → UI | `choc::DirtyList<NodeID>` | per-node set | coalesces repaint spam to one per Timer tick |
| Dead-node notifications | RT → UI | `juce::AbstractFifo<DeathEvent>` | 64 slots | drained by `EventLogController` |
| Meter values | RT → UI | `std::atomic<float>` per meter | 1 each | relaxed; UI polls at 30 Hz decoupled from canvas |

**Ordering cookbook** (see Preshing, Timur Doumler, `jrade` C++20 SPSC gist):
- Producer reads own `writeIdx`: `relaxed`. Reads consumer's `readIdx`: `acquire`. Publishes new `writeIdx`: `release`.
- Consumer reads own `readIdx`: `relaxed`. Reads producer's `writeIdx`: `acquire`. Publishes new `readIdx`: `release`.
- Never `seq_cst` on audio thread.

**Deallocation avoidance ("basedrop" pattern in C++):** retired objects (old graph, retired plugin instances) pushed onto a raw-pointer trash FIFO; message-thread `Timer` drains and `delete`s. Audio thread never frees.

### A.4 `juce_clap_hosting` status and vendored-fork plan

**Upstream:** `jatinchowdhury18/juce_clap_hosting` — zero commits since 2022-10-06 (`aa8a812`). Author's README says "super-alpha." Known limitations: no GUI on Windows/Linux (stubbed), no MIDI, no transport, mono parameter steps hard-coded, 10 Hz `Timer` is the only main-thread callback dispatch, bus counts hard-coded 2-in/2-out, double-precision `processBlock` is empty-body.

**Community fork:** `areadenialgames/juce_clap_hosting@926ba25` (2026-02-24) adds JUCE 8 compat, MIDI input, transport events, macOS `NSViewComponent` editor embedding. **Windows HWND and Linux X11 embedding still TBD.** MIT licensed.

**If CLAP ships in v3.0** (not the current recommendation — see §14 Ruthless v3.0):
1. Vendor the fork at `app-v3/vendor/juce_clap_hosting` pinned by SHA.
2. Pin CLAP SDK `1.2.7` and `clap-helpers@a61bcdf0ec` via CPM/git submodule.
3. Budget 1-2 weeks to implement `HWNDComponent` editor embedding on Windows and X11 `POSIX_FD_SUPPORT` event loop wiring on Linux.
4. Add `pluginval`-style integration tests against Surge XT CLAP, Vital CLAP, Diva CLAP at pinned versions.
5. Track JUCE major-version upgrades: budget ~40 engineer-hours/year for reconciliation.
6. `PluginHost` facade exposes a narrow interface (`load`, `prepareToPlay`, `processBlock`, `saveState`, `restoreState`, `setParameter`, `openEditor`, `close`) with no CLAP types leaking — polyphonic modulation exposed via capability query if ever needed.

**If deferred to v3.1** (recommended): no vendored module in v3.0; CLAP loading returns "format not supported" in the UI; VST3 of CLAP-first plugins (Surge XT, Vital, Diva) covers 90%+ of the user need in the interim.

### A.5 `AtomicFileWriter` cross-platform durable-write

```cpp
// session/AtomicFileWriter.h
class AtomicFileWriter {
public:
    // Write `data` atomically to `finalPath`. Returns true on durable success.
    static bool write(const juce::File& finalPath,
                      const juce::MemoryBlock& data,
                      juce::String& errorOut) noexcept
    {
        auto tmp = finalPath.getSiblingFile(finalPath.getFileName() + ".tmp");
      #if JUCE_WINDOWS
        HANDLE h = CreateFileW(tmp.getFullPathName().toWideCharPointer(),
                               GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { errorOut = "CreateFileW failed"; return false; }
        DWORD written = 0;
        if (! WriteFile(h, data.getData(), (DWORD) data.getSize(), &written, nullptr)
            || written != data.getSize()) {
            CloseHandle(h); errorOut = "WriteFile failed"; return false;
        }
        if (! FlushFileBuffers(h)) { CloseHandle(h); errorOut = "FlushFileBuffers failed"; return false; }
        CloseHandle(h);
        if (! MoveFileExW(tmp.getFullPathName().toWideCharPointer(),
                          finalPath.getFullPathName().toWideCharPointer(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            errorOut = "MoveFileExW failed"; return false;
        }
        return true;
      #else
        int fd = ::open(tmp.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { errorOut = "open failed"; return false; }
        if (::write(fd, data.getData(), data.getSize()) != (ssize_t) data.getSize()) {
            ::close(fd); errorOut = "write failed"; return false;
        }
        if (::fsync(fd) != 0) { ::close(fd); errorOut = "fsync failed"; return false; }
        ::close(fd);
        if (::rename(tmp.getFullPathName().toRawUTF8(),
                     finalPath.getFullPathName().toRawUTF8()) != 0) {
            errorOut = "rename failed"; return false;
        }
        int dfd = ::open(finalPath.getParentDirectory().getFullPathName().toRawUTF8(),
                         O_RDONLY | O_DIRECTORY);
        if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }     // POSIX dir-fsync
        return true;
      #endif
    }
};
```

All writes to `PdlSerializer`, `Preferences`, `ColourScheme` files, user preset library, and `crashed_plugins.json` go through this.

---

## Appendix B — Remote Linux + Windows dev-VM topology

### B.1 Recommended setup

**Primary: Proxmox Win11 VM on owned hardware.** $0/mo marginal after one-time Windows license (~$140 retail, free with VS subscription, or eval-for-90-days). 4 vCPU / 16 GiB RAM / 80 GiB NVMe. LAN-grade latency from the headless Linux dev server. No cloud dependency.

**Fallback: Azure `Standard_B2s_v2` Windows, deallocated when idle.** 8h/day × 20 days = ~$16/mo. Escalate to `B4s_v2` (~$22/mo) if MSVC feels slow. Azure VS-subscriber $150/mo dev-test credit zeros this line if available.

**Not a fallback: GitHub Codespaces Windows** — does not exist for end users as of April 2026. Historical 2020 private preview was wound down.

### B.2 Agent-allowed workflow

Claude Code on Linux → SSH into Windows VM → permitted without human approval:
- `git pull`, `rsync -az --delete` source in
- `cmake --preset windows-debug`, `cmake --build`, `ctest`
- `pluginval --strictness-level 5 --skip-gui-tests ...`
- `sccache --show-stats`
- VM start/stop (`az vm start` / `az vm deallocate`) to keep costs bounded

**Human-in-the-loop required:** any `signtool` invocation, Azure TS token use, `git tag v*` + push, modifying signing certs or updater EdDSA keys, pushing to QA desktop.

### B.3 Bootstrap script

`scripts/provision/bootstrap-winvm.ps1` (~20 lines): OpenSSH Server enable, winget-install VS 2022 Build Tools + CMake + Ninja + Git + PowerShell 7 + sccache + WebView2 + Surge XT, VB-CABLE install, sccache env vars, set default SSH shell to pwsh. Full script in the research brief.

### B.4 File sync

**rsync over SSH** for the inner loop (Linux → VM for source; VM → Linux for artifacts/crash dumps). **Not** a shared network mount — MSVC's many-small-syncs PDB writes + SMB oplocks produce minute-scale regressions. Syncthing only for bidirectional-edit asset directories.

### B.5 Audio testing on the VM

- Pluginval headless: yes. WASAPI render-path compile/smoke: yes. WASAPI loopback capture for offline golden tests: yes-but-validate.
- ASIO latency measurement: **no** (VM scheduling jitter). ASIO driver-specific bugs: **no**. MIDI from physical controllers: **no** (unless USBIP passthrough, flaky).
- Commercial VSTs: stay on the QA desktop; VM tests against Surge XT, Dexed, Dragonfly, Vital, Valhalla Supermassive, TDR Nova.

---

## Appendix C — GitLab CI concrete skeletons

Excerpted from the GitLab CI research brief. Full skeleton is ~200 lines with secrets placeholders, caching, parallel matrix fan-out, Azure Trusted Signing PowerShell invocation, macOS keychain-create/notarytool/stapler flow, manual production gate, Generic Package Registry upload, Sparkle/WinSparkle appcast regeneration. Three runbooks (cert rotation, runner outage, updater-key loss) included.

Key shape:

```yaml
stages: [fixtures, lint, build, test, pluginval, package, sign, release, publish]

default:
  interruptible: true
  retry: { max: 2, when: [runner_system_failure, stuck_or_timeout_failure, scheduler_failure] }

variables:
  CI_DEBUG_TRACE: "false"                       # AC-SE-19
  CCACHE_DIR: "${CI_PROJECT_DIR}/.ccache"
  SCCACHE_DIR: "${CI_PROJECT_DIR}/.sccache"

build:linux:   { stage: build, tags: [saas-linux-medium-amd64], image: registry.gitlab.com/pedalboard3/ci:linux-latest, ... }
build:windows: { stage: build, tags: [self-hosted-windows], rules: [...allow_failure on branch push, hard-fail on tag...] }
build:macos:   { stage: build, tags: [self-hosted-macos], ... }

sign:windows:  # AzureSignTool with RFC 3161 timestamp
sign:macos:    # security create-keychain + codesign --options runtime + notarytool submit --wait + stapler staple
sign:linux:    # appimagetool --sign + dpkg-sig + rpm --addsign

release:approve:
  stage: release
  when: manual
  environment: { name: production, url: https://pedalboard3.dev/releases/$CI_COMMIT_TAG }
  rules: [{ if: '$CI_COMMIT_TAG =~ /^v\d+\.\d+\.\d+$/' }]

publish:artifacts:
  stage: publish
  needs: [release:approve, sign:windows, sign:macos, sign:linux]
  # curl --upload-file to Generic Package Registry; regenerate Sparkle/WinSparkle appcasts
```

Linux CI Dockerfile (Ubuntu 22.04 base for glibc back-compat): Clang 18, CMake 3.28.6, Ninja 1.12.1, pluginval, appimagetool, linuxdeploy, `rust-minidump`/`minidump-stackwalk`/`dump_syms` for crash symbolication on Linux.

Runner registration: Windows `gitlab-runner install` as Windows service under a named service account (not LocalSystem) with GUI session for audio-plugin tests; macOS `launchctl bootstrap gui/<uid>` under the logged-in runner user (not root LaunchDaemon — loses GUI context).

Runbooks:
- **Azure TS cert rotation:** automatic every ~24h, 3-day cert validity; RFC 3161 timestamps anchor trust at signing instant. Scheduled daily canary-sign pipeline at 02:00 UTC; two consecutive failures page on-call.
- **Windows runner outage:** retry 2× with backoff (AC-CI-2), `allow_failure: true` on branch jobs so unrelated MRs merge. Long outages: spot Windows EC2 with same runner token + `self-hosted-windows` tag as temporary failover.
- **Updater key loss:** two-key rotation (AC-SE-15) means losing `current` drops to `next-rotation` with one degraded release; losing both = manual reinstall. Quarterly restore drill from paper copy.

---

## Appendix D — v2 `.pdl` import attribute mapping (compact)

| v2 element | v2 attribute | v3 element | v3 attribute | Notes |
|---|---|---|---|---|
| `<FILTER>` | `uid` | `<NODE>` | `id` | direct |
| `<FILTER>` | `x`, `y` | `<NODE>` | `x`, `y` | direct (normalized 0-1) |
| `<FILTER>` | `program` | `<NODE>` | `program` | direct (may mismatch if plugin state defaulted) |
| `<FILTER>` | `MIDIChannel` | `<NODE>` | `midiChannel` | direct (0=omni, 1-16) |
| `<FILTER>` | `oscMIDIAddress` | `<OSC_MAPPING>` | `address` | v2 stored on node; v3 moves to proper mapping row |
| `<FILTER>` | — | `<NODE>` | `overridePluginMidi` | **New in v3** (v2 had UI-only toggle, not serialized — bug fix) |
| `<PLUGIN><uid>` (VST2 integer) | — | `<IDENTITY>` | `classId`/`vst2uid` | via `PluginIdentityRemapper` |
| `<STATE>` (base64) | — | `<STATE encoding="base64">` | — | direct; defaulted-with-warning if remapped across format |
| `<CONNECTION srcChannel=0x1000>` | — | `<CONN>` | type="midi" | v2 used magic channel 0x1000 for MIDI |
| `<MidiMapping>` | `pluginId`, `parameter`, `cc`, `latch`, `channel`, `lowerBound`, `upperBound` | `<MIDI>` | `nodeId`, `paramIndex`, `cc`, `latch`, `midiChannel`, `rangeLo`, `rangeHi` | `inverted` inferred from `lo > hi` |
| `<OscMapping>` | `pluginId`, `parameter`, `address`, `parameterIndex` | `<OSC>` | `nodeId`, `paramIndex`, `address`, `paramIndex` | `outputEcho` defaults true for OSC |
| `<UserNames><Name>` | `id`, `val` | `<NODE customName="...">` | `id`, `customName` | direct |

**19 colour scheme tokens** (v2 `.colourscheme` → v3 named):
`Window background` → `bg.window`, `Plugin field background` → `bg.graph`, `Text colour` → `text.primary`, `Plugin border` → `border.plugin`, `Plugin background` → `bg.plugin`, `Audio connection` → `connection.audio`, `Parameter connection` → `connection.param`, `Button colour` → `button.default`, `Button highlight` → `button.hover`, `Text editor` → `bg.editor`, `Dialog inner background` → `bg.dialog`, `CPU meter` → `meter.cpu`, `Toggle/slider` → `slider.thumb`, `List selected` → `highlight.selection`, `VU lower/upper/over` → `meter.vu.{low,mid,hot}`, `Vector drawings` → `icon.primary`, `Menu selection` → `highlight.menu`.

**v2 app commands**: `patch_next`, `patch_prev`, `tap_tempo`, `transport_play_pause`, `transport_stop`, `record_toggle` (command ID numeric values from `source code/src/App.cpp:244-310`, hard-coded remap table).

**Edge cases to test:** VST2 UID collision with VST3 Class ID numeric value (reject numeric-only match); FilePlayer missing-file path (placeholder with "locate" prompt); state blob >5 MB (warn) / >100 MB (refuse); cross-platform path encoding (Windows backslashes → forward slashes on load).

---

## Sources & References

### Internal references

- **`source code/`** — Pedalboard2 v2 codebase (read-only behavioral reference).
- **`source code/src/FilterGraph.cpp`** — v2 `.pdl` XML schema reference for import logic.
- **`source code/src/MainPanel.cpp`** — patch-switch UI glue.
- **`source code/ChangeLog.txt`** — prior-art bug history (threading, MIDI, patch-switch deadlocks — must-fix regression list).
- **`source code/ToDo.txt`** — v2's unfinished work list (MIDI out, serial input, home-key bug, AudioRecorder MIDI-trigger bug).
- **`source code/ColourSchemeColours.txt`** — 19 colour tokens to preserve.
- **`documentation/*.html`** — behavioral specification for all v2 features.

### External — frameworks & SDKs

- [JUCE 8 — GitHub](https://github.com/juce-framework/JUCE)
- [JUCE 8 licence (AGPLv3 / commercial)](https://juce.com/legal/juce-8-licence/)
- [JUCE AudioPluginHost sample (graph-based host reference)](https://github.com/juce-framework/JUCE/tree/master/extras/AudioPluginHost)
- [JUCE VST3PluginFormat class reference](https://docs.juce.com/master/classVST3PluginFormat.html)
- [JUCE AudioProcessorGraph tutorial](https://docs.juce.com/master/tutorial_audio_processor_graph.html)
- [JUCE OSCReceiver / OSCSender](https://docs.juce.com/master/classOSCReceiver.html)
- [juce_clap_hosting (Jatin Chowdhury)](https://github.com/jatinchowdhury18/juce_clap_hosting)
- [clap-juce-extensions (free-audio)](https://github.com/free-audio/clap-juce-extensions)
- [CLAP plugin format](https://github.com/free-audio/clap)
- [Steinberg VST3 MIT relicensing (Oct 2025)](https://www.steinberg.net/press/2025/vst-3-8/)
- [Steinberg ASIO GPLv3 dual-licensing (Oct 2025) — Libre Arts](https://librearts.org/2025/11/steinberg-relicenses-vst3-and-asio/)
- [Tracktion Engine (higher-level JUCE graph engine, held in reserve)](https://github.com/Tracktion/tracktion_engine)

### External — CI / signing / distribution

- [GitLab hosted runners](https://docs.gitlab.com/ci/runners/hosted_runners/)
- [GitLab hosted Windows runners (still Beta as of April 2026)](https://docs.gitlab.com/ci/runners/hosted_runners/windows/)
- [GitLab hosted macOS runners](https://docs.gitlab.com/ci/runners/hosted_runners/macos/)
- [Azure Trusted Signing / Artifact Signing](https://learn.microsoft.com/en-us/azure/artifact-signing/how-to-signing-integrations)
- [Azure Trusted Signing on GitLab runner (walkthrough)](https://lets-talk-data.net/blog/trusted-signing-gitlab/)
- [SignPath build-system integration](https://about.signpath.io/documentation/build-system-integration)
- [SignPath Foundation (free tier for OSS)](https://signpath.org/)
- [DigiCert HSM private-key requirement (2023)](https://knowledge.digicert.com/general-information/new-private-key-storage-requirement-for-standard-code-signing-certificates-november-2022)
- [Apple notarytool man page](https://keith.github.io/xcode-man-pages/notarytool.1.html)
- [Apple notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)
- [Peter Steinberger — Code signing & notarization, Sparkle (2025)](https://steipete.me/posts/2025/code-signing-and-notarization-sparkle-and-tears)
- [Sparkle documentation](https://sparkle-project.org/documentation/)
- [WinSparkle on Windows](https://winsparkle.org/)
- [AppImage signing docs](https://docs.appimage.org/packaging-guide/optional/signatures.html)
- [WiX v5 / FireGiant docs](https://docs.firegiant.com/)
- [GitLab Release + Generic Package Registry pattern](https://docs.gitlab.com/user/packages/generic_packages/)

### External — testing & dev workflow

- [Pluginval (Tracktion)](https://github.com/Tracktion/pluginval)
- [Pamplejuce — JUCE 8 GitHub Actions template (adapt to GitLab)](https://github.com/sudara/pamplejuce)
- [Melatonin — CMake + JUCE](https://melatonin.dev/blog/how-to-use-cmake-with-juce/)
- [Melatonin — Windows code signing with Azure Trusted Signing](https://melatonin.dev/blog/code-signing-on-windows-with-azure-trusted-signing/)
- [xwin — cross-compile Windows from Linux (sanity-check only, not release)](https://jake-shadle.github.io/xwin/)
- [yabridge — Windows VST plugins on Linux via Wine (dev-convenience only)](https://github.com/robbert-vdh/yabridge)
- [rust-minidump (symbolicate Windows crash dumps on Linux)](https://github.com/rust-minidump/minidump-writer)
- [Surge XT (open-source test VST)](https://github.com/surge-synthesizer/surge)
- [Dexed (DX7 clone test VST)](https://github.com/asb2m10/dexed)
- [Dragonfly Reverb (test VST)](https://github.com/michaelwillis/dragonfly-reverb)

### External — research brief sources (deepen pass)

- [JUCE forum — AudioProcessorGraph thread safety](https://forum.juce.com/t/audioprocessorgraph-thread-safety/24419)
- [JUCE forum — Loading AudioProcessorGraph state from a background thread](https://forum.juce.com/t/loading-audioprocessorgraph-state-from-a-background-thread/16451)
- [JUCE forum — AudioProcessorGraph PDC and changing latency](https://forum.juce.com/t/audioprocessorgraph-pdc-and-changing-latency/18121)
- [JUCE forum — KnownPluginList isn't thread safe](https://forum.juce.com/t/knownpluginlist-isnt-thread-safe/33370)
- [JUCE forum — Bypassing audio processors in an audiograph](https://forum.juce.com/t/bypassing-audio-processors-in-an-audiograph/66520)
- [JUCE forum — State of the Art Denormal Prevention](https://forum.juce.com/t/state-of-the-art-denormal-prevention/16802)
- [Tracktion choc (header-only, ISC)](https://github.com/Tracktion/choc)
- [Timur Doumler — Using locks in real-time audio processing, safely](https://timur.audio/using-locks-in-real-time-audio-processing-safely)
- [Jeff Preshing — Acquire and Release Semantics](https://preshing.com/20120913/acquire-and-release-semantics/)
- [Clang RealtimeSanitizer docs](https://clang.llvm.org/docs/RealtimeSanitizer.html)
- [Raymond Chen — Zero-cost exceptions aren't actually zero cost](https://devblogs.microsoft.com/oldnewthing/20220228-00/?p=106296)
- [SEI CERT SIG30-C — Async-signal-safe functions](https://wiki.sei.cmu.edu/confluence/display/c/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers)
- [Bitwig — Plug-in Hosting & Crash Protection](https://www.bitwig.com/learnings/plug-in-hosting-crash-protection-in-bitwig-studio-20/)
- [yabridge — architecture.md](https://github.com/robbert-vdh/yabridge/blob/master/docs/architecture.md)
- [mozilla/sccache Azure Blob backend](https://github.com/mozilla/sccache/blob/main/docs/Azure.md)
- [Microsoft — Trusted Signing / AzureSignTool v5](https://github.com/vcsjones/AzureSignTool)
- [Peter Steinberger — Code signing & notarization with Sparkle (2025)](https://steipete.me/posts/2025/code-signing-and-notarization-sparkle-and-tears)
- [Sparkle EdDSA migration](https://sparkle-project.org/documentation/eddsa-migration/)
- [Pillai et al., All File Systems Are Not Created Equal, OSDI 2014](https://www.usenix.org/system/files/conference/osdi14/osdi14-paper-pillai.pdf)
- [Azure B-series Windows VM pricing (Vantage)](https://instances.vantage.sh/azure/vm/b2s-v2)
- [Proxmox + Win11 development sandbox guide](https://windowsforum.com/threads/windows-11-vm-on-proxmox-practical-home-lab-development-sandbox.383662/)

### Related original project

- [Niall Moody — Pedalboard2 project page](https://www.niallmoody.com/work/pedalboard2/)
- [Pedalboard2 GPLv3 source mirror (lrq3000)](https://github.com/lrq3000/Pedalboard2)
