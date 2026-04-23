# Pedalboard 3 — developer convenience wrappers.
# Use `just <target>` from repo root. Run `just --list` to see everything.
#
# Host-OS aware: the default `build`/`test`/`run` pick the platform-
# appropriate preset (linux-dev on Linux, macos-dev on macOS). Explicit
# recipes are also exposed when you want to override.

set shell := ["bash", "-cu"]
set dotenv-load := true

app_dir := "app-v3"

# Resolved at parse time: which dev preset fits this host.
dev_preset := if os() == "macos" { "macos-dev" } else { "linux-dev" }

# Default: show the list.
default:
    @just --list

# ---------- Build ----------

# Configure + build the host's dev preset (linux-dev on Linux, macos-dev on macOS).
build:
    cmake --preset {{dev_preset}} -S {{app_dir}}
    cmake --build build/{{dev_preset}}

# Explicit Linux Debug build.
build-linux:
    cmake --preset linux-dev -S {{app_dir}}
    cmake --build build/linux-dev

# Explicit macOS Debug build (host architecture, not universal).
build-macos:
    cmake --preset macos-dev -S {{app_dir}}
    cmake --build build/macos-dev

# Optimized release (per host OS).
release:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ "$(uname)" = "Darwin" ]; then
        cmake --preset macos-universal -S {{app_dir}}
        cmake --build build/macos-universal
    else
        cmake --preset linux-release -S {{app_dir}}
        cmake --build build/linux-release
    fi

# Configure + build with RealtimeSanitizer (Linux + clang-18+ only).
rtsan:
    cmake --preset linux-rtsan -S {{app_dir}}
    cmake --build build/linux-rtsan

# Fast configure with no JUCE/CLAP/Catch2 — used by lint CI.
lint-configure:
    cmake --preset lint-only -S {{app_dir}}

# ---------- Test ----------

# Run the unit test suite on the host's dev preset.
test:
    ctest --test-dir build/{{dev_preset}} --output-on-failure

# Run tests with RealtimeSanitizer enabled (Linux).
test-rtsan:
    ctest --test-dir build/linux-rtsan --output-on-failure

# ---------- Code quality ----------

# clang-format every tracked C/C++ source file in-place.
format:
    find {{app_dir}}/src {{app_dir}}/tests \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
        -not -path '*/_deps/*' \
        -exec clang-format -i {} +

# Check formatting without modifying (CI lint).
format-check:
    find {{app_dir}}/src {{app_dir}}/tests \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
        -not -path '*/_deps/*' \
        -exec clang-format --dry-run --Werror {} +

# ---------- Run ----------

# Launch the built hello-world app from the host's dev preset.
# On macOS this uses `open` to launch the .app bundle (so window focus and
# LSUIElement-style behaviour work). On Linux it directly execs the binary.
run:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ "$(uname)" = "Darwin" ]; then
        APP="build/macos-dev/src/Pedalboard 3_artefacts/Debug/Pedalboard 3.app"
        [ -d "$APP" ] || { echo "Not built yet: $APP" >&2; exit 1; }
        open "$APP"
    else
        BIN="build/linux-dev/src/Pedalboard3_artefacts/Debug/Pedalboard 3"
        [ -x "$BIN" ] || { echo "Not built yet: $BIN" >&2; exit 1; }
        "$BIN"
    fi

# ---------- Clean ----------

# Remove all CMake build directories.
clean:
    rm -rf build/

# ---------- Plan / docs ----------

# Open the current plan.
plan:
    $${EDITOR:-less} docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md
