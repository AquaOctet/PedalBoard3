#!/usr/bin/env bash
# Pedalboard 3 - the macOS and Linux packages, made from a release build (#88).
#
#   macOS: Pedalboard3-<version>-macos-universal.zip  the .app (the scanner helper is inside it)
#   Linux: Pedalboard3-<version>-linux-x64.tar.gz     the app and the scanner helper
# Each also holds the third-party licences and, when there are some, the release
# notes. The Windows packages are tools/package.ps1.
#
# The version is the build's own (pb3-version.txt, written by CMake). In a tag
# pipeline the tag must be v<version>, and the release notes
# docs/releases/v<version>.md must exist.
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
if [ -n "${CI_COMMIT_TAG:-}" ]; then
    if [ "$CI_COMMIT_TAG" != "v$version" ]; then
        echo "The tag $CI_COMMIT_TAG doesn't match the build's version, $version. Tag v$version, or change the version in app-v3/CMakeLists.txt." >&2
        exit 1
    fi
    if [ ! -f "$notes" ]; then
        echo "A release needs its notes: $notes is missing." >&2
        exit 1
    fi
fi

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
