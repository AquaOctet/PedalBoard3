---
title: "chore: close out the out-of-process plugin scanner (MR !14)"
type: chore
status: completed
date: 2026-06-06
origin: docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md
---

# ♻️ Close out the out-of-process plugin scanner (MR !14)

> **Context recovery note.** The conversation that drove this work was lost.
> This plan is a reconstruction from the repo state on `2026-06-06`, branch
> `feat/out-of-process-plugin-scanner` (HEAD `010d395`, fully pushed, clean
> tree), and the still-open **MR !14 → main**. The feature is *functionally
> complete*; this plan covers the **wrap-up** needed to land it cleanly, not
> new functionality.

## Enhancement Summary

**Deepened on:** 2026-06-06
**Research agents used:** Explore (full-project trajectory map),
best-practices-researcher (out-of-process scanning), architecture-strategist,
code-simplicity-reviewer, learnings-researcher.

### Key changes from the first draft
1. **Corrected the mechanism count from two to three.** A third blacklister
   exists — `PluginHost::changeListenerCallback` (`PluginHost.cpp:25-52`) sweeps
   and blacklists *every* `isInstrument` descriptor on *every* list mutation.
   The floor list's true load-bearing role is narrower and sharper than the
   first draft stated.
2. **Defused a comment-wording footgun.** The corrected comments must tie the
   floor list to **launch-time, pre-construction, path-based** blacklisting
   (the `Current.vst3` case that crashes *during* `createInstance`), **not** to
   "the in-process scan path" — otherwise a future maintainer who removes the
   in-process fallback re-arms the exact crash this MR prevents.
3. **Fixed the test.** The originally-proposed instrument-drop test was a
   tautology (the filter lives in a private method that spawns a subprocess).
   Decision: **extract the filter into a testable free function**, then test the
   real code path. Small, behavior-preserving, deliberate expansion beyond
   "comment-only."
4. **Added a prioritized Hardening Backlog** (post-merge, explicitly *out* of
   close-out scope) capturing real scanner bugs the research surfaced: Unix
   signal-exit-code crash detection, AUv3 silent-skip, hardened-runtime
   entitlements for the signed helper, zombie reaping, shell-plugin timeouts.
5. **Added the full Project Context & Roadmap** (end goal, phase status, where
   this fits, what's next) per the request to confirm the whole picture.

### New considerations discovered
- Crash detection via `exitCode != 0` is **unreliable on Unix** — JUCE's
  `getExitCode()` returns 0 for signal-killed children. Today we're saved only
  by the secondary XML-parse-failure branch.
- **AUv3 plugins are silently skipped** on macOS because the helper runs no
  message loop — effectively VST3-only on macOS today.
- The OOP scanner **never re-validates floor-listed plugins** (it skips
  blacklisted paths), so the subprocess isolation is "spent" on plugins it
  never actually scans.

---

## Project Context & Roadmap

> Added per the request to confirm *what we're building, the end goal, and how
> we get there.* Source: master plan §4.3 + git history + plan docs
> (see origin). Status verdicts are evidence-backed as of `2026-06-06`.

**End goal — v3.0.0.** A from-scratch modern cross-platform VST host honoring
Pedalboard2, shipping as **signed/notarized installers on Windows, macOS, and
Linux + an auto-update feed** (master plan §4.3 Phase 6 deliverable). It targets
v2 feature parity (drag-to-connect graph, MIDI/OSC mapping, patch switching with
atomic graph swap, FilePlayer/Looper/Metronome) **plus** new capabilities
(MidiOutputNode, automatic PDC, undo/redo of topology+mappings, VST3+AU+CLAP+LV2,
per-node CPU metering, crash isolation, structured event log). Success metrics:
zero crashes per 100 logged hours within 3 months; 100% of the v2 patch corpus
opens with correct topology+mappings; patch-swap glitch <1/1000.

**Where we are:** Phase 0 done; **Phase 1 (~90%)** is the active phase; Phase 3
(~35%) has foundations; Phases 2, 4, 5, 6 not started.

| Phase | Goal | Status | Evidence |
|---|---|---|---|
| **0 — Infra/CI** | CMake, presets, CI, justfile, AGENTS.md | ✅ **Done** | `app-v3/CMakeLists.txt`, `CMakePresets.json`, `.gitlab-ci.yml`, ongoing CI hardening commits |
| **1 — Audio engine core** | Graph runtime, bypass, host, **scanner**, processors | ⚠️ **In progress (~90%)** | `engine/{AudioGraphRuntime,BypassableInstance,PluginHost,PluginScanner,AudioEngine}`; **this MR** is the scanner |
| **2 — Session/persistence** | `.pdl` v3 write + v2 import, AtomicFileWriter, PluginIdentity | ❌ **Not started** | `PdlSerializer`/`AtomicFileWriter`/`v2_import/` absent — **first major gap** |
| **3 — UI** | Field canvas, dialogs, menus, LookAndFeel | ⚠️ **In progress (~35%)** | `ui/{MainWindow,TransportBar,SignalCanvas,EditorWindow,PluginCard,ColourScheme,Pb3LookAndFeel}` exist; no dialogs/menus/drag-to-connect |
| **4 — MIDI/OSC control** | Router, mappings, OSC, dispatcher | ❌ **Not started** | `control/` empty |
| **5 — Processors + polish** | FilePlayer, Looper, Recorder, Metronome, MidiOut, PDC | ❌ **Not started** | `engine/processors/` has only `OutputSanitiser` + `TestToneProcessor` |
| **6 — Release/sign/update** | MSI/DMG/AppImage, signing, auto-update | ❌ **Not started (deferred)** | `packaging/` absent; runners unprovisioned per AGENTS.md |

**How the scanner close-out fits:** it completes **Phase 1.C (PluginScanner with
crash list, §4.3)**. The scanner is load-bearing for everything downstream — once
Phase 2 ships `.pdl` load, users open v2 patches referencing dozens of plugins,
and scan-time crash isolation must be solid. **Immediate next unit after this
close-out: Phase 2 `.pdl` persistence** (the first net-new gap), with Phase 3 UI
dialogs/menus as the parallel track.

---

## Overview

The out-of-process plugin scanner (master plan §4.2.A, Phase 1.C) is built,
wired in, and green in CI. `engine::PluginScanner` drives the
`pb3-scanner-helper` subprocess per plugin file; a crashing plugin kills only
the helper, the parent observes the failure and blacklists the path. The UI
Rescan button runs it via a self-deleting `ThreadWithProgressWindow`
(`TransportBar.cpp:338`), the macOS `.app` bundle embeds the helper (`2fca755`),
and instrument descriptors are filtered at scan time (`36da048`, `010d395`).

What remains is **close-out**, not coding-heavy work:

1. **Resolve the floor-list contradiction** (the heart of this MR). Five comment
   sites tell a future maintainer `engine/KnownProblemPlugins.h` becomes
   "redundant once wired in" / "goes away in v3.1." That is wrong *and dangerous*
   — the floor list is still load-bearing for the **launch-time pre-construction
   blacklist** that nothing else can replace. Decision: **keep it, correct the
   comments, and word them so they survive a future removal of the fallback.**
2. **One real test:** extract and pin the instrument-drop invariant.
3. **MR metadata hygiene:** MR !14's title/description still describe only the
   first commit, not the full branch scope.
4. **CI status note:** the lone red job (`test:macos:unit`) is a VM-provisioning
   timeout, not a failure — confirm and document, do not chase (infra deferred
   to Phase 6 per AGENTS.md and [[feedback_infra_timing]]).
5. **Hand off for human merge** (agent never merges — AGENTS.md,
   [[feedback_git_remote_ops]]).

## Problem Statement / Motivation

The branch is ready to merge but carries **documentation debt that actively
misleads**. Five comment sites say the floor list is a temporary v3.1 stopgap
the scanner replaces. A maintainer reading them would reasonably delete
`KnownProblemPlugins.h` after this MR merges — silently removing host-crash
protection. Closing the MR without reconciling this leaves a loaded footgun.

**Why the comments exist (for the MR narrative):** they weren't careless — they
tracked the *original* roadmap, where the master plan (§R3, line 856) scheduled
out-of-process scanning for **v3.1** and the floor list as the **v3.0** stopgap.
Reality moved faster: OOP scanning landed in v3.0. So this is **plan-vs-reality
reconciliation, not a reversal of a prior decision** — good framing for the MR
description.

### The system actually has THREE blacklist mechanisms (not two)

| # | Mechanism | Trigger | Keys off | Catches |
|---|---|---|---|---|
| 1 | **Launch floor** — `PluginHost::applyKnownProblemBlacklist` (`PluginHost.cpp:182`, called `App.cpp:36`) | App launch, before any scan | **file path** (3 hardcoded basenames) | plugins that crash *during construction* (e.g. `Current.vst3` `__cxa_pure_virtual`) — **before any descriptor is added** |
| 2 | **OOP scanner** — `PluginScanner::scanFile` → `addType`/`addToBlacklist` (`PluginScanner.cpp:144`) | Rescan button | file path + uniqueId | any plugin that crashes the helper subprocess |
| 3 | **Instrument sweep** — `PluginHost::changeListenerCallback` (`PluginHost.cpp:25-52`) | *every* `KnownPluginList` mutation | `isInstrument`, blacklists by `fileOrIdentifier` | any instrument descriptor that *successfully made it into the list* |

**The crux:** mechanism 3 only fires *after* a descriptor is successfully added.
`Current.vst3` crashes *during* `createInstance` — the descriptor is never added,
mechanism 3 never runs, and the host is already dead. **Only mechanism 1
(launch-time, path-based, pre-construction) prevents that.** The instrument sweep
double-covers the three floor entries *only because they happen to be
instruments* — that is an invariant the floor list silently relies on, not a
guarantee. Add a crashing *effect* to the floor list tomorrow and only mechanism
1 protects it.

## Proposed Solution

Keep all three mechanisms; document the division of responsibility correctly,
and make every comment say so. **Code behavior is unchanged except for one small,
test-enabling refactor** (extracting the instrument filter).

| Mechanism | Standing role (post-MR) |
|---|---|
| **Launch floor** (`KnownProblemPlugins.h`) | **Primary, irreplaceable:** pre-construction, path-based blacklist at launch for plugins that crash before any descriptor exists. Independent of the in-process *and* out-of-process scan paths. |
| **OOP scanner** | Primary path for generic, safe, crash-isolated scanning (Rescan button). |
| **Instrument sweep** | Enforces the effects-only invariant (§4.2.D) regardless of how a descriptor entered the list. |

**Comment-wording rule (critical):** tie the floor list to *"launch-time,
pre-construction, path-based blacklisting of plugins that crash before a
descriptor is ever added — which neither the in-process nor the out-of-process
scanner can catch."* Do **not** write "defense-in-depth for the in-process
path." Add one sentence noting floor entries need not be instruments
(launch-time path-blacklist covers effects too).

## Scope — explicit IN / OUT

**IN scope (this MR / close-out):**
- Correct the five stale comments (wording rule above).
- Extract the instrument filter to a free function + add one real unit test.
- Update MR !14 title/description; confirm + note CI status.
- Hand off for human merge.

**OUT of scope (tracked in Hardening Backlog below; do NOT block the green MR):**
- Unix signal-exit-code crash detection, AUv3 message-loop, signed-helper
  entitlements, zombie reaping, shell-plugin timeout, resume marker,
  persist-all-vs-filter-at-picker. These are real but belong to follow-up
  issues / Phase 6, per [[feedback_infra_timing]].

## Technical Considerations

- **Keeping the floor list needs no behavior change** — the only code change in
  scope is extracting the `!d.isInstrument` filter from the private `runHelper`
  into a free function so the test pins the *real* predicate, not a copy.
- **`scanAllDefaultPaths` honors the blacklist** (`PluginScanner.cpp:223`), so
  floor-listed plugins are skipped by the OOP scan too. Consequence (worth
  stating, not fixing now): the crash-isolating scanner **never actually scans
  the three most dangerous plugins** — they're path-blacklisted at launch first.
  Long-term cleanest architecture is *OOP-subsumes-floor-for-Rescan,
  floor-stays-for-launch-only* — parked under Future Considerations.
- **Effects-only invariant.** `isInstrument` is the *only* type discriminator
  JUCE's `PluginDescription` exposes (no plugin-type field) — confirmed in the
  UI-integration plan. A bundle exposing only instruments yields an empty result
  and gets blacklisted (`PluginScanner.cpp:160-168`), conflating "instrument"
  with "crashed" in persisted state. Intended for v3.0; the un-blacklist seam is
  the v3.1 instrument-support path.

### Research Insights (best-practices-researcher)

- **Pedalboard 3's per-process model is sound** and gives cleaner crash
  attribution than JUCE's reference scanner (one long-lived worker over a socket,
  `extras/AudioPluginHost/Source/HostStartup.cpp`). Keep it.
- **Things we're doing right, don't change:** `waitForProcessToFinish` *before*
  `readAllProcessOutput` (avoids JUCE's documented byte-drop), eager-flushed
  durable blacklist, keeping the floor list, the `restore_in_flight` marker
  pattern in `PreferencesPersister`.
- **AC-SE-24 check (from learnings):** the master plan required the crash store
  to use strict parsing, `0600` ACL, and *"corruption → re-scan prompt, not
  silent reset."* The close-out now relies on JUCE's `KnownPluginList` XML inside
  `preferences.xml`. Add one line to the MR confirming the corruption-handling
  invariant still holds (or filing it as a Phase 2 follow-up).

## System-Wide Impact

- **Interaction graph:** `App::initialise` → `applyKnownProblemBlacklist`
  (launch floor) → later, Rescan → `TransportBar::rescanPlugins` →
  `findHelperBinary` → `PluginRescanThread` (worker thread) →
  `scanAllDefaultPaths` → per-file `scanFile` → `runHelper` (subprocess) →
  `KnownPluginList::addType`/`addToBlacklist` → **fires `changeListenerCallback`
  (instrument sweep)** → `threadComplete` persists via
  `preferences.setKnownPluginList` + `flush`. All three mechanisms mutate the
  *same* `KnownPluginList`; they compose because all current floor entries are
  instruments — an invariant, not a guarantee (see Problem Statement).
- **Threading caveat (corrected):** `scanAllDefaultPaths` reads and mutates
  `host_.getKnownPluginList()` from the **worker thread** while the message
  thread can also read it (e.g. the Add-Plugin picker). `changeListenerCallback`
  takes a `ScopedWriteLock`, but `getKnownPluginList()` hands out a raw reference
  and `KnownPluginList` is not documented as thread-safe. This is **pre-existing,
  not introduced here**, but the plan should not claim safety it hasn't
  established — correctness relies on callers using the locked accessors
  (`getKnownEffects`). Note it; don't fix it in this MR.
- **Error propagation:** helper non-zero exit / timeout / unparseable output →
  `ScanResult.error` populated, path blacklisted, surfaced in the post-scan
  summary alert (`TransportBar.cpp:367-379`). **Caveat (backlog):** on Unix a
  signal-killed helper reports exit code 0, so the *parse-failure* branch — not
  the exit-code branch — is what actually catches crashes today.
- **State lifecycle:** mutations are additive; the change listener flushes on
  every `addType`, so successes persist incrementally. A crash mid-scan loses at
  most the un-flushed tail (validated against the preferences-persistence
  learnings: ≤3s flush window, additive-only model).
- **API surface parity:** both the in-process path (Plugin List dialog) and the
  out-of-process path (Rescan) must keep the host alive on a known-crasher.
  After this MR the Rescan path is primary; the **launch floor** guarantees
  pre-construction crashers are blocked on *both* paths.

## Acceptance Criteria

- [ ] Floor list **kept**; no behavioral change to `applyKnownProblemBlacklist`,
      `App.cpp` launch wiring, the instrument sweep, or the `TransportBar`
      fallback.
- [ ] All five stale comments corrected **per the wording rule** (tie to
      launch-time / pre-construction / path-based; not "the in-process path"):
  - [ ] `app-v3/src/engine/KnownProblemPlugins.h:19-20` ("Until v3.1's … lands")
  - [ ] `app-v3/src/engine/KnownProblemPlugins.h:27-28` ("…after we move to OOP")
  - [ ] `app-v3/src/engine/PluginHost.h:144-145` ("Replaced … in v3.1 … this
        list goes away.")
  - [ ] `app-v3/src/engine/PluginScanner.h:24-25` ("Replaces (eventually) … the
        floor list (which becomes redundant once this is wired in).")
  - [ ] `app-v3/src/App.cpp:~31` ("the only protection until v3.1's OOP scanner…")
  - [ ] One added sentence (in `KnownProblemPlugins.h`) noting floor entries
        need not be instruments — launch-time path-blacklist covers effects too.
- [ ] Instrument filter **extracted** to a free function and **unit-tested**
      against the real predicate (see MVP).
- [ ] `just format` clean (pre-hoist long calls per
      [[feedback_clang_format_split_hoist]]); `just build` + `just test` green on
      Linux locally / in CI.
- [ ] CI confirmed green on Linux + Windows unit + all three builds + lint;
      `test:macos:unit` confirmed a VM-provisioning timeout (`allow_failure`),
      noted in the MR.
- [ ] MR !14 title + description updated to the full branch scope (helper +
      parent scanner + UI Rescan + macOS bundle embed + instrument filter + CI),
      framed as plan-vs-reality reconciliation, with a one-line "floor list
      retained by design (launch-time pre-construction protection)" note.
- [ ] Hardening Backlog items filed as follow-up issues (or a tracking note),
      explicitly *not* part of this MR.
- [ ] MR handed off to the human for merge (agent does not merge).

## MVP — extract the filter, then pin it

The instrument filter currently lives inline in the **private** `runHelper`,
which spawns a real subprocess — untestable without a live helper. Extract it so
the test exercises the actual code path (not a copy).

### app-v3/src/engine/PluginScanner.h (add a free function)

```cpp
// Parse a <PB3_SCAN_RESULT> XML tree into effect-only descriptions.
// Drops isInstrument entries at the source (master plan section 4.2.D);
// a bundle exposing both effect + instrument types keeps only the effect.
// Free function so it is unit-testable without spawning the helper.
juce::Array<juce::PluginDescription> effectDescriptionsFromScanXml(const juce::XmlElement& scanResult);
```

### app-v3/src/engine/PluginScanner.cpp (runHelper calls it)

```cpp
// runHelper, replacing the inline loop at lines 134-139:
result.descriptions = effectDescriptionsFromScanXml(*root);
```

### app-v3/tests/unit/engine/PluginScanner_test.cpp (real regression pin)

```cpp
TEST_CASE("effectDescriptionsFromScanXml keeps effects, drops instruments", "[scanner]")
{
    // Build a <PB3_SCAN_RESULT> with one effect + one instrument PLUGIN.
    // Assert only the effect survives, and an instrument-only tree -> empty.
    // This pins engine::effectDescriptionsFromScanXml -- the exact code
    // runHelper runs -- so deleting the filter breaks the test (unlike a
    // copied predicate, which a refactor would silently bypass).
    juce::XmlElement root("PB3_SCAN_RESULT");
    // ... append effect (isInstrument=false) and instrument (isInstrument=true)
    const auto kept = pedalboard3::engine::effectDescriptionsFromScanXml(root);
    REQUIRE(kept.size() == 1);
    CHECK_FALSE(kept[0].isInstrument);
}
```

> The empty-result → blacklist consequence is **already** covered by existing
> integration cases (`PluginScanner_test.cpp:173-220`); this adds only the
> filtering invariant the recent commits introduced.

## Hardening Backlog (post-merge — NOT in close-out scope)

Surfaced by research; file as follow-up issues so the green MR isn't bloated.
Prioritized:

| Pri | Item | Where | Fix sketch |
|---|---|---|---|
| **High** | Crash detection keys off `exitCode != 0`, but Unix signal-killed children report exit code **0** | `PluginScanner.cpp:96` (confirmed in vendored `juce_SharedCode_posix.h:1237`) | Treat *absence of the `<PB3_SCAN_RESULT>` sentinel* as the primary crash signal (document it); optionally `waitpid`+`WIFSIGNALED` for which-signal diagnostics |
| **High** | **AUv3 silently skipped** — helper has no running message loop; JUCE's `findAllTypesForFile` early-returns for AUv3 off a blocked message thread | `pb3-scanner-helper.cpp:64-85` | Run a real loop (mirror `extras/AudioPluginHost/Source/HostStartup.cpp`) for AU, or document the helper as VST3-only on macOS |
| **High** | Signed helper will appear to **crash on every plugin** under hardened runtime | Phase 6 signing | Helper needs its *own* entitlements with `com.apple.security.cs.disable-library-validation`; sign `-o runtime` (NOT `runtime,library`), bottom-up. Bake into the Phase 6 signing plan now so it isn't a late "all plugins crash" mystery |
| Med | Zombie children after `kill()` on Linux | `PluginScanner.cpp:88` | Reap after kill (`getExitCode()`/`isRunning()`) or use `ChildProcessManager` |
| Med | 30s timeout too short for shell plugins (WaveShell) | `PluginScanner.h:100` | Raise to 60-90s or special-case shells |
| Med | Effects-only blacklists instrument-only bundles, conflating "instrument" with "crashed" | `PluginScanner.cpp:134-168` | Consider persist-all + filter-at-picker for lower v3.1 regret; at minimum document the list is not a full inventory |
| Low | No resume marker for the scan loop (resume-don't-restart) | `PluginScanner.cpp:211-229` | Write current path to a marker before each `runHelper`; blacklist if still present at next launch (pattern already exists in `PreferencesPersister`) |
| Low | `locateHelper` walks 6 parent levels with recursive `findChildFiles` (dev-only) | `PluginScanner.cpp:38-51` | If the build always copies the helper beside the binary, the artefacts-walk fallback may be redundant — simplify later |

## Dependencies & Risks

- **Risk: someone deletes the floor list anyway.** Mitigated by the wording rule
  (tie to launch-time pre-construction, not "in-process path") + the MR
  description note. The first draft's wording would *not* have prevented this.
- **Risk: macOS CI masks a real macOS regression.** Accepted — runner
  provisioning is Phase 6 (AGENTS.md; [[feedback_infra_timing]]). The macOS job
  is `allow_failure`; per [[reference_glab_cli]] confirm red = provisioning
  timeout, note it, move on. *Note:* the AUv3-skip and signing issues above will
  first surface only on a signed macOS build, so they are currently **unexercised
  by CI** — call this out in the backlog issues.
- **Merge is human-owned** (AGENTS.md; [[feedback_git_remote_ops]]). Plan ends
  at "ready for merge."

## Alternative Approaches Considered

- **Delete `KnownProblemPlugins.h` entirely.** Rejected: removes the only
  pre-construction protection for `Current.vst3` (crashes before any descriptor
  or listener can act). The instrument sweep cannot help — it runs only *after* a
  descriptor is added.
- **Remove the in-process fallback so the floor list can go.** Rejected: the
  floor list's primary job is **launch-time** pre-blacklist, independent of the
  fallback — removing the fallback doesn't make it deletable. (This is exactly
  the reasoning a future maintainer must not miss; hence the wording rule.)
- **Have the OOP scanner subsume launch-time protection.** Rejected for now:
  would mean a subprocess scan at every launch — the latency cost the
  Rescan-button design exists to avoid.
- **Ship the comment-only test (no extraction).** Rejected: it pins a *copy* of
  the predicate (tautology) and would not catch the filter being deleted.

## Future Considerations

- Long-term, let the OOP scanner *re-validate* floor-listed plugins
  out-of-process (currently skipped via the blacklist at
  `PluginScanner.cpp:223`) so a fixed Current/Kontakt can be re-admitted
  automatically — making the floor list a **launch-time-only** necessity.
- When v3.1 adds instrument support, `effectDescriptionsFromScanXml` and the
  empty-bundle→blacklist behavior become the un-blacklist seam.
- **First `docs/solutions/` entries.** `docs/solutions/` does not exist yet.
  Three close-out topics have zero institutional record and are worth capturing
  once this lands: (1) `ThreadWithProgressWindow` without
  `JUCE_MODAL_LOOPS_PERMITTED` (`launchThread` vs `runThread`); (2)
  `ScopedJuceInitialiser_GUI` in a console helper for AU scanning; (3) embedding
  `pb3-scanner-helper` in the `.app` bundle.

## Sources & References

### Origin
- **Origin document:** [master plan §4.2.A + §4.3 Phase 1, §R3 line 856](../plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md) —
  carried forward: (1) out-of-process scanning is the structural fix; OOP was
  *originally* scheduled v3.1 (R3) and landed early in v3.0 — the source of the
  stale comments; (2) effects-only host for v3.0 (§4.2.D); (3)
  `AtomicFileWriter`/durable-write + AC-SE-24 corruption handling for the crash
  list and prefs.

### Internal References
- Parent scanner: `app-v3/src/engine/PluginScanner.cpp` (filter `134-139`,
  blacklist-on-empty `160-168`, blacklist-honoring `222-223`) / `.h`
- Helper binary: `app-v3/src/tools/pb3-scanner-helper.cpp` (`64-85`)
- Floor list: `app-v3/src/engine/KnownProblemPlugins.h`
- Three mechanisms: `PluginHost.cpp` (instrument sweep `25-52`,
  `applyKnownProblemBlacklist` `182-221`), launch call `App.cpp:36`
- UI Rescan + fallback: `app-v3/src/ui/TransportBar.cpp:338,394`
- Existing tests: `app-v3/tests/unit/engine/PluginScanner_test.cpp` (8 cases)
- JUCE reference: vendored
  `build/linux-dev/_deps/juce-src/extras/AudioPluginHost/Source/HostStartup.cpp`
  and `modules/juce_core/native/juce_SharedCode_posix.h:1181-1263`

### External References
- JUCE `KnownPluginList::CustomScanner` — https://docs.juce.com/master/classKnownPluginList_1_1CustomScanner.html
- JUCE `ChildProcessCoordinator` / `ChildProcessManager` — https://docs.juce.com/master/classChildProcessManager.html
- AUv3 `findAllTypesForFile` skipped off message thread — https://forum.juce.com/t/auv3-audiounitpluginformat-findalltypesforfile/47282
- Killed `ChildProcess` exit-code behavior — https://forum.juce.com/t/killed-childprocess-activeprocess-exit-code/61645
- Hardened-runtime child-process / library-validation — https://forum.juce.com/t/child-process-hardened-runtime-issue/48425
- WaveShell scan timeout — https://github.com/juce-framework/JUCE/issues/997

### Related Work
- Merge request **!14** — https://gitlab.com/void-realm-solutions/pedalboard-3/-/merge_requests/14
- Latest pipeline (green; macOS = infra timeout): 2504591448
- Branch: `feat/out-of-process-plugin-scanner`

### AI / Tooling Notes
- Plan authored via `ce:plan`, deepened via `deepen-plan` (Explore +
  best-practices + architecture + simplicity + learnings agents). No code written
  during planning. Conventions: `.h` headers, `pedalboard3::engine::` namespace,
  conventional commits, `clang-format-18` (AGENTS.md).
