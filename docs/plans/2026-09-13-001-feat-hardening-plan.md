---
title: M5 — Hardening: crash guard + .pdl
type: feat
date: 2026-09-13
topic: hardening
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: roadmap R18 / R19 (docs/plans/2026-07-02-001-feat-road-to-1-0-release-plan.md), master plan AC-PL-1, AC-FS-6/7/9/11 and AC-SE-5..8/24/30 (docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md), issue #5, and the user's decisions of 2026-09-13 (Key Decisions)
execution: code
---

# M5 — Hardening: crash guard + .pdl - Plan

## Goal Capsule

- **Objective:** Make the core safe to put in front of beta users (the public beta is M7, right after this milestone):
  - a plugin that crashes mid-audio can't take down the host
  - a board file can't read other files, exhaust memory, or be half-written by a crash or power cut
  - a board never loses what it can't load: a missing plugin's settings and elements written by a newer app survive a save
  - the plugin scanner can't leak processes, deadlock on a big result, or silently lose its list
- **Product authority:** the master plan's acceptance criteria, and the user's decisions where they conflict or choose (Key Decisions).
- **Tracking:** milestone "M5 — Hardening: crash guard + .pdl" (iid 7, renumbered from M7 on 2026-09-13).
  - #39 (.pdl hardening) closes with U4, the last file unit.
  - #38 (crash guard) closes with U5.
  - #5 (scanner backlog) closes with U7.
- **Out of scope:**
  - the macOS crash guard (user decision; the M8 crash reporter covers macOS)
  - out-of-process plugin hosting
  - the R21 crash reporter itself (M8), which must coexist with U5's handlers
- **Stop conditions:** stop and surface if the crash guard needs the audio thread to allocate, lock or call anything but async-signal-safe functions. Also stop if any unit would change what an existing, valid `.pdl` loads as.

---

## Key Decisions

- **KD1. The crash guard covers Windows and Linux** (user decision, 2026-09-13).
  - Windows: an SEH shim.
  - Linux: a signal guard.
  - macOS: off, as the master plan designed (`:135-137`). Mach exceptions and the debugger make signal recovery unreliable there.
  - Roadmap R18's "macOS signals" verification is narrowed to match.
- **KD2. A dead plugin passes its input through** (user decision). This applies to one that crashed, and to one that couldn't be found.
  - The slot behaves like bypass, so the rest of the chain keeps sounding. It uses the dry snapshot BypassableInstance already takes.
  - AC-PL-1's "silence" would quiet everything after the plugin in a serial chain.
  - The card is flagged red with the reason. A click re-arms it (`clearDeath`).
- **KD3. Board files name plugins by ID, never by path** (user decision; AC-SE-5, AC-SE-6).
  - `<IDENTITY>` is written with format, unique id / class id, name, vendor and version. `file=` is never written.
  - On load, a plugin resolves only through the scanned KnownPluginList. Legacy `file=` attributes are read only as a matching hint, never opened.
  - `<IDENTITY>` attributes outside the allowlist are ignored, and path-like values are rejected.
- **KD4. Missing is a placeholder, not a failure** (AC-FS-9).
  - A plugin that isn't in the scanned list, or fails to load, becomes a *missing* node, and the rest of the graph builds. Today one missing plugin fails the whole patch.
  - The missing node passes audio through (KD2) and keeps its identity and state bytes verbatim, so a save writes them back unchanged.
  - The card reads "Missing: <name>" and says how to fix it (scan, or install the plugin).
  - A plugin whose `setStateInformation` crashes or throws counts as missing too, with its bytes kept (AC-SE-30).
- **KD5. Board files are parsed from memory, capped, with no DTDs** (AC-SE-7, AC-SE-8, AC-FS-11).
  - A file over 32 MB is refused before it's read.
  - Files are read into memory, and any `<!DOCTYPE` or `<!ENTITY` is refused before parsing. Parsing then goes through `juce::parseXML(String)`, which has no input source. `parseXML(File)` today can load `SYSTEM` DTDs and entities relative to the file.
  - Nesting deeper than 64 levels is refused.
  - Per patch: 10,000 nodes, 50,000 connections and 1,000 mappings. State blobs are capped at 16 MB each, and a malformed base64 blob is an error.
  - The cycle check becomes iterative, so no board can overflow the stack.
- **KD6. Saves are atomic and durable** (AC-FS-6). Every `.pdl` and user-data write goes through one `AtomicFileWriter`:
  1. write a temp file in the same folder
  2. flush it to disk (`FlushFileBuffers` / `fsync`)
  3. replace the target (`MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` / `rename`)
  4. on POSIX, fsync the folder

  A crash at any step leaves the old file or the new one, never a torn file. This covers `writeBank`, the preferences session file and the default board.
- **KD7. What a newer app wrote survives** (AC-FS-7).
  - Within schemaMajor 3, elements and attributes this version doesn't know are kept on the bank, the patch, the node and the mappings, and written back. A mapping with an unknown kind is kept verbatim rather than dropped or coerced.
  - A file with a higher schemaMajor opens **read-only** with a banner, instead of being refused. Save is disabled. Save As warns that this version can't keep what it doesn't understand.
- **KD8. The scanner never leaks, deadlocks or forgets** (#5, AC-SE-24):
  - A killed helper is reaped.
  - Helper output is read while it runs, so a big result can't block its pipe.
  - Shell plugins get 90 s.
  - A failed path records why it failed. Instruments are kept in the list and filtered at the picker, not blacklisted.
  - A scan-in-flight marker makes the next scan skip, rather than blacklist, whatever was being scanned when the host died.
  - The helper is looked for only next to the app.
  - A corrupt preferences file is set aside as `.corrupt` and triggers a re-scan prompt, never a silent reset. Settings files are written 0600 on POSIX.

---

## Implementation Units

### U1. Safe board-file parsing and limits

- **Goal:** No board file can read other files, expand without bound, exhaust memory or overflow the stack (KD5).
- **Requirements:** KD5; AC-SE-7, AC-SE-8, AC-FS-11; R19
- **Dependencies:** none
- **Files:**
  - `app-v3/src/session/PdlSerializer.*`: size cap, read into memory, refuse DTDs and entities, depth pre-scan, parse from a string
  - `app-v3/src/session/SafeXml.h` (create): the shared "bytes to XmlElement" helper and its limits
  - `app-v3/src/session/Patch.cpp`: count caps, checked base64 decode with a blob cap, iterative cycle check
  - `app-v3/src/session/PatchBank.cpp`, `app-v3/src/ui/FileActions.cpp` (the v2 sniff), `app-v3/src/session/PdlV2Importer.cpp`: the same helper and caps
  - `app-v3/tests/unit/session/PdlSerializer_test.cpp`, `app-v3/tests/unit/session/Preferences_test.cpp`
- **Test scenarios:**
  - XXE, an external DTD, a `SYSTEM` parameter entity, and billion laughs are all refused, and none of them reads a planted file
  - truncated XML, random bytes and an empty file each give a clear error
  - a 33 MB file is refused before it's read
  - 10,001 nodes, or a 17 MB blob, is refused
  - deep nesting is refused
  - a long chain passes the cycle check without recursion
  - the Preferences XXE test also asserts the planted file's text never appears
- **Verification:** ctest green on all three platforms.

### U2. Atomic, durable saves

- **Goal:** A crash or power cut during a save leaves the old file or the new one, never a torn one (KD6).
- **Requirements:** KD6; AC-FS-6; R19
- **Dependencies:** none
- **Files:**
  - `app-v3/src/session/AtomicFileWriter.h`, `app-v3/src/session/AtomicFileWriter.cpp` (create): write temp, flush, replace (write-through), directory fsync. It has step hooks for tests.
  - `app-v3/src/session/PdlSerializer.cpp` (`writeBank`), `app-v3/src/session/PreferencesPersister.cpp`, `app-v3/src/session/DefaultBank.cpp`
  - `app-v3/tests/unit/session/AtomicFileWriter_test.cpp` (create)
- **Test scenarios:**
  - a failure injected after each step leaves either the old contents or the new, and never a partial file
  - an unwritable folder reports an error and leaves the old file
  - the temp file sits beside the target and is removed on failure
- **Verification:** ctest green.

### U3. Plugins by ID, and missing-plugin placeholders

- **Goal:** Boards find plugins only through the scanned list. A plugin that isn't there becomes a pass-through placeholder that keeps its settings (KD3, KD4).
- **Requirements:** KD2, KD3, KD4; AC-SE-5, AC-SE-6, AC-FS-9, AC-SE-30
- **Dependencies:** U1
- **Files:**
  - `app-v3/src/session/Patch.*`:
    - IDENTITY is written without `file=`, with an attribute allowlist
    - a node records whether it's missing
    - its verbatim state bytes are kept
  - `app-v3/src/engine/PluginHost.*`: `resolve(identity)` through KnownPluginList
  - `app-v3/src/session/PreparedGraph.cpp`: a missing node becomes a pass-through placeholder, and a failed `setStateInformation` marks it missing
  - `app-v3/src/session/Session.cpp`: capture keeps the original bytes for a missing node
  - `app-v3/src/engine/processors/MissingPluginProcessor.*` (create)
  - `app-v3/src/ui/PluginCard.*` and `app-v3/src/ui/CardText.h`: the "Missing: <name>" state and its hint
  - `app-v3/src/ui/FileActions.cpp` and `app-v3/src/session/PdlV2Importer.cpp`: the v2 name match stays; paths are never loaded
  - `app-v3/tests/unit/session/Patch_test.cpp`, `app-v3/tests/unit/session/PdlSerializer_test.cpp`, `app-v3/tests/unit/session/Session_test.cpp`
- **Test scenarios:**
  - a saved board has no `file=`
  - a legacy board with `file=` still loads its plugin when that plugin is scanned
  - a board naming an unscanned plugin builds the rest of its chain, passes audio through the gap, and re-saves byte-identical state
  - a path-like attribute in `<IDENTITY>` is ignored
  - a throwing `setStateInformation` gives a missing node with its bytes kept
- **Verification:** ctest green. By hand: open a board whose plugin you've removed. The card says Missing, the rest plays, and the re-saved file keeps the plugin.

### U4. Keep what a newer app wrote

- **Goal:** Elements and attributes this version doesn't know survive a load and save. A newer major version opens read-only (KD7).
- **Requirements:** KD7; AC-FS-7; R19; closes #39
- **Dependencies:** U1, U3
- **Files:**
  - `app-v3/src/session/Patch.*`, `app-v3/src/session/PatchBank.*`: the unknown children and attributes kept on the bank, the patch, the node and the mappings, and unknown mapping kinds kept verbatim
  - `app-v3/src/session/PdlSerializer.*`: the root's unknowns kept, and a higher major returned read-only
  - `app-v3/src/session/BankDocument.*`: a read-only flag
  - `app-v3/src/ui/FileActions.cpp`: Save disabled while read-only, and Save As warns
  - `app-v3/src/ui/MainWindow.cpp` or `app-v3/src/ui/StatusBar.*`: the read-only banner
  - `app-v3/tests/unit/session/PdlSerializer_test.cpp`, `app-v3/tests/unit/session/Patch_test.cpp`
- **Test scenarios:**
  - a board with an extra element on each level round-trips it
  - an unknown mapping kind round-trips
  - a schemaMajor-4 file opens read-only
  - Save is refused on a read-only board
- **Verification:** ctest green.

### U5. Plugin crash guard (Windows + Linux)

- **Goal:** A plugin that segfaults during processing is caught. Its slot passes audio through, its card is flagged, and the host keeps running (KD1, KD2).
- **Requirements:** KD1, KD2; AC-PL-1, AC-SE-30; R18; closes #38
- **Dependencies:** U3 (the dead and missing card state)
- **Files:**
  - `app-v3/src/platform/CrashGuard.h` (create): `guardedCall(fn, ctx)`, returning whether the call faulted
  - `app-v3/src/platform/CrashGuard_windows.cpp` (create): a C-style `__try/__except` shim with no destructors in its frame, calling `_resetstkoflw` after a stack overflow
  - `app-v3/src/platform/CrashGuard_posix.cpp` (create), Linux only:
    - `sigaction` for SIGSEGV, SIGBUS, SIGFPE and SIGILL, installed at startup
    - a thread-local armed `sigjmp_buf`
    - chaining to the previous handler when not armed, so the M8 crash reporter still sees real crashes
  - `app-v3/src/platform/CrashGuard_none.cpp` (create), macOS: no guard
  - `app-v3/src/engine/BypassableInstance.*`:
    - both `processBlock` paths go through the shim
    - dead means dry pass-through (KD2)
    - the MIDI-filter path stops allocating on the audio thread
    - a death reason is recorded
  - `app-v3/src/engine/DeathEvents.h` (create): a preallocated SPSC FIFO of death events, drained on the message thread
  - `app-v3/src/session/Session.cpp`: `prepareToPlay` and `set/getStateInformation` are guarded
  - `app-v3/src/ui/SignalCanvas.cpp` and `app-v3/src/ui/PluginCard.*`: a red "Crashed: <reason>" state and a click to re-arm
  - `app-v3/CMakeLists.txt`, `app-v3/src/engine/CMakeLists.txt`: per-platform sources
  - `app-v3/tests/support/CrashingProcessor.h` (create), `app-v3/tests/unit/engine/CrashGuard_test.cpp` (create)
- **Test scenarios** (Windows and Linux; skipped on macOS):
  - a processor that writes through a null pointer is caught; its slot passes the input through, and the next block runs
  - a divide-by-zero trap and a stack overflow (Windows) are caught
  - after a catch, a crash outside a guarded call still reaches the previous handler (Linux)
  - re-arming runs the plugin again
  - no allocation occurs on the guarded path (RT-safety test)
- **Verification:** ctest green; the Windows and Linux jobs run the crash tests. By hand: a deliberately crashing test plugin is flagged while audio continues.

### U6. Scanner robustness

- **Goal:** Scanning never leaks processes, deadlocks on a big result, or blacklists the wrong thing (KD8, #5).
- **Requirements:** KD8; issue #5
- **Dependencies:** none
- **Files:**
  - `app-v3/src/engine/PluginScanner.*`:
    - reap after `kill()`
    - read the helper's output during the poll loop
    - 90 s for shell bundles
    - a failure reason per path
    - the scan-in-flight marker
    - sibling-only `locateHelper`
  - `app-v3/src/engine/PluginHost.cpp`: instruments kept in the list and filtered at the picker; no instrument sweep into the blacklist
  - `app-v3/src/ui/TransportBar.cpp`: the scan summary shows the reasons
  - `app-v3/tests/support/FakeScannerHelper.cpp` (create): sleeps, floods its output, or crashes, on demand
  - `app-v3/tests/unit/engine/PluginScanner_test.cpp`
- **Test scenarios:**
  - a helper that hangs is killed at the timeout and reaped, leaving no zombie (Linux)
  - a 1 MB result is read without a deadlock
  - a crashing helper records "crashed", and an instrument-only bundle records "instrument"
  - a marker left by a dead scan makes the next scan skip that path, without blacklisting it
  - the helper is found beside the app
- **Verification:** ctest green.

### U7. A plugin list that survives corruption

- **Goal:** A corrupt preferences file never silently resets the plugin list and blacklist (KD8, AC-SE-24).
- **Requirements:** KD8; AC-SE-24; closes #5
- **Dependencies:** U2 (the durable writer), U6
- **Files:**
  - `app-v3/src/session/Preferences.*`:
    - detect a file that exists but failed to load (`PropertiesFile` not loaded OK)
    - move it aside as `.corrupt`
    - report it
    - write through the atomic writer, 0600 on POSIX
  - `app-v3/src/App.cpp`: the re-scan prompt
  - `app-v3/src/engine/PluginHost.cpp`: the crash list is parsed strictly, and the folder name matches the preferences folder (the doc drift)
  - `app-v3/tests/unit/session/Preferences_test.cpp`
- **Test scenarios:**
  - a corrupt settings file is renamed `.corrupt`, and the next load reports it instead of resetting silently
  - written settings are 0600 (POSIX)
  - a malformed crash list is reported, not half-read
- **Verification:** ctest green. By hand: truncate `Preferences.settings`. The app says so and offers a re-scan.

---

## Verification Contract

- ctest green on Windows, Linux and macOS, with every job listed, the allow_failure ones included.
- The crash-guard tests run for real on Windows and Linux, and are reported as skipped on macOS.
- The hostile-file tests run on all three platforms.
- Every existing, valid `.pdl` in the test suite still loads to the same patch (the stop condition).

## Risks

- **Signal recovery on Linux can leave a plugin's heap or locks broken** (a fault inside malloc). The guard stops the crash, but the plugin stays dead until re-armed, and a re-arm after a heap fault may crash for real. The card says "restart recommended".
- **`sigsetjmp` with the signal mask costs a syscall per block.** Use `savemask = 0` and unblock the signal after the jump, off the audio path.
- **Windows and macOS CI jobs are still allow_failure.** Making them required is M7's #33. Until then, U5's Windows results are read from the job list every time.
- **KD3 changes behaviour for boards whose plugins aren't scanned:** they now show Missing instead of loading from the path. The card says to scan.

## MR plan

1. U1 and U2: safe parsing, limits and durable saves, plus this plan and the roadmap reorder
2. U3: plugins by ID and missing placeholders
3. U4: keep what a newer app wrote (closes #39)
4. U5: the crash guard (closes #38)
5. U6: scanner robustness
6. U7: a plugin list that survives corruption (closes #5)
