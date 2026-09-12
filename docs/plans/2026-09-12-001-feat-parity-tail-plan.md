---
title: M6 — Parity tail: transport + processors
type: feat
date: 2026-09-12
topic: parity-tail
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: roadmap R15 / R16 (docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md), master plan AC-TR-* / AC-FP-* (docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md), and the user's decisions of 2026-09-12 (Key Decisions)
execution: code
---

# M6 — Parity tail: transport + processors - Plan

## Goal Capsule

- **Objective:** Give Pedalboard 3 the transport and the built-in processors Pedalboard2 had:
  - a transport with play, stop, Return to Zero, tempo, time signature and tap tempo, visible to plugins as a running play head
  - File Player, Audio Recorder, Looper, Metronome and Output Toggle, each addable like Level and VU Meter
- **Product authority:** Pedalboard2's behaviour (`source code/`, read-only), except where it was buggy or where the user decided otherwise (Key Decisions).
- **Tracking:** milestone "M6 — Parity tail: transport + processors".
  - #35 (transport) closes with U4, the last transport unit.
  - #36 (processors) closes with the last processor unit.
  - #37 (the VU Meter on its card) already closed in !69.
  - MIDI Output (U11) is deferred to "1.x — Deferred" and filed there.
- **Out of scope:**
  - MIDI Output (user decision, U11)
  - a Router with more than two outputs (Pedalboard2's to-do list)
  - timeline, arrangement or tempo automation: the transport is one tempo per patch
  - MIDI clock in or out
- **Stop conditions:** stop and surface if any unit needs the audio thread to allocate, lock or touch the disk, or if a processor's state can't round-trip through the existing `<NODE>` state blob.

---

## Key Decisions

- **KD1. The transport is a running play head** (user decision, 2026-09-12).
  - While playing, plugins see a position that advances in samples, seconds and beats (PPQ), with the bar start and the patch's time signature. Stopped, the position holds.
  - Return to Zero sets it to 0 at the next block boundary.
  - Pedalboard2 only reported play/stop and tempo, with the position fixed at 0 in 4/4; tempo-synced plugins couldn't lock to the bar.
- **KD2. Tempo and time signature belong to the patch**, as Pedalboard2's tempo did (`<Patch tempo>`).
  - Defaults are 120 BPM and 4/4. Switching patch applies its tempo and time signature.
  - `.pdl` schemaMinor 6 adds the attributes, so older files load at the defaults.
  - The Pedalboard2 importer reads `tempo`.
- **KD3. One tap-tempo rule everywhere (AC-TR-2).** The tempo is the average of the last 4 taps' gaps. A gap over 2 s starts again. The result is clamped to 20–300 BPM and shown to 0.1 BPM. Keys, MIDI, OSC and the Tap button all share it; Pedalboard2 had three different rules.
- **KD4. Transport actions are app commands.** Play/Pause (Space), Stop, Return to Zero (Home) and Tap Tempo join `session::AppCommands`, so keys, MIDI and OSC drive them and App Mappings can rebind them.
- **KD5. MIDI Machine Control is optional and literal.**
  - A MIDI Settings switch, off by default, lets MMC drive the transport: Play and Deferred Play play, Stop stops, Pause toggles, Rewind returns to zero.
  - Pedalboard2 mapped Stop and Play to the same toggle, so a Stop could start the transport.
- **KD6. Recordings are auto-named takes** (user decision, 2026-09-12).
  - Each Audio Recorder take, and each recorded Looper loop, is a new 24-bit WAV in `Documents/Pedalboard 3/Recordings`, named `<patch> <yyyy-mm-dd HH-mm-ss>.wav`. Nothing is overwritten.
  - The folder can be changed per recorder and is saved with the node.
  - Pedalboard2 needed a file picked first, overwrote it on every take, and forgot the path after stopping.
- **KD7. Processor controls sit on the card** (user decision, 2026-09-12).
  - File Player, Audio Recorder, Looper and Metronome cards get a row of buttons (Play, Record, Return to Zero as they apply) and a small waveform, time or beat readout.
  - ED opens a window with everything else: file, sync, bar length and levels.
  - Output Toggle's card is one large 1 / 2 switch.
  - Every control is also a parameter, so it's mappable to MIDI and OSC.
- **KD8. The audio thread never allocates, locks or touches the disk.**
  - File reading and writing, and Looper memory, run on one shared disk thread (`juce::TimeSliceThread`) owned by the engine.
  - A parameter that has to open or create a file (Record, a new file) hands off to the message thread (`juce::AsyncUpdater`), as Pedalboard2's Looper eventually did and its Recorder never did.
  - Cross-thread flags are atomics.
- **KD9. "Sync to transport" follows the running position.**
  - A synced File Player or Looper plays from the transport's position, and a synced Metronome clicks on the transport's beats. That makes AC-TR-1 (no drift over repeated play/stop) true by construction.
  - A synced Audio Recorder starts a take on play and ends it on stop (AC-TR-4).
  - Sync is off by default, as in Pedalboard2.
- **KD10. Output Toggle keeps Pedalboard2's shape:** a mono input switched between output 1 and output 2, with a 20 ms crossfade. It fits the cards' two output ports.
- **KD11. Processor parameters use real ranges.** Bar numerator 1–16, bar denominator 1, 2, 4, 8 or 16, and levels in dB. Pedalboard2's raw-integer parameters broke when mapped to MIDI.

---

## Implementation Units

### U1. Transport engine and play head

- **Goal:** An engine transport that hosted plugins read as their play head:
  - playing flag
  - position in samples
  - tempo and time signature
  - Return to Zero at a block boundary
- **Requirements:** KD1; AC-TR-1
- **Dependencies:** none
- **Files:**
  - `app-v3/src/engine/Transport.h`, `app-v3/src/engine/Transport.cpp` (create): atomics set from the message thread, advanced by the audio thread; implements `juce::AudioPlayHead::getPosition`
  - `app-v3/src/engine/TransportMath.h` (create): pure samples → seconds / PPQ / bar-start maths
  - `app-v3/src/engine/AudioGraphRuntime.h`, `app-v3/src/engine/AudioGraphRuntime.cpp`: set the play head on the live graph each block and advance the transport after it
  - `app-v3/src/engine/BypassableInstance.h`, `app-v3/src/engine/BypassableInstance.cpp`: forward the play head to the wrapped plugin (it doesn't today)
  - `app-v3/src/engine/AudioEngine.h`, `app-v3/src/engine/AudioEngine.cpp`: own the transport
  - `app-v3/tests/unit/engine/Transport_test.cpp`, `app-v3/tests/unit/engine/TransportMath_test.cpp` (create)
- **Approach:**
  - JUCE's graph already hands its play head to each node every block. The runtime sets it on the live graph before processing, and `BypassableInstance` passes it through to the plugin it wraps.
  - The position advances by the block length only while playing. A Return to Zero request is a flag the audio thread consumes at the top of the next block.
- **Test scenarios:**
  - the position advances only while playing
  - PPQ and bar start at 120 and 90 BPM, in 4/4 and 7/8
  - Return to Zero lands at a block boundary
  - a hosted test processor sees the play head through `BypassableInstance`
- **Verification:** ctest green. By hand, a tempo-synced delay plugin follows the transport.

### U2. Tempo and time signature per patch

- **Goal:** Each patch carries a tempo and time signature; switching patch applies them; `.pdl` files save them; Pedalboard2 imports bring their tempo.
- **Requirements:** KD2
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/session/Patch.h`, `app-v3/src/session/Patch.cpp`: `tempo`, `timeSigNumerator`, `timeSigDenominator` with defaults, written on `<PATCH>`
  - `app-v3/src/session/PdlSerializer.h`: schemaMinor 6
  - `app-v3/src/session/v2_import/PdlV2Importer.cpp`: read `tempo`
  - `app-v3/src/session/BankDocument.h`, `app-v3/src/session/BankDocument.cpp`: apply the active patch's tempo and time signature to the transport on switch and load; set and mark modified on edit
  - `app-v3/tests/unit/session/Patch_test.cpp`, `app-v3/tests/unit/session/PdlSerializer_test.cpp`, `app-v3/tests/unit/session/PdlV2Importer_test.cpp`, `app-v3/tests/unit/session/BankDocument_test.cpp`
- **Test scenarios:**
  - tempo and time signature round-trip
  - a schemaMinor 5 file loads at 120 and 4/4
  - a Pedalboard2 file's tempo imports
  - switching patch applies its tempo
  - a tempo edit marks the board modified
- **Verification:** ctest green.

### U3. Tap tempo, transport commands and MMC

- **Goal:** Play/Pause, Stop, Return to Zero and Tap Tempo as app commands, one tap-tempo rule, and optional MMC.
- **Requirements:** KD3, KD4, KD5; AC-TR-2
- **Dependencies:** U1, U2
- **Files:**
  - `app-v3/src/session/TapTempo.h` (create): pure; the last four taps, a 2 s reset, clamping
  - `app-v3/src/session/AppCommands.h`, `app-v3/src/session/AppCommands.cpp`: the four commands, and a transport target for `invokeAppCommand`
  - `app-v3/src/session/Mmc.h` (create): pure MMC message → transport command
  - `app-v3/src/session/ControlHub.*` or the MIDI input path: route MMC when enabled
  - `app-v3/src/session/Preferences.*`: the MMC switch
  - `app-v3/src/ui/SettingsWindow.cpp`: the switch in MIDI Settings
  - `app-v3/src/ui/MainWindow.cpp`: default keys Space and Home
  - `app-v3/tests/unit/session/TapTempo_test.cpp`, `app-v3/tests/unit/session/Mmc_test.cpp` (create); `app-v3/tests/unit/session/AppCommands_test.cpp`
- **Test scenarios:**
  - four taps at 500 ms give 120 BPM
  - a 2.1 s gap restarts the average
  - the tempo clamps at 20 and 300
  - each command drives the transport
  - MMC Play, Stop, Pause and Rewind map as KD5 says; other MMC is ignored
  - MMC does nothing while the switch is off
- **Verification:** ctest green. By hand, Space plays and pauses, and a MIDI-mapped tap sets the tempo.

### U4. Transport controls in the top bar

- **Goal:** The top bar gets:
  - Play/Pause and Return to Zero buttons
  - an editable tempo with a Tap button
  - the time signature
  - a bars.beats position readout
- **Requirements:** KD1, KD3; #35
- **Dependencies:** U3
- **Files:**
  - `app-v3/src/ui/TransportControls.h`, `app-v3/src/ui/TransportControls.cpp` (create)
  - `app-v3/src/ui/TransportBar.h`, `app-v3/src/ui/TransportBar.cpp`: place it between the patch strip and the lights
  - `app-v3/src/ui/Pb3LookAndFeel.*` if the buttons need icons
- **Approach:**
  - The readout polls the transport at 30 Hz, as the CPU meter does.
  - The tempo field takes typing, and dragging up and down. The time signature opens a small editor.
  - Under about 1100 px the time signature and readout fold away; the patch name still gives way first.
- **Test scenarios:** pure formatting of bars.beats (in `TransportMath.h`) is unit-tested. The rest is by hand.
- **Verification:** by hand at 1280 and 960 px wide: play, Return to Zero, type and tap a tempo, change the time signature.

### U5. Processor groundwork: disk thread, card controls, taller cards

- **Goal:** The shared pieces the processors need:
  - the engine's disk thread and audio format manager
  - a base class with sync-to-transport, parameters and the message-thread hand-off
  - a card area for processor controls, and cards that can be taller
- **Requirements:** KD7, KD8, KD11
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/engine/DiskThread.h` (create): one shared `juce::TimeSliceThread` plus the registered audio formats
  - `app-v3/src/engine/processors/ProcessorBase.h`, `app-v3/src/engine/processors/ProcessorBase.cpp` (create): a parameter helper, the sync flag, state as XML inside the node blob, and an `AsyncUpdater` hand-off
  - `app-v3/src/ui/ProcessorControls.h`, `app-v3/src/ui/ProcessorControls.cpp` (create): the card's control row, one layout per built-in type
  - `app-v3/src/ui/PluginCard.*`: host the control row and report its height
  - `app-v3/src/ui/CanvasLayout.h`: per-card heights
  - `app-v3/src/ui/SignalCanvas.cpp`: poll processor state on the existing 30 Hz timer
  - `app-v3/tests/unit/ui/CanvasLayout_test.cpp`
- **Approach:** a processor card is 170 × 124. The extra 32 px holds the control row. `CanvasLayout` places cards by their own heights and still centres the group.
- **Test scenarios:** mixed card heights centre and keep their ports aligned.
- **Verification:** ctest green.

### U6. File Player

- **Goal:**
  - plays a WAV, AIFF, FLAC or Ogg file, stereo out
  - Play/Pause, Return to Zero, Loop and Sync
  - a hidden Trigger parameter (AC-FP-2)
  - a waveform on the card, where a click seeks
  - dropping an audio file on the window adds a File Player with it (AC-FP-6)
- **Requirements:** KD7, KD8, KD9; AC-TR-1, AC-FP-2, AC-FP-6; #36
- **Dependencies:** U5
- **Files:**
  - `app-v3/src/engine/processors/FilePlayerProcessor.h`, `app-v3/src/engine/processors/FilePlayerProcessor.cpp` (create)
  - `app-v3/src/engine/PluginHost.cpp`: register "FilePlayerProcessor"
  - `app-v3/src/ui/ProcessorControls.cpp`
  - `app-v3/src/ui/FilePlayerWindow.*` (create): the ED window
  - `app-v3/src/ui/MainWindow.cpp`: an audio-file drop adds a File Player at the drop point
  - `app-v3/src/session/BoardFiles.h`: which dropped files are audio
  - `app-v3/tests/unit/engine/FilePlayerProcessor_test.cpp` (create)
- **Approach:**
  - Reading is buffered ahead on the disk thread.
  - Synced, the read position follows the transport's position.
  - At the end of a file it stops, or wraps when Loop is on. Unlike Pedalboard2, it never stops the transport.
  - State is the file path, Loop and Sync.
- **Test scenarios:**
  - plays a generated file sample-exact
  - Return to Zero
  - loops
  - 1000 synced play/stop cycles stay within 1 sample of the transport (AC-TR-1)
  - Trigger restarts
  - state round-trips
- **Verification:** ctest green. By hand, drop a WAV on the window, and play it synced and unsynced.

### U7. Audio Recorder

- **Goal:** Records its stereo input to auto-named takes:
  - Record button with elapsed time and a level on the card
  - Sync starts a take on play and ends it on stop
  - the folder can be changed
- **Requirements:** KD6, KD8, KD9; AC-TR-4, AC-FP-3; #36
- **Dependencies:** U5
- **Files:**
  - `app-v3/src/engine/processors/AudioRecorderProcessor.h`, `app-v3/src/engine/processors/AudioRecorderProcessor.cpp` (create)
  - `app-v3/src/engine/Recordings.h` (create): pure take naming and the default folder
  - `app-v3/src/engine/PluginHost.cpp`
  - `app-v3/src/ui/ProcessorControls.cpp`
  - `app-v3/src/ui/RecorderWindow.*` (create)
  - `app-v3/tests/unit/engine/Recordings_test.cpp`, `app-v3/tests/unit/engine/AudioRecorderProcessor_test.cpp` (create)
- **Approach:**
  - A take opens on the message thread and writes through `juce::AudioFormatWriter::ThreadedWriter` on the disk thread.
  - Stopping flushes and closes the take. The written file is never reopened or truncated (AC-FP-3).
  - A name collision adds a counter.
- **Test scenarios:**
  - take names from the patch and time, with a counter on collision
  - a synced recorder starts on every transport play (AC-TR-4), including when play comes from a MIDI-mapped command
  - the written WAV matches the input
  - stop closes the file
  - the folder round-trips
- **Verification:** ctest green. By hand, record three takes, synced and unsynced, and find three files.

### U8. Looper

- **Goal:** Record a loop, then play it back seamlessly:
  - Play/Pause, Return to Zero and Record on the card, with a waveform
  - Stop After Bar, Auto-play, bar length, input and loop levels, and Sync in the ED window
  - each recorded loop also saved as a take, and reloaded with the patch
- **Requirements:** KD6, KD7, KD8, KD9, KD11; #36
- **Dependencies:** U5, U7 (takes)
- **Files:**
  - `app-v3/src/engine/processors/LooperProcessor.h`, `app-v3/src/engine/processors/LooperProcessor.cpp` (create)
  - `app-v3/src/engine/processors/LoopBuffer.h` (create): chunked memory the disk thread grows ahead of the recorder, capped at 10 minutes
  - `app-v3/src/engine/PluginHost.cpp`
  - `app-v3/src/ui/ProcessorControls.cpp`
  - `app-v3/src/ui/LooperWindow.*` (create)
  - `app-v3/tests/unit/engine/LoopBuffer_test.cpp`, `app-v3/tests/unit/engine/LooperProcessor_test.cpp` (create)
- **Approach:**
  - Keep Pedalboard2's 128-sample crossfade at the loop point.
  - Stop After Bar counts at the transport tempo and the Looper's own bar length.
  - Fix Pedalboard2's bugs:
    - Return to Zero paused a playing synced loop
    - Read Position 0 read a null chunk
    - changing the file while playing could hang
    - the load and construct defaults disagreed
- **Test scenarios:**
  - record then play is sample-exact
  - a seamless loop point, with no step at the crossfade
  - Stop After Bar stops on the bar
  - running out of the buffer stops recording cleanly
  - state and the loop take round-trip
- **Verification:** ctest green. By hand, loop a guitar phrase synced to the transport.

### U9. Metronome

- **Goal:**
  - an accented click on beat 1 and clicks on the other beats, at the transport or its own tempo
  - numerator and denominator
  - optional accent and click sample files
  - Play and a beat readout on the card
- **Requirements:** KD9, KD11; AC-TR-3; #36
- **Dependencies:** U5
- **Files:**
  - `app-v3/src/engine/processors/MetronomeProcessor.h`, `app-v3/src/engine/processors/MetronomeProcessor.cpp` (create)
  - `app-v3/src/engine/PluginHost.cpp`
  - `app-v3/src/ui/ProcessorControls.cpp`
  - `app-v3/src/ui/MetronomeWindow.*` (create)
  - `app-v3/tests/unit/engine/MetronomeProcessor_test.cpp` (create)
- **Approach:**
  - The synthesized click is computed from the sample rate. Pedalboard2 assumed 44.1 kHz, so its pitch was wrong elsewhere.
  - Sample files load and resample on the disk thread.
  - Synced, clicks land on the transport's beats. Turning sync on or off fades over 5 ms (AC-TR-3).
  - Stereo out, the same click on both channels.
- **Test scenarios:**
  - clicks land on the exact beat samples at 120 BPM 4/4 and 90 BPM 7/8
  - the accent falls on beat 1
  - no discontinuity when toggling sync
  - correct pitch at 44.1 and 96 kHz
- **Verification:** ctest green. By hand, a synced metronome stays on the beat through tempo changes.

### U10. Output Toggle

- **Goal:** A mono input sent to output 1 or output 2, with a 20 ms crossfade. The card is one 1 / 2 switch, and Output is a mappable parameter.
- **Requirements:** KD7, KD10; #36
- **Dependencies:** U5
- **Files:**
  - `app-v3/src/engine/processors/OutputToggleProcessor.h`, `app-v3/src/engine/processors/OutputToggleProcessor.cpp` (create)
  - `app-v3/src/engine/PluginHost.cpp`
  - `app-v3/src/ui/ProcessorControls.cpp`
  - `app-v3/tests/unit/engine/OutputToggleProcessor_test.cpp` (create)
- **Test scenarios:**
  - output 1 by default
  - a switch crossfades over 20 ms at 44.1 and 48 kHz
  - the parameter maps; state round-trips
- **Verification:** ctest green. By hand, switch between two amp chains from a footswitch mapping.

### U11. MIDI Output (deferred)

- **Goal:** A node that sends the MIDI wired into it to a chosen MIDI output device.
- **Status:** deferred to 1.x (user decision, 2026-09-12). Pedalboard2 never had one; it was only on its to-do list. It's filed on "1.x — Deferred" so it isn't lost.

---

## Verification Contract

- Every unit keeps `build.ps1 test` green on Windows and the Linux and macOS CI jobs green.
- **Engine units** (U1, U6–U10) add headless tests that drive `processBlock` with generated audio and a test transport. No test needs an audio device.
- **Nothing on the audio thread allocates, locks or does I/O.** Each processor MR says how it meets this.
- **By hand, on Windows,** each MR lists its checks. The macOS pass comes with the desktop pass.

## Risks

- **Taller processor cards (U5).** `CanvasLayout` assumes one card height today. Per-card heights change centring and port anchors, so U5 carries the geometry tests.
- **The Looper's memory (U8).** Growing buffers must stay ahead of the recorder without the audio thread allocating. The disk thread allocates the next chunk well before it's needed, and recording stops cleanly at the cap or if allocation falls behind.
- **Disk throughput (U6, U7, U8).** Several players and recorders share one disk thread. Read-ahead and writer buffers are sized for four streams at 96 kHz.
- **Space as Play/Pause (U3).** Space must not fire while a text field has focus. JUCE gives the focused editor the key first, and the MR checks the tempo field and the rename editor.

## MR plan

Each MR is stacked on the one before while it's unmerged:
1. U1 and U2: the transport engine and per-patch tempo, plus this plan
2. U3 and U4: commands, tap tempo, MMC and the top-bar controls. This closes #35.
3. U5 and U10: groundwork, proved on the simplest processor
4. U6: File Player
5. U7: Audio Recorder
6. U9: Metronome
7. U8: Looper. This closes #36.
