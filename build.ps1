#!/usr/bin/env pwsh
# Pedalboard 3 - local build/test wrapper for Windows.
#
# One command, no manual "x64 Native Tools" prompt: this auto-loads the Visual
# Studio developer environment (so cl.exe + Ninja land on PATH) before invoking
# CMake. Mirrors the Makefile/justfile used on Linux/macOS.
#
# Usage (from the repo root, in any PowerShell):
#   ./build.ps1                      # configure + build  (Debug / windows-dev)
#   ./build.ps1 build                # same
#   ./build.ps1 build -Config release   # optimized       (RelWithDebInfo / windows-ci)
#   ./build.ps1 test                 # build, then run the unit tests
#   ./build.ps1 run                  # launch the built app
#   ./build.ps1 snapshots            # render every menu and dialog to build/ui-snapshots
#   ./build.ps1 package              # release build, then the portable zip in build/packages
#                                    # (and the installer, when Inno Setup is installed)
#   ./build.ps1 configure            # configure only
#   ./build.ps1 clean                # remove this config's build dir
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'configure', 'test', 'run', 'clean', 'snapshots', 'package')]
    [string]$Task = 'build',

    # 'debug' -> windows-dev (Debug, fast iteration; needs VS to run, which you have)
    # 'release' -> windows-ci (RelWithDebInfo, runs on a clean machine)
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug'
)

$ErrorActionPreference = 'Stop'
# The script lives at the repo root; run everything from there regardless of CWD.
Set-Location -LiteralPath $PSScriptRoot

# Packages always come from the release build.
if ($Task -eq 'package') { $Config = 'release' }
$preset = if ($Config -eq 'release') { 'windows-ci' } else { 'windows-dev' }
$cmakeConfig = if ($Config -eq 'release') { 'RelWithDebInfo' } else { 'Debug' }
$buildDir = "build/$preset"
$appExe = "$buildDir/src/Pedalboard3_artefacts/$cmakeConfig/Pedalboard 3.exe"

# Put the MSVC toolchain (cl.exe) and Ninja on PATH if they aren't already, by
# entering the Visual Studio developer environment in THIS session. No-op when
# the script is already run from a VS developer shell.
function Initialize-VsEnv {
    if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and
        (Get-Command ninja.exe -ErrorAction SilentlyContinue)) {
        return
    }

    Write-Host 'Loading the Visual Studio developer environment (x64)...' -ForegroundColor Cyan

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere not found at $vswhere. Install Visual Studio with the 'Desktop development with C++' workload."
    }

    $vsPath = & $vswhere -latest -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) {
        throw "No Visual Studio install with the C++ toolset was found. Add 'Desktop development with C++' in the VS Installer."
    }

    $devShell = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path -LiteralPath $devShell)) {
        throw "VS DevShell module not found at $devShell."
    }

    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64' | Out-Null
    # Enter-VsDevShell changes the working directory; restore it.
    Set-Location -LiteralPath $PSScriptRoot

    if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) {
        throw ("Ninja is still not on PATH after loading the VS environment. Add the " +
            "'C++ CMake tools for Windows' component in the VS Installer (it bundles Ninja), " +
            "or use the Visual Studio generator: cmake -S app-v3 -B build\vs -G 'Visual Studio 17 2022' -A x64.")
    }
}

function Invoke-CMake {
    param([string[]]$CMakeArgs)
    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake failed ($LASTEXITCODE): cmake $($CMakeArgs -join ' ')" }
}

function Invoke-Configure {
    Initialize-VsEnv
    Invoke-CMake @('--preset', $preset, '-S', 'app-v3')
}

function Invoke-Build {
    Invoke-Configure
    Invoke-CMake @('--build', '--preset', $preset)
}

switch ($Task) {
    'configure' { Invoke-Configure }
    'build' { Invoke-Build; Write-Host "Built: $appExe" -ForegroundColor Green }
    'test' {
        Invoke-Build
        & ctest --test-dir $buildDir --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "ctest failed ($LASTEXITCODE)" }
    }
    'run' {
        if (-not (Test-Path -LiteralPath $appExe)) {
            throw "Not built yet: $appExe  (run './build.ps1 build' first)"
        }
        & $appExe
    }
    'snapshots' {
        # Every menu, dialog and alert, rendered to PNG through the app's own
        # LookAndFeel: the UI chrome plan's before-and-after images (U1).
        Invoke-Build
        $tool = "$buildDir/src/pb3-ui-snapshots_artefacts/$cmakeConfig/pb3-ui-snapshots.exe"
        & $tool 'build/ui-snapshots'
        if ($LASTEXITCODE -ne 0) { throw "pb3-ui-snapshots failed ($LASTEXITCODE)" }
    }
    'package' {
        # The Windows packages (#88): the portable zip, and the installer when
        # Inno Setup's ISCC.exe is found. CI builds both on every release.
        Invoke-Build
        & ./tools/package.ps1 -BuildDir $buildDir
    }
    'clean' {
        if (Test-Path -LiteralPath $buildDir) { Remove-Item -Recurse -Force -LiteralPath $buildDir }
        Write-Host "Removed $buildDir"
    }
}
