#!/usr/bin/env bash
# Pedalboard 3 - the macOS and Linux packages, made from a release build (#88).
#
#   macOS: Pedalboard3-<version>-macos-universal.zip  the .app (the scanner helper is inside it)
#   Linux: Pedalboard3-<version>-linux-x64.tar.gz     the app and the scanner helper
# Each also holds the third-party licences and, when there are some, the release
# notes. The Windows packages are tools/package.ps1.
#
# The version is the build's own (pb3-version.txt, written by CMake). The
# release notes docs/releases/v<version>.md go in when they exist;
# release:gitlab requires them before it publishes.
#
# Usage: tools/package.sh <build-dir> [<out-dir>]    e.g. tools/package.sh build/linux-release
set -euo pipefail
cd "$(dirname "$0")/.."

build="${1:?usage: tools/package.sh <build-dir> [<out-dir>]}"
out="${2:-build/packages}"

if [ ! -f "$build/pb3-version.txt" ]; then
    echo "No $build/pb3-version.txt. Configure and build $build first." >&2
    exit 1
fi
version="$(tr -d '[:space:]' < "$build/pb3-version.txt")"
notes="docs/releases/v$version.md"

bin="$build/src/Pedalboard3_artefacts/RelWithDebInfo"
case "$(uname -s)" in
    Darwin) platform="macos-universal" ;;
    Linux)
        case "$(uname -m)" in
            x86_64) platform="linux-x64" ;;
            aarch64 | arm64) platform="linux-arm64" ;;
            *) platform="linux-$(uname -m)" ;;
        esac
        ;;
    *)
        echo "tools/package.sh makes the macOS and Linux packages; on Windows use tools/package.ps1." >&2
        exit 1
        ;;
esac

name="Pedalboard3-$version-$platform"
stage="$out/$name"
rm -rf "$stage"
mkdir -p "$stage/licences"

if [ "$platform" = "macos-universal" ]; then
    app="$bin/Pedalboard 3.app"
    [ -x "$app/Contents/MacOS/pb3-scanner-helper" ] || { echo "Missing the helper inside $app. Build $build first." >&2; exit 1; }
    echo "Architectures: $(lipo -archs "$app/Contents/MacOS/Pedalboard 3")"
    ditto "$app" "$stage/Pedalboard 3.app"
else
    for exe in "Pedalboard 3" "pb3-scanner-helper"; do
        [ -x "$bin/$exe" ] || { echo "Missing $bin/$exe. Build $build first." >&2; exit 1; }
        cp "$bin/$exe" "$stage/"
    done
fi

# The app's own licence (AGPLv3) and what everything else is under. Shipping
# both is an AGPL obligation, not a courtesy.
cp LICENSE "$stage/LICENSE.txt"
cp THIRD-PARTY-NOTICES.md "$stage/"
# Third-party licences: every embedded font's OFL, named after its folder.
for licence in app-v3/resources/fonts/*/OFL.txt; do
    cp "$licence" "$stage/licences/$(basename "$(dirname "$licence")")-OFL.txt"
done
if [ -f "$notes" ]; then cp "$notes" "$stage/Release notes.md"; fi

if [ "$platform" = "macos-universal" ]; then
    rm -f "$out/$name.zip"
    ditto -c -k --keepParent "$stage" "$out/$name.zip"
    echo "Package: $out/$name.zip"
else
    rm -f "$out/$name.tar.gz"
    tar -C "$out" -czf "$out/$name.tar.gz" "$name"
    echo "Package: $out/$name.tar.gz"
fi

# Debug symbols, beside the package and never inside it. A crash can only be
# read with the symbols from this exact build: they are matched by the dSYM's
# UUID or the ELF build id, which change on every link.
symbols="$out/$name-symbols"
rm -rf "$symbols"
mkdir -p "$symbols"
if [ "$platform" = "macos-universal" ]; then
    if command -v dsymutil >/dev/null 2>&1; then
        dsymutil "$stage/Pedalboard 3.app/Contents/MacOS/Pedalboard 3" -o "$symbols/Pedalboard 3.dSYM"
        dsymutil "$stage/Pedalboard 3.app/Contents/MacOS/pb3-scanner-helper" -o "$symbols/pb3-scanner-helper.dSYM"
    else
        echo "dsymutil not found, so no symbols archive." >&2
    fi
elif command -v objcopy >/dev/null 2>&1; then
    for exe in "Pedalboard 3" "pb3-scanner-helper"; do
        objcopy --only-keep-debug "$bin/$exe" "$symbols/$exe.debug"
    done
else
    echo "objcopy not found, so the unstripped binaries go in as they are." >&2
    for exe in "Pedalboard 3" "pb3-scanner-helper"; do cp "$bin/$exe" "$symbols/"; done
fi

if [ -n "$(ls -A "$symbols" 2>/dev/null)" ]; then
    rm -f "$out/$name-symbols.tar.gz"
    tar -C "$out" -czf "$out/$name-symbols.tar.gz" "$name-symbols"
    echo "Symbols: $out/$name-symbols.tar.gz"
fi
rm -rf "$symbols"
