---
title: Crash visibility — symbols, a log, a last-session report, and the macOS guard
type: feat
status: active
date: 2026-10-13
topic: crash-visibility
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: the maintainer's decisions of 2026-10-13 ("We want a crash guard", full guard on macOS, reporter scope "logs, symbols, breadcrumb") and the research recorded in Key Decisions
execution: code
---

# Crash visibility — symbols, a log, a last-session report, and the macOS guard - Plan

## Goal Capsule

- **Objective:** when something goes wrong, the user has something to hand over, and the maintainer can read it.
  - Today a plugin crash is caught and turned into a placeholder (M5 U5), but a crash of the app itself leaves nothing behind: there is no log file, no report, and no published symbols to decode a dump with.
  - Every `juce::Logger::writeToLog` call in the app currently goes nowhere, because no logger is ever installed.
  - On macOS the plugin crash guard is off, so one bad plugin takes the whole app down.
- **Product authority:** the maintainer's two decisions of 2026-10-13, recorded as KD1 and KD2 below.
- **Tracking:** milestone M7 (public beta). Marker units `CV-U<N>`. Relates to #41 (crash reporter) and #87 (the hands-on pass).
- **Out of scope:**
  - uploading anything anywhere (KD3)
  - writing minidumps or stack traces from inside a signal handler (KD2); if that ever happens, KD6 says how
  - out-of-process plugin hosting, which is the durable fix for plugin crashes on every platform and belongs to v3.1
  - an emergency save of unsaved work at crash time, worth doing later: for a live-performance host, rescuing the patch beats any stack trace
- **Stop conditions:** stop and surface if U4's handler proves unreliable on real Apple Silicon hardware, or if any unit would make a plugin crash that is currently caught become fatal.

---

## Key Decisions

- **KD1. The crash guard is enabled on macOS, with recovery (maintainer, 2026-10-13).** This overrides the master plan (`2026-04-12-001`, "brittle on macOS") and hardening KD1. The maintainer chose it after being shown the risks:
  - On macOS a hardware fault is a Mach exception first; a debugger or a system crash reporter holding the task's exception port sees it before any BSD signal, so the handler may never run.
  - `siglongjmp` out of a handler abandons whatever locks the Objective-C runtime or the CoreAudio HAL held, so the process is undefined afterwards: a later editor repaint can deadlock.
  - No comparable host does this in process: Bitwig and Carla host plugins in a child process, Ardour declines crash protection, and JUCE's maintainers decline to add it.
  - Accepted mitigations, all in U4: keep catching SIGBUS as well as SIGSEGV; touch the thread-local guard state before arming, since a macOS TLS first touch can allocate; and after any catch on macOS, mark the session "restart recommended" and say so in the UI.
- **KD2. The reporter goes as far as a breadcrumb, no further (maintainer, 2026-10-13).** Publish symbols, write a log, and leave a last-session report. No fault handler writes the report.
- **KD3. Reports stay on the user's machine.** The app shows the file and offers to reveal it; the user attaches it to an issue. Nothing is uploaded, and no endpoint is compiled in. This matches what Audacity, MuseScore, KiCad and OBS do: none upload without an explicit click.
- **KD4. Build identity is the load-bearing field.** A report is worthless without it: the PDB GUID and age on Windows, the dSYM UUID on macOS, and `.note.gnu.build-id` on Linux. They change on every link, so U1 keys the symbol archive by version and U3 prints the identity in the report.
- **KD5. Don't use `juce::SystemStats::setApplicationCrashHandler`.** It installs plain `::signal` handlers, discards the previous one so nothing can chain, which would break `CrashGuard`'s `passOn`, runs a stack-overflow handler on the exhausted stack, and on macOS ends in `SIGKILL`, which suppresses the system's own `.ips` report. `getStackBacktrace` also allocates, so it is not safe from a handler.
- **KD6. If a Windows minidump is ever added**, it is either WER LocalDumps *or* an out-of-process writer, never an in-process `SetUnhandledExceptionFilter`. Microsoft warns in-process dumping can deadlock, LocalDumps and a custom handler are mutually exclusive, and a filter misses `__fastfail`, heap corruption and `/GS` failures anyway.

---

## Unit Index

- U1. Ship debug symbols with every release
- U2. A log file
- U3. The last-session report
- U4. The crash guard on macOS

---

### U1. Ship debug symbols with every release

- **Goal:** every release keeps the symbols needed to decode a crash from that exact build.
- **Requirements:** KD4
- **Dependencies:** none
- **Files:**
  - `tools/package.ps1`: collect `Pedalboard 3.pdb` and `pb3-scanner-helper.pdb` into `Pedalboard3-<version>-windows-x64-symbols.zip`, beside the app package and never inside it.
  - `tools/package.sh`: the same for the macOS `.dSYM` bundles and the Linux unstripped binaries or `.debug` files.
  - `.gitlab-ci.yml`: upload the symbol archives with the rest in `release:gitlab`, as a separate asset link per platform.
  - `docs/releases/README.md`: a line saying where symbols live and that they must match the build.
- **Test scenarios:** `./build.ps1 package` produces the symbols zip; it contains a PDB whose GUID matches the shipped exe (`dumpbin /headers` or `llvm-pdbutil`).
- **Verification:** a release pipeline uploads four packages plus the symbol archives, and the Release page links them.

### U2. A log file

- **Goal:** the app writes a log, so a user can send one and the existing log lines stop vanishing.
- **Requirements:** KD2
- **Dependencies:** none
- **Files:**
  - `app-v3/src/App.cpp`: install a `juce::FileLogger` as the first thing `initialise` does, before the crash guard, writing beside the settings file (`Preferences::defaultSettingsFile()`'s folder) as `logs/Pedalboard 3.log`, rotated so the folder stays small.
  - The header line records the version, the build identity (KD4), the OS and the CPU.
  - `app-v3/src/session/Preferences.h`: expose the folder, so both the log and the report agree on where they live.
  - Add log lines where the app already knows something worth keeping: audio device open and failure, scan start and summary with the per-plugin errors #96 surfaces, plugin death and re-arm, and board open and save failures.
- **Test scenarios:** a unit test writes through the logger into a temp folder and checks the file appears with the header; rotation keeps at most N files.
- **Verification:** run the app, open Settings, cause a scan, quit: the log exists and reads sensibly, and `RealSettingsGuard` still passes.

### U3. The last-session report

- **Goal:** after an unclean exit, the app offers a file that says what it was doing.
- **Requirements:** KD2, KD3, KD4
- **Dependencies:** U2
- **Files:**
  - `app-v3/src/session/SessionRecord.h` / `.cpp` (create): extend the existing restore-in-flight marker (`session::writeRestoreInFlightMarker`) into one record, rather than adding a second file. It holds the version and build identity, the OS and device, the plugin list, the active patch identity, the restore-in-flight state, and a clean-exit flag that `App::shutdown` sets.
  - `app-v3/src/App.cpp`: write it at startup, clear it on a clean quit, and on a dirty start build a report next to the log.
  - `app-v3/src/ui/`: an alert on next launch, "Pedalboard 3 didn't shut down cleanly", with Reveal and Dismiss. It must say plainly what the file contains, because it names the user's plugins and patch.
  - Paths are written with the user's home folder replaced by `~`, and no environment variables are included.
  - A caught plugin crash is not an unclean exit: it is a line in the record, not a report.
- **Test scenarios:** a record written and not cleared produces a report on the next run; a clean exit produces none; a caught plugin crash alone produces none; the report contains no absolute home path.
- **Verification:** kill the app from Task Manager, relaunch, and the offer appears with a readable file.

### U4. The crash guard on macOS

- **Goal:** a plugin that crashes on macOS becomes a placeholder, as it already does on Windows and Linux (KD1).
- **Requirements:** KD1
- **Dependencies:** none
- **Files:**
  - `app-v3/src/engine/CMakeLists.txt`: build `platform/CrashGuard_posix.cpp` on Apple instead of `CrashGuard_none.cpp`.
  - `app-v3/src/platform/CrashGuard_posix.cpp`: touch the thread-local guard state and alternate stack before arming on every thread that calls `guardedCall`, so a first touch can't allocate inside the handler.
  - `app-v3/src/engine/BypassableInstance.cpp` and the session: after a catch on macOS, set a "restart recommended" flag on the session; the status bar says so once.
  - `app-v3/tests/unit/engine/CrashGuard_test.cpp`: the cases currently skipped on macOS run there.
  - `docs/releases/v1.0.0-beta.1.md`: the macOS note changes from "a crashing plugin takes the app down" to what it now does, with the caveat that recovery on macOS is best-effort and a restart is advised.
- **Test scenarios:** the existing guard tests pass on the macOS runner; a plugin that crashes in `processBlock` becomes a placeholder and the app keeps running.
- **Verification:** the macOS CI job runs the guard tests green, and a hands-on check on Apple Silicon (#87) confirms the placeholder path. Note that the macOS CI job is still `allow_failure`, so a red guard test there is invisible: check it by hand until #33's merge-request half lands.
