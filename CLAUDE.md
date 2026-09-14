<!-- Compatibility shim. See AGENTS.md for the real content. -->

See [AGENTS.md](AGENTS.md).

## Start here

1. Read `AGENTS.md`, especially the "Working agreement".
2. Read the newest file in `docs/status/`, currently `2026-09-14-handoff.md`.
   It covers where things stand, what's next and the open decisions.
3. Check the GitLab board: milestones M7 (public beta), M8 (v1.0.0) and 1.x.

## Shipping and tracking (silkops-harness)

- The shipping process for this repo is the `ship-mr` skill from the `silkops-harness`
  plugin: it opens or updates the GitLab merge request for the current branch and hands
  off to `watch-pipeline`. It never merges; a human merges. Use it in place of
  `ce-commit-push-pr`.
- Accepted code-review residuals are filed with `file-residuals`; plan units become
  milestone issues with `milestone-from-plan`.
- Environment facts and the operating loop live in the plugin: `facts/environment.json`
  and `references/runbook.md`. Add a fact there, not as a comment here.
- The harness is a Claude Code plugin installed from a local clone of
  `gitlab.com/void-realm-solutions/silkops-harness`: marketplace `silkops`, plugin
  `silkops-harness@silkops`.
  - Its `ops/` scripts (`issue-upsert.sh`, `mr-upsert.sh`, `issue-link.sh`, `note.sh`)
    live in that clone, so their paths differ per machine.
  - The handoff doc says how to set it up on a Mac.
- Issues filed outside a plan use `--plan user-requests` with one marker unit per item,
  such as `REL-PUBLIC-LINKS`. Re-running the upsert with the same plan and unit updates
  the issue in place, so never reuse a unit for a different item.
- The silkOps settings token lives in the pipeline (`SILKOPS_CI_TOKEN`); don't ask
  for a local one.
- On Windows git-bash, `ops/watch.sh` can die on the argument-length limit. When a
  watch prints nothing, list `glab api projects/<p>/pipelines/<id>/jobs` directly.
