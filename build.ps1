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
# .NET keeps a current directory of its own, and Set-Location does not touch it:
# it stays wherever the process started. Anything in here that reaches past the
# PowerShell providers -- [System.IO.File], for one -- resolves relative paths
# against this, not against the line above.
[System.Environment]::CurrentDirectory = $PSScriptRoot

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

# A Visual Studio update retires the versioned MSVC directory that CMakeCache
# recorded, and moving or reinstalling Ninja does the same to CMAKE_MAKE_PROGRAM.
# CMake's own message for this names the missing path but not the cause, and not
# that the cure is the whole directory: deleting CMakeCache.txt alone leaves
# object files built by the old toolset to be linked against the new one.
function Reset-StaleToolchain {
    $cache = Join-Path $buildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache)) { return }

    $entries = 'CMAKE_CXX_COMPILER', 'CMAKE_C_COMPILER', 'CMAKE_MAKE_PROGRAM', 'CMAKE_RC_COMPILER'
    $gone = foreach ($line in Get-Content -LiteralPath $cache) {
        if ($line -match '^(?<name>[A-Z_]+):(?:FILE)?PATH=(?<path>.+)$' -and
            $entries -contains $Matches.name -and
            -not (Test-Path -LiteralPath $Matches.path)) {
            "  $($Matches.name) -> $($Matches.path)"
        }
    }

    if (-not $gone) { return }

    Write-Host "The toolchain $buildDir was configured against is gone:" -ForegroundColor Yellow
    $gone | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
    Write-Host ("Usually a Visual Studio update. Removing $buildDir and configuring from " +
        'scratch, because a part-cleaned tree links new objects against the old toolset.') -ForegroundColor Yellow
    Remove-Item -Recurse -Force -LiteralPath $buildDir
}

# The drawn icon has to actually be inside the exe. JUCE's generated .rc names
# the icon file rather than containing it, so its text does not change when the
# icon does, and CMake's Ninja custom commands restat: the resource compiler can
# be skipped and the previous icon stay in the exe through a build that prints
# "Built:" exactly as usual. app-v3/src/CMakeLists.txt touches the .rc to stop
# that happening; this is what notices if it ever stops working.
#
# Local only, like the check above: CI configures and builds from an empty
# directory every time, so there is no previous icon for a build to keep.
function Assert-DrawnIconInExe {
    # Relative, like every other path here, and resolved to full paths before
    # they reach .NET: a relative path handed to [System.IO.File] is read
    # against .NET's own current directory rather than this script's location.
    $ico = 'app-v3/resources/icons/windows/Pedalboard3.ico'
    if (-not (Test-Path -LiteralPath $ico) -or -not (Test-Path -LiteralPath $appExe)) { return }

    $icoPath = (Resolve-Path -LiteralPath $ico).ProviderPath
    $exePath = (Resolve-Path -LiteralPath $appExe).ProviderPath

    try {
        $drawn = [System.IO.File]::ReadAllBytes($icoPath)
        $image = [System.IO.File]::ReadAllBytes($exePath)
    }
    catch {
        # The check could not run. A guard that cannot read its inputs has
        # found nothing wrong with the build, and must not be the reason one
        # fails.
        Write-Warning "Could not check the icon in the built exe: $($_.Exception.Message)"
        return
    }

    if ($drawn.Length -lt 22) { return }

    # The first image in the .ico. Its length and offset are the last two fields
    # of the directory entry that starts at byte 6, and the exe stores the image
    # data verbatim as an RT_ICON resource.
    $length = [BitConverter]::ToInt32($drawn, 14)
    $offset = [BitConverter]::ToInt32($drawn, 18)
    if ($length -le 0 -or ($offset + $length) -gt $drawn.Length) { return }

    # Latin1 maps every byte to the character of the same value, so this is a
    # byte search wearing a string's clothes -- and IndexOf is a great deal
    # faster than walking 30-odd MB of exe in PowerShell.
    $haystack = [System.Text.Encoding]::Latin1.GetString($image)
    $needle = [System.Text.Encoding]::Latin1.GetString($drawn, $offset, $length)

    if ($haystack.IndexOf($needle, [StringComparison]::Ordinal) -ge 0) { return }

    throw ("The build succeeded but the icon in $appExe is not the one in " +
        "app-v3/resources/icons/windows/Pedalboard3.ico. The resource compiler was skipped: " +
        "touch $buildDir/src/Pedalboard3_artefacts/JuceLibraryCode/Pedalboard3_resources.rc and " +
        "build again, or run './build.ps1 clean' first. See pb3_use_drawn_icon_file in " +
        'app-v3/src/CMakeLists.txt.')
}

function Invoke-CMake {
    param([string[]]$CMakeArgs)
    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake failed ($LASTEXITCODE): cmake $($CMakeArgs -join ' ')" }
}

function Invoke-Configure {
    Initialize-VsEnv
    Reset-StaleToolchain
    Invoke-CMake @('--preset', $preset, '-S', 'app-v3')
}

function Invoke-Build {
    Invoke-Configure
    Invoke-CMake @('--build', '--preset', $preset)
    Assert-DrawnIconInExe
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
