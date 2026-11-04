# Contributing to Pedalboard 3

Thanks for looking. Pedalboard 3 is open source under the
[AGPLv3](LICENSE), developed on GitLab at
<https://gitlab.com/void-realm-solutions/pedalboard-3> and mirrored to GitHub.
GitLab is where CI runs, so merge requests go there; issues are welcome in
either place.

## Sign your commits (DCO)

Every commit needs a `Signed-off-by` line. There is no CLA to sign and no
copyright to assign — the sign-off is a statement that you wrote the change, or
otherwise have the right to submit it, under the project's licence. Git adds the
line for you:

```sh
git commit -s -m "fix(engine): stop the metronome drifting at 44.1 kHz"
```

That appends `Signed-off-by: Your Name <your@email>`, using your `user.name` and
`user.email`. Amend an existing commit with `git commit -s --amend`, or a whole
branch with `git rebase --signoff main`.

The line means you agree to the Developer Certificate of Origin 1.1, reproduced
in full below.

<details>
<summary>Developer Certificate of Origin 1.1</summary>

```text
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same license (unless I am permitted to submit
    under a different license), as indicated in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

</details>

### Why a DCO and not a CLA

A CLA would let the maintainers relicense your contribution, including into a
closed commercial build. A DCO does not: your code stays under the AGPLv3 like
everyone else's. Paid features are built as separate components rather than as
code inside this application, so there is nothing here that needs the broader
rights a CLA grants. If that ever has to change, it will be asked for openly and
in advance, never retroactively.

## Before you start

- **Open an issue first** for anything larger than a bug fix. The roadmap is
  tracked on GitLab milestones (M7 public beta, M8 v1.0.0, 1.x deferred), and a
  change that cuts across the plan is better discussed before it is written.
- **Read [`AGENTS.md`](AGENTS.md).** It is written for coding agents, but it is
  the honest description of how this repo is built and tested: the stack, the
  conventions, the cross-platform gotchas, and what is verified how.

## Building and testing

JUCE 8, CMake 3.25+, C++20, Catch2. Windows is the primary target; macOS and
Linux build and are covered by CI.

### Windows

Any PowerShell — the script finds the Visual Studio developer environment
itself. You need VS with the C++ workload.

```powershell
./build.ps1 build                  # Debug
./build.ps1 build -Config release  # RelWithDebInfo
./build.ps1 test                   # build, then unit tests
./build.ps1 run
./build.ps1 snapshots              # every window, menu and dialog to build/ui-snapshots
./build.ps1 package                # release build + portable zip (+ installer, with Inno Setup)
./build.ps1 clean
```

The screenshots in the README came out of `snapshots`, which is also how the
UI gets reviewed without clicking through it.

### macOS and Linux

`just` picks the host's preset. The `Makefile` has the same targets.

```bash
just build
just test
just run
just release
```

Or CMake directly:

```bash
cmake --preset linux-dev -S app-v3
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

macOS needs Xcode's command-line tools (`xcode-select --install`) and
`brew install cmake ninja just pkg-config`.

Linux needs CMake 3.25+, Ninja, a C++20 compiler (CI uses clang-18) and
JUCE 8's X11 and audio libraries:

```
build-essential pkg-config clang-18
libasound2-dev libjack-jackd2-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
libxext-dev libxcomposite-dev libxrender-dev
libfreetype-dev libfontconfig1-dev
libgl-dev libglu1-mesa-dev mesa-common-dev
```

It builds with `JUCE_USE_CURL=0` and `JUCE_WEB_BROWSER=0`, so no libcurl or
WebKit needed.

### Other plugin formats

CLAP and VST2 are build flags (`PB3_ENABLE_CLAP`, `PB3_ENABLE_VST2`), off in
every preset, so no release carries either. VST2 also needs a copy of the SDK
— Steinberg stopped giving it out, so there is nothing to download; drop yours
at `app-v3/vst2_sdk/` and configure with `-DPB3_ENABLE_VST2=ON`. Without it
the configure stops and says so.

### What CI requires

A merge request needs the test suite passing and `clang-format` clean. CI runs
both on every MR, across Windows, macOS and Linux, and the release jobs are
required to pass before a release is cut.

## Where things are

```
app-v3/src/           engine/, session/, control/, ui/, platform/, tools/
app-v3/tests/         Catch2 — unit/ mirrors src/
docs/                 plans/, design/, releases/, status/, screenshots/
installer/windows/    Inno Setup script
tools/                packaging scripts
legacy/               Pedalboard2's end-user docs, for reference
source code/          original Pedalboard2 source — READ-ONLY, never built
vercel-ui-demo/       the UI mockup the design follows
AGENTS.md             conventions, including for coding agents
```

## Conventions

- **Commits** follow Conventional Commits: `type(scope): summary`, written in the
  imperative. `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `ci`.
- **Code** matches the surrounding style; `.clang-format` settles the rest.
  Comments explain *why*, not *what*.
- **Tests** go with the change. Anything in the audio thread needs a test that
  would fail without the fix.
- **One MR, one subject.** A description that says what changed and what proves
  it is worth more than a long diff.

## Reporting bugs

Say which platform and version (the About window shows both), what you did, what
happened, and what you expected. If the app logs anything, the log is at
`logs/Pedalboard 3.log` beside the settings file, and its first lines identify
the exact build — attach it.
