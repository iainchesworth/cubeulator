# Branch protection and the merge queue

GitHub branch/repo security settings can't be expressed as a workflow file —
they're applied in **Settings → Rules → Rulesets** by someone with admin
rights on the repo. This page records what is configured and why, so the
settings can be rebuilt (or audited) from the page alone.

## The flow

Cubeulator uses a gitflow shape with a merge queue in front of `develop`:

```text
feature/* bugfix/* ──PR──▶ merge queue ──▶ develop ──release PR──▶ main
                                                       ▲
                                  release/* hotfix/* support/* ─┘
```

- **Topic branches** (`feature/*`, `bugfix/*`, `release/*`, `hotfix/*`,
  `support/*`, plus Dependabot's `dependabot/*`) open a pull request against
  `develop`, and a PR that is ready is put in the **merge queue**. Nobody
  pushes to `develop` directly, and nobody merges by hand.
- **`develop` → `main`** is a release step: a `develop` → `main` pull request
  merged with a *merge commit* (never squash or rebase, or the two branches
  diverge and the next release PR re-lists every commit). Hotfixes go to
  `main` from a `hotfix/*` branch and are merged back into `develop`
  through a PR from a `bugfix/*` or `support/*` branch.
- **Head branches are deleted** when their PR merges. A stacked PR whose base
  was the merged branch is retargeted to `develop` automatically.

## `develop-protection` (ruleset on `develop`)

- **Require a pull request before merging**, with 0 required approvals and
  stale approvals dismissed on push. Zero, not one: this is a solo-maintainer
  repo and GitHub doesn't count an author's own approval, so a required
  approval can only be met by bypassing it. Merge method: **merge commit**
  only, so `develop` keeps the `Merge feature/x: …` history gitflow expects.
- **Require status checks to pass**, "up to date" **off** — the merge queue
  makes each entry current server-side, without a rebase treadmill (see
  below). Which checks are required is staged, see
  [Required checks](#required-checks).
- **Block force pushes** and **restrict deletions**.
- The repository admin role can bypass **through a pull request only**
  (`bypass_mode: pull_request`): direct pushes to `develop` stay blocked even
  for an admin, and anything that skips a requirement is a deliberate
  `gh pr merge --admin` / "merge without waiting" that someone has to choose.

## `merge-queue-develop` (ruleset on `develop`)

One `merge_queue` rule: `merge_method: MERGE`, `grouping_strategy: ALLGREEN`,
`max_entries_to_build: 4`, `max_entries_to_merge: 5`,
`min_entries_to_merge: 1`, `min_entries_to_merge_wait_minutes: 1`,
`check_response_timeout_minutes: 180`.

A PR enters the queue once its own checks pass (or earlier, with auto-merge
enabled: `gh pr merge --auto --merge <n>`). GitHub then builds a temporary
`gh-readonly-queue/develop/pr-<n>-<sha>` branch — the PR merged onto the
current queue tip — re-runs the required checks on *that* tree, and merges it
only if they pass. A failed entry leaves the queue and the entries behind it
are rebuilt without it.

Two rules keep the queue honest:

- **Every workflow that produces a required check must also trigger on
  `merge_group`.** GitHub only runs workflows that opt in on the queue's
  temporary ref, so a required check from a workflow without that trigger
  never reports, and the entry sits until `check_response_timeout_minutes`
  expires. `branch-name-check.yml` and `dependency-review.yml` carry it today.
- **A workflow that produces no required check must *not* carry
  `merge_group`**, or every queue entry burns a full run of it for nothing.
  This is why `ci.yml` and `codeql.yml` don't have it yet.

A queue entry has no PR head name, so `Validate gitflow branch name` is
skipped there (the name was checked when the PR was opened); a skipped job
counts as a pass for a required check.

## Required checks

`develop`'s required set is **staged**, because `develop` is not yet green:
the build, test and static-analysis jobs in `ci.yml` and `codeql.yml` were not
run against `develop` between 17 July and 13 October 2026 (commits went in
without a PR), and they currently fail on `develop` itself — a required check
that fails on the base fails every PR. The rule is therefore:

> A check becomes required on `develop` when it is green on the `develop` tip
> **and** its workflow has `merge_group` added in the same change.

| Check | Workflow | Required on `develop` | Required on `main` |
| --- | --- | --- | --- |
| `Validate gitflow branch name` | `branch-name-check.yml` | yes | yes |
| `Scan dependency diff` | `dependency-review.yml` | yes | yes |
| `clang-format` | `ci.yml` | when green | yes |
| `clang-tidy` | `ci.yml` | when green | yes |
| `Linux (GCC 15)` | `ci.yml` | when green | yes |
| `macOS (AppleClang)` | `ci.yml` | when green | yes |
| `Windows (MSVC)` | `ci.yml` | when green | yes |
| `Coverage (Linux, GCC)` | `ci.yml` | when green | yes |
| `Analyze (C++)` | `codeql.yml` | when green | yes |

Promoting a check only ever tightens the ruleset. Android and iOS are
informational (`continue-on-error`) and are never required.

## `main-protection` (ruleset on `main`)

`main` only receives `develop`, `release/*`, `hotfix/*` and `support/*`
(`branch-name-check.yml` enforces the pairing). The ruleset requires a pull
request with 1 approval (dismiss stale), every check in the table above with
"up to date" **on**, conversation resolution, and blocks force pushes and
deletions. Releases are a deliberate human step, so they are the one place an
admin is expected to bypass through the PR.

## Repository settings

- **Allow auto-merge**: on — it is how a ready PR goes into the queue.
- **Automatically delete head branches**: on.
- **Allow merge commits / squash / rebase**: all on at the repo level; the
  rulesets above restrict `develop` to merge commits.

## Other scanners (visible-only)

`osv-scanner.yml`, `zizmor.yml`, and `scorecard.yml` upload SARIF to
**Security → Code scanning** but don't fail PR checks — triage their alerts
there rather than via a required status check. `scorecard.yml`'s
branch-protection sub-check scores more completely with a fine-grained PAT
(read-only, "Administration: read") added as a repo secret named
`SCORECARD_READ_TOKEN`; without it, that sub-check degrades gracefully
instead of failing.
