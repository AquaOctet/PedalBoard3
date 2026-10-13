#!/usr/bin/env pwsh
# Pedalboard 3 - the Windows packages, made from a release build (#88).
#
# In -OutDir it makes:
#   Pedalboard3-<version>-windows-x64.zip        the portable app: unzip anywhere and run
#   Pedalboard3-<version>-windows-x64-setup.exe  the installer (installer/windows/Pedalboard3.iss),
#                                                when Inno Setup's ISCC.exe is given or found
# Both ship the same files: the app, the scanner helper, the third-party
# licences and, when there are some, the release notes.
#
# The version is the build's own (pb3-version.txt, written by CMake). The
# release notes docs/releases/v<version>.md go in when they exist;
# release:gitlab requires them before it publishes.
#
# Usage (from a VS developer shell, so dumpbin can check the runtime):
#   ./tools/package.ps1 [-BuildDir build/windows-ci] [-OutDir build/packages] [-Iscc <path to ISCC.exe>]
# ./build.ps1 package does the release build first and then runs this.
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/windows-ci',
    [string]$OutDir = 'build/packages',
    [string]$Iscc = ''
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)

$versionFile = Join-Path $BuildDir 'pb3-version.txt'
if (-not (Test-Path -LiteralPath $versionFile)) {
    throw "No $versionFile. Configure and build $BuildDir first (./build.ps1 build -Config release)."
}
$version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
$notes = "docs/releases/v$version.md"

$bin = Join-Path $BuildDir 'src/Pedalboard3_artefacts/RelWithDebInfo'
$exes = @('Pedalboard 3.exe', 'pb3-scanner-helper.exe')
foreach ($exe in $exes) {
    if (-not (Test-Path -LiteralPath (Join-Path $bin $exe))) { throw "Missing $bin/$exe. Build $BuildDir first." }
}

# A clean PC has no VC++ runtime, so both exes must carry their own (the static
# runtime, set in app-v3/CMakeLists.txt). dumpbin comes with the VS developer shell.
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($dumpbin) {
    foreach ($exe in $exes) {
        $deps = & $dumpbin.Source /nologo /dependents (Join-Path $bin $exe) | Out-String
        # Every exe imports KERNEL32; without it dumpbin read nothing, and an
        # empty list would pass the check below without checking anything.
        if ($LASTEXITCODE -ne 0 -or $deps -notmatch '(?i)kernel32\.dll') {
            throw "dumpbin couldn't read the imports of $exe, so the runtime check can't be trusted:`n$deps"
        }
        $runtime = [regex]::Matches($deps, '(?im)^\s+((?:msvcp|vcruntime|concrt)\S*\.dll|api-ms-win-crt-\S+\.dll|ucrtbase\S*\.dll)\s*$') |
            ForEach-Object { $_.Groups[1].Value }
        if ($runtime) {
            throw "$exe needs the VC++ runtime DLLs ($($runtime -join ', ')), so it won't start on a clean PC. Release builds must link the runtime statically."
        }
    }
    Write-Host 'Runtime check: neither exe needs the VC++ runtime DLLs.'
} else {
    Write-Warning 'dumpbin is not on PATH, so the runtime check was skipped. Run this from a VS developer shell.'
}

# The portable folder: what the zip holds, and what the installer installs.
$name = "Pedalboard3-$version-windows-x64"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stage = Join-Path $OutDir $name
if (Test-Path -LiteralPath $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'licences') | Out-Null
foreach ($exe in $exes) { Copy-Item -LiteralPath (Join-Path $bin $exe) -Destination $stage }
# The app's own licence (AGPLv3) and what everything else is under. Shipping
# both is an AGPL obligation, not a courtesy.
Copy-Item -LiteralPath 'LICENSE' -Destination (Join-Path $stage 'LICENSE.txt')
Copy-Item -LiteralPath 'THIRD-PARTY-NOTICES.md' -Destination $stage
# Third-party licences: every embedded font's OFL, named after its folder.
foreach ($licence in Get-ChildItem -Path 'app-v3/resources/fonts/*/OFL.txt') {
    Copy-Item -LiteralPath $licence.FullName -Destination (Join-Path $stage "licences/$($licence.Directory.Name)-OFL.txt")
}
if (Test-Path -LiteralPath $notes) { Copy-Item -LiteralPath $notes -Destination (Join-Path $stage 'Release notes.md') }

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -Force -LiteralPath $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Portable: $zip" -ForegroundColor Green

# Debug symbols, beside the packages and never inside them. A crash dump can
# only be read with the PDBs from this exact build: they are matched by the
# PDB's GUID and age, which change on every link.
$symbols = Join-Path $OutDir "$name-symbols.zip"
if (Test-Path -LiteralPath $symbols) { Remove-Item -Force -LiteralPath $symbols }
$pdbs = @(Get-ChildItem -Path (Join-Path $bin '*.pdb'), (Join-Path $BuildDir 'src/pb3-scanner-helper_artefacts/RelWithDebInfo/*.pdb') -File -ErrorAction SilentlyContinue)
if ($pdbs.Count -gt 0) {
    Compress-Archive -Path $pdbs.FullName -DestinationPath $symbols
    Write-Host "Symbols: $symbols ($([math]::Round((Get-Item $symbols).Length / 1MB)) MB)" -ForegroundColor Green
} else {
    Write-Warning "No PDBs under $BuildDir, so no symbols archive. A RelWithDebInfo build writes them."
}

# The installer, from the same folder. Inno Setup doesn't put ISCC.exe on PATH,
# and winget installs it per-user rather than into Program Files, so look in
# all three places before giving up.
if (-not $Iscc) {
    $found = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($found) { $Iscc = $found.Source }
    else {
        $candidates = @(
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
            "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
            "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"   # winget's default
        )
        $Iscc = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
}
if ($Iscc) {
    $icon = Join-Path $BuildDir 'src/Pedalboard3_artefacts/JuceLibraryCode/icon.ico'
    & $Iscc /Qp "/DAppVersion=$version" "/DNumericVersion=$($version -replace '-.*$', '')" `
        "/DStageDir=$((Resolve-Path -LiteralPath $stage).Path)" `
        "/DIconFile=$((Resolve-Path -LiteralPath $icon).Path)" `
        "/DOutputDir=$((Resolve-Path -LiteralPath $OutDir).Path)" `
        'installer/windows/Pedalboard3.iss'
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)." }
    Write-Host "Installer: $(Join-Path $OutDir "$name-setup.exe")" -ForegroundColor Green
} else {
    Write-Warning 'Inno Setup (ISCC.exe) not found, so no installer. Install it (winget install JRSoftware.InnoSetup), pass -Iscc, or let CI build it.'
}
