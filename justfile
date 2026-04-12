# Pedalboard 3 — developer convenience wrappers.
# Use `just <target>` from repo root. Run `just --list` to see everything.

set shell := ["bash", "-cu"]
set dotenv-load := true

app_dir := "app-v3"

# Default: show the list.
default:
    @just --list

# ---------- Build ----------

# Configure + build the Linux dev preset.
build:
    cmake --preset linux-dev -S {{app_dir}}
    cmake --build --preset linux-dev

# Optimized release build.
release:
    cmake --preset linux-release -S {{app_dir}}
    cmake --build --preset linux-release

# Configure + build with RealtimeSanitizer (requires clang-18+).
rtsan:
    cmake --preset linux-rtsan -S {{app_dir}}
    cmake --build --preset linux-rtsan

# Fast configure with no JUCE/CLAP/Catch2 — used by lint CI.
lint-configure:
    cmake --preset lint-only -S {{app_dir}}

# ---------- Test ----------

# Run the unit test suite on the dev preset.
test:
    ctest --preset linux-dev

# Run the tests with RealtimeSanitizer enabled.
test-rtsan:
    ctest --preset linux-rtsan

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

# Launch the built hello-world app (Linux dev build).
run:
    build/linux-dev/Pedalboard3_artefacts/Debug/Pedalboard3

# ---------- Clean ----------

# Remove all CMake build directories.
clean:
    rm -rf build/

# ---------- Plan / docs ----------

# Open the current plan.
plan:
    $${EDITOR:-less} docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md
