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

The full build instructions are in [`README.md`](README.md). The short version:

```sh
# Windows
./build.ps1 test

# Linux and macOS
cmake --preset linux-debug && ctest --preset linux-debug
```

A merge request needs the test suite passing and `clang-format` clean. CI runs
both on every MR, across Windows, macOS and Linux, and the release jobs are
required to pass before a release is cut.

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
