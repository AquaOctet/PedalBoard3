---
title: Scope strict warnings off JUCE INTERFACE_SOURCES — unblock Linux CI log
type: fix
status: completed
date: 2026-04-26
---

# Scope strict warnings off JUCE INTERFACE_SOURCES — unblock Linux CI log

## Overview

GitLab CI on `feat/plugin-list-ui` (MR #7) fails because the `build:linux` job
log overflows GitLab's 4 MB limit. The truncation message (`Job's log exceeded
limit of 4194304 bytes. Job execution will continue but no more output will
be collected.`) appears at build step **[115/195]**, mid-compile, after roughly
24 000 `-Wold-style-cast` / `-Wnontrivial-memcall` / `-Wcast-align` warnings
emitted from JUCE's vendored sources (juce_core, juce_audio_basics,
juce_audio_devices, juce_graphics/Harfbuzz). The job's true exit status is
invisible because the log gets cut off; whether the failure is a real compile
error, a test failure, or a timeout, we cannot diagnose it until the noise is
gone.

Root cause: commit `8581ac2` (`refactor(cmake): scope pb3_warnings via
pb3_{engine,app}_core static libs`) was based on a wrong mental model of how
JUCE 8 modules propagate sources. JUCE modules are declared as INTERFACE
libraries with INTERFACE_SOURCES (`build/linux-dev/_deps/juce-src/extras/Build/CMake/JUCEModuleSupport.cmake:100-101`),
which means every consumer that links a JUCE module **PUBLIC** pulls JUCE's
.cpp files into its own compile unit list — and those .cpp files compile with
the consumer's PRIVATE compile flags. `compile_commands.json` confirms that
`juce_audio_basics.cpp` is compiled **four** times in our build, three of
which carry `pb3_warnings`:

```
src/CMakeFiles/pb3_app_core.dir/.../juce_audio_basics.cpp.o          # has pb3_warnings → noisy
src/engine/CMakeFiles/pb3_engine_core.dir/.../juce_audio_basics.cpp.o # has pb3_warnings → noisy
tests/CMakeFiles/pb3_smoke_tests.dir/.../juce_audio_basics.cpp.o      # has pb3_warnings → noisy
src/CMakeFiles/Pedalboard3.dir/.../juce_audio_basics.cpp.o            # clean (no pb3_warnings)
```

The previous refactor made the situation **worse** than before: pre-refactor,
JUCE was compiled noisy in one target (Pedalboard3) and one target only
(pb3_smoke_tests, which only pulls juce_audio_*); post-refactor, three of
four JUCE compiles are noisy.

## Problem Statement

A `target_link_libraries(my_target PUBLIC juce::juce_<module>)` adds JUCE
module sources to `my_target`'s compile list via INTERFACE_SOURCES. Any
PRIVATE compile option on `my_target` (including those provided by an
INTERFACE target like `pb3_warnings` linked PRIVATE) applies to those JUCE
sources. There is no way to scope target-level compile options to only "the
sources I wrote" once a JUCE module is in the link chain.

Concrete symptoms observable in the latest CI run (`logs/log.tx`):

- Single JUCE compile units emit thousands of warnings each: `juce_audio_basics.cpp` ≈ 5 700 lines of warning output, `juce_core.cpp` ≈ 7 500 lines, `juce_audio_devices.cpp` ≈ 3 400 lines, `juce_events.cpp` truncated mid-output.
- Total warnings >24 000 on Linux GCC 13.3.0 alone. macOS / Clang previously hit 24 033 (`logs/log.tx` from earlier macOS run before MR #7 went red).
- Log truncated at [115/195] — we cannot see whether `pb3_smoke_tests` even built, let alone whether tests passed.
- The format-host fix in `ce39f93` (`fix(cmake): enable VST3/LV2/AU hosting`) cannot be verified end-to-end until logs fit.

## Proposed Solution

Stop treating `pb3_warnings` as a per-target INTERFACE. Apply our strict
warning flags **per-source-file** to OUR `.cpp` files only, via
`set_source_files_properties(... PROPERTIES COMPILE_OPTIONS ...)`. JUCE's
INTERFACE_SOURCES, even when compiled into our targets, never carry our
flags because per-source COMPILE_OPTIONS attach to a specific TU, not to the
target.

Concretely:

1. Convert the existing `add_library(pb3_warnings INTERFACE)` into a CMake
   list variable `PB3_STRICT_WARNING_FLAGS` plus a helper function
   `pb3_apply_strict_warnings(<target> <source...>)` that iterates the
   source list and calls `set_source_files_properties` with the right
   `DIRECTORY` and `TARGET_DIRECTORY` scope.
2. Remove `pb3_warnings` from every `target_link_libraries` call.
3. From each `CMakeLists.txt` that owns sources, call
   `pb3_apply_strict_warnings(<target> <our .cpp files>)` listing only files
   we wrote (App.cpp, MainWindow.cpp, engine/*.cpp, main.cpp, every test
   `.cpp`). JUCE modules and Catch2 sources stay untouched.

Keep the rest of the build graph as-is:

- `pb3_engine_core` and `pb3_app_core` STATIC libs stay (reasonable
  architecture, not the cause of the regression).
- `pb3_plugin_formats` INTERFACE (commit `ce39f93`) stays — orthogonal,
  already correct.
- `juce::juce_recommended_warning_flags`, `juce_recommended_config_flags`,
  `juce_recommended_lto_flags`, `pb3_rtsafety` stay as INTERFACE links
  (JUCE authored / sanitizer flags — safe to apply broadly).

### Why per-source instead of alternatives

- **Mark JUCE includes SYSTEM** suppresses warnings emitted from JUCE
  *headers* used by our .cpp files. It does NOT silence warnings from JUCE
  *.cpp files* compiled as INTERFACE_SOURCES of our targets — and those are
  90 % of the noise. Insufficient on its own.
- **Drop `-Wold-style-cast`, `-Wconversion`, `-Wsign-conversion`,
  `-Wdouble-promotion` from `pb3_warnings`** kills the noise but loses
  warning coverage on our own code. Acceptable as a fallback if per-source
  scoping turns out to be too fiddly.
- **Revert the `pb3_engine_core` / `pb3_app_core` split** still leaves
  `pb3_warnings` on Pedalboard3 + pb3_smoke_tests, where JUCE compiles into
  both — the original noise source. Doesn't actually fix the bug.

## Technical Considerations

- `set_source_files_properties` with `COMPILE_OPTIONS` takes a
  semicolon-separated list. Build the list via `string(REPLACE " " ";" ...)`
  or pass a CMake `;`-list directly.
- For sources defined in a subdirectory, use the `DIRECTORY` argument so
  CMake resolves relative paths against that subdirectory. For sources whose
  TARGET lives in a different directory than their definition (e.g. a test
  target consuming files from `tests/unit/engine/`), use
  `TARGET_DIRECTORY <target>` to disambiguate.
- MSVC needs `/W4 /permissive-`; non-MSVC needs the GCC/Clang flag list.
  Mirror the existing `if(MSVC) ... else() ... endif()` branch from
  `app-v3/CMakeLists.txt:113-124` inside the helper.
- The helper must accept either a list of source paths or a glob; prefer
  explicit lists to avoid drift when files move.
- Verify after change: `compile_commands.json` should show our flags on our
  files only. `grep -E '\-Wold-style-cast' build/linux-dev/compile_commands.json`
  should match exclusively `*App.cpp.o.json`-style outputs and never
  `*juce_*.cpp.o`.

## System-Wide Impact

- **Interaction graph:** CMake configure → ninja generates per-file compile
  commands. `set_source_files_properties` adds COMPILE_OPTIONS to specific
  ninja rules, not to target-wide rules. JUCE INTERFACE_SOURCES still
  inherit target-level options (rtsafety, juce_recommended_*) but no
  longer pb3_warnings. No runtime-behaviour change — pure build hygiene.
- **Error propagation:** If `set_source_files_properties` syntax is wrong,
  configure fails immediately and noisily — caught by `cmake --preset
  linux-dev` locally before push. No silent failure mode.
- **State lifecycle risks:** None — build-system change.
- **API surface parity:** Apply the helper in `src/CMakeLists.txt`,
  `src/engine/CMakeLists.txt`, `tests/CMakeLists.txt`. main.cpp lives in
  the Pedalboard3 target and needs the helper called there explicitly so
  that the entry point keeps strict warnings even after we drop the
  target-level pb3_warnings link from Pedalboard3 (which it doesn't have
  today, but for consistency).
- **Integration test scenarios:**
  1. Build:linux job log fits within GitLab's 4 MB limit (well below — target <1 MB).
  2. Test:linux:unit runs all 47 tests and exits 0 (assumes ce39f93 works as designed).
  3. Local `just build-macos` no longer emits 24k+ Harfbuzz warnings.
  4. A deliberate violation in `App.cpp` (e.g. `int x = (int) 3.14f;`) is flagged at build time — proves strict warnings still apply to OUR code.
  5. JUCE module sources continue to compile cleanly with their own flags + JUCE's own recommended flags.

## Acceptance Criteria

- [ ] `app-v3/CMakeLists.txt` defines `PB3_STRICT_WARNING_FLAGS` (list) and `pb3_apply_strict_warnings(<target> <source...>)` helper.
- [ ] `pb3_warnings` INTERFACE library is removed (or kept as an alias mapping to the list, but no longer linked into any target).
- [ ] `app-v3/src/CMakeLists.txt`: pb3_app_core no longer links pb3_warnings; helper is called for `App.cpp`, `ui/MainWindow.cpp`. Pedalboard3 helper called for `main.cpp`.
- [ ] `app-v3/src/engine/CMakeLists.txt`: pb3_engine_core no longer links pb3_warnings; helper is called for every `.cpp` in the existing source list.
- [ ] `app-v3/tests/CMakeLists.txt`: pb3_smoke_tests no longer links pb3_warnings; helper is called for every entry in `PB3_TEST_SOURCES`.
- [ ] CI `build:linux` job log size drops below 1 MB.
- [ ] CI `test:linux:unit` job runs all 47 tests and reports them.
- [ ] `compile_commands.json` shows `-Wold-style-cast` on our .cpp files only, never on `juce_*.cpp` records.
- [ ] No regression in macOS dev build (≈zero JUCE warnings; warnings from our code remain visible).

## Dependencies & Risks

- **Hard dependency on ce39f93 (`pb3_plugin_formats`)** — must remain wired
  in. Without it, the format-registration tests (37, 44, 45) still fail.
- **Risk: helper signature drift** — if a future contributor adds a new
  `.cpp` file but forgets to call `pb3_apply_strict_warnings`, that file
  silently lacks strict warnings and bugs slip in. Mitigation: collocate
  the helper call right after each `target_sources` / `add_library` call
  in the same CMake file, so adding sources without warnings is visible
  in review.
- **Risk: `TARGET_DIRECTORY` isn't on every CMake generator** —
  `set_source_files_properties(... TARGET_DIRECTORY ...)` requires CMake
  ≥ 3.18. Our minimum is 3.28 (`app-v3/CMakeLists.txt:1`), so safe.
- **Risk: `${CMAKE_CURRENT_SOURCE_DIR}` shifts when called from a
  subdirectory.** Mitigation: have the helper resolve paths relative to a
  passed-in source directory, or always invoke from the same CMake file
  the sources live in.
- **Latent bug exposure**: once the log fits, we may discover the build
  was actually failing on a real compile error or a test failure that the
  truncation hid. Treat that as a follow-up — the warning fix is still the
  right first step.

## Implementation Sketch

`app-v3/CMakeLists.txt` (replace the `add_library(pb3_warnings INTERFACE)`
block at lines 112-124):

```cmake
# Strict warnings for OUR sources only. Applied per-source via the helper
# below — NOT as an INTERFACE library, because JUCE modules pipe their
# sources into every consumer's compile unit list via INTERFACE_SOURCES,
# and target-level compile options would flood the build with ~24k
# old-style-cast warnings from JUCE's vendored Harfbuzz / juce_core.
if(MSVC)
    set(PB3_STRICT_WARNING_FLAGS /W4 /permissive-)
else()
    set(PB3_STRICT_WARNING_FLAGS
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wconversion -Wsign-conversion
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2
    )
endif()

function(pb3_apply_strict_warnings target)
    foreach(src IN LISTS ARGN)
        set_source_files_properties("${src}"
            TARGET_DIRECTORY "${target}"
            PROPERTIES COMPILE_OPTIONS "${PB3_STRICT_WARNING_FLAGS}"
        )
    endforeach()
endfunction()
```

`app-v3/src/engine/CMakeLists.txt` — after `add_library(pb3_engine_core ...)`:

```cmake
pb3_apply_strict_warnings(pb3_engine_core
    AudioEngine.cpp
    AudioGraphRuntime.cpp
    BypassableInstance.cpp
    PluginHost.cpp
    processors/OutputSanitiser.cpp
    processors/TestToneProcessor.cpp
)
# remove pb3_warnings from target_link_libraries
```

`app-v3/src/CMakeLists.txt` — after `add_library(pb3_app_core ...)` and after
`juce_add_gui_app(Pedalboard3 ...)`:

```cmake
pb3_apply_strict_warnings(pb3_app_core
    App.cpp
    ui/MainWindow.cpp
)
pb3_apply_strict_warnings(Pedalboard3 main.cpp)
# remove pb3_warnings from both target_link_libraries calls
```

`app-v3/tests/CMakeLists.txt` — after `add_executable(pb3_smoke_tests ...)`:

```cmake
pb3_apply_strict_warnings(pb3_smoke_tests ${PB3_TEST_SOURCES})
# remove pb3_warnings from target_link_libraries
```

## Verification

Local (no full build needed per `feedback_build_validation`):

```bash
cmake --preset linux-dev -S app-v3
# Inspect a JUCE source compile command — should NOT contain -Wold-style-cast:
grep -A1 'juce_audio_basics.cpp' build/linux-dev/compile_commands.json | head
# Inspect our own source — SHOULD contain -Wold-style-cast:
grep -A1 '"file": ".*src/App.cpp"' build/linux-dev/compile_commands.json
```

CI:

- Push branch; observe `build:linux` log size and contents.
- Confirm `test:linux:unit` runs all 47 tests after build green.
- Confirm log fits under 1 MB (well below the 4 MB cap).

## Sources & References

- CI log (truncated): `logs/log.tx` — 26 519 lines, 4 MB cap reached at build step [115/195].
- Compile-command evidence: `build/linux-dev/compile_commands.json` shows juce_audio_basics.cpp compiled 4× across `pb3_engine_core`, `pb3_app_core`, `Pedalboard3`, `pb3_smoke_tests`.
- JUCE 8 module pattern: `build/linux-dev/_deps/juce-src/extras/Build/CMake/JUCEModuleSupport.cmake:100-101` — `add_library(${target} INTERFACE)` + `target_sources(${target} INTERFACE ${ARGN})`.
- Failed refactor: commit `8581ac2` `refactor(cmake): scope pb3_warnings via pb3_{engine,app}_core static libs`.
- Orthogonal format-host fix to KEEP: commit `ce39f93` `fix(cmake): enable VST3/LV2/AU hosting`.
- AGENTS.md build conventions: `AGENTS.md:97-110` (RT-safety comment marker, header style).
- User preference: `feedback_build_validation.md` — push to CI for full build validation; don't run `cmake --build` locally.
