---
title: silkOps Factory Onboarding - Plan
type: chore
date: 2026-09-10
topic: silkops-factory-onboarding
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
execution: code
---

# silkOps Factory Onboarding - Plan

## Goal Capsule

- **Objective:** pedalboard-3's Linux CI jobs run inside an immutable, factory-built toolchain image instead of an image this repo builds for itself with kaniko, and the repo adopts the silkops-harness shipping loop (`ship-mr` → `watch-pipeline`, human merges).
- **Means:** a `juce-linux` catalog entry in `void-realm-solutions/ci-cd` (the silkOps image factory), a pinned tag in `.gitlab-ci.yml`, the harness snippet in `CLAUDE.md`, and the job-token allow-list entry on the factory that lets this project pull.
- **Product authority:** the factory's contract is `ci-cd/docs/image-catalog.md`; the harness loop is the silkops-harness plugin's `references/runbook.md`. This plan owns only pedalboard-3's consumer side plus the one catalog entry it needs.
- **Stop conditions:** never merge from a session; never write the factory allow-list without the operator confirming the shown diff; if the factory refuses to build the image (tag exists, checksum mismatch), fix the catalog entry rather than retrying.

## Key Decisions

- KD1. **One generic `juce-linux` image, not a `pedalboard-ci` image.** The recipe is the old `ci/docker/Dockerfile.linux` with every version moved to `image.yml` build args. Nothing in it is pedalboard-specific, so the name leaves room for other JUCE consumers in the group.
- KD2. **Tag from the LLVM major (`18-r<N>`).** clang's major is the identity-defining pin for a C++ toolchain; CMake and Ninja versions live in the record.
- KD3. **Lint runs in the factory image too.** It already carries clang-format-18 and just, so the stock-ubuntu + apt-install lint job goes away.
- KD4. **The apt layer stays unpinned** (governed by `base.digest` + the noble archive, drift caught by the identity hash), the same reasoning `sec-tools` documents. The two downloaded tarballs (CMake, Ninja) are sha256-pinned.

## Implementation Units

### U1. The `juce-linux` factory image

- **Goal:** The factory publishes `registry.gitlab.com/void-realm-solutions/ci-cd/juce-linux:18-r1` with a build record.
- **Requirements:** KD1, KD2, KD4
- **Dependencies:** none
- **Files:**
  - `ci-cd: images/juce-linux/image.yml` (create)
  - `ci-cd: images/juce-linux/Dockerfile` (create)
  - `ci-cd: tasks/image-factory/.image-factory.yml` (add `build-juce-linux`)
  - `ci-cd: images/README.md` (catalog row)
- **Verification:** the ci-cd MR pipeline's `build-juce-linux` dry run builds, scans, and renders a record; on merge the tag `18-r1` appears in the registry.

### U2. Pin the factory image and adopt the harness

- **Goal:** pedalboard-3's Linux jobs pull the pinned factory tag; the kaniko image-build job and its Dockerfile are gone; the repo declares `ship-mr` as its shipping process.
- **Requirements:** KD3
- **Dependencies:** U1
- **Files:**
  - `.gitlab-ci.yml` (pin `CI_LINUX_IMAGE`, drop `pre-build` stage, `build:image:linux`, `.rules-ci-image`, `.lint-setup`)
  - `ci/docker/Dockerfile.linux` (delete — recipe now lives in ci-cd)
  - `CLAUDE.md` (harness snippet)
  - `AGENTS.md` (CI description, human-gate wording)
  - `docs/plans/2026-09-10-001-chore-silkops-factory-onboarding-plan.md` (this plan)
- **Verification:** the MR's `lint:format`, `build:linux`, and `test:linux:unit` jobs pull the image and pass; no `apt-get` appears in any Linux job trace.

### U3. Allow-list pedalboard-3 on the factory

- **Goal:** ci-cd's job-token allow-list names `void-realm-solutions/pedalboard-3` so the pull in U2 succeeds.
- **Requirements:** the `consumer-onboarding` skill's Phase A, with the operator's `SILKOPS_SETTINGS_TOKEN` exported and a confirmation of the shown diff.
- **Dependencies:** none (can land before or after U1; U2's pipeline needs both)
- **Files:**
  - none (a GitLab settings write via `ops/allowlist.sh --project void-realm-solutions/ci-cd add --consumer void-realm-solutions/pedalboard-3`)
- **Verification:** `ops/allowlist.sh --project void-realm-solutions/ci-cd get` lists pedalboard-3; a `pull access denied` within seconds of a Linux job start means this did not land (fact `pull-access-denied`).

## Sequencing

U1 and U3 are independent. U2's pipeline goes green only once U1 has published `18-r1` and U3 has landed, so open the U2 MR after U1 merges (or expect its first pipeline to fail on the pull and re-run it).

## Definition of Done

- `18-r1` published with a record in `ci-cd/images/juce-linux/builds/`.
- pedalboard-3 `main` carries the pin, the snippet, and no Dockerfile; its Linux jobs pass on the factory image.
- pedalboard-3 is on the factory allow-list.
