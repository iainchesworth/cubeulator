# Branch protection for `main`

GitHub branch/repo security settings can't be expressed as a workflow file —
they're applied in **Settings → Rules → Rulesets** (or the classic
**Settings → Branches**) by someone with admin rights on the repo. Configure
a ruleset for `main` with:

- **Require a pull request before merging**
  - Require at least 1 approval
  - Dismiss stale approvals when new commits are pushed
- **Require status checks to pass before merging** (enable "Require branches
  to be up to date" too), selecting:
  - `Validate gitflow branch name` (from `branch-name-check.yml`)
  - `Analyze (C++)` (from `codeql.yml`)
  - the `build-and-test` matrix jobs (from `ci.yml`)
  - `Coverage (Linux, GCC)` (from `ci.yml`) — fails below 80% line/branch
    coverage
  - `Scan dependency diff` (from `dependency-review.yml`) — fails on a
    moderate-or-worse known vulnerability newly introduced by the PR
- **Require conversation resolution before merging**
- **Do not allow bypassing the above settings**
- **Restrict who can push to matching branches** — only allow merges via PR
- **Block force pushes**
- **Restrict deletions**

`main` only receives merges from `release/*`, `hotfix/*`, and `support/*`
branches (or `develop`) under this project's gitflow model — GitHub
rulesets support restricting which branches may target `main` directly;
`branch-name-check.yml` enforces the same pairing as a required status check
either way.

## Other scanners (visible-only)

`osv-scanner.yml`, `zizmor.yml`, and `scorecard.yml` upload SARIF to
**Security → Code scanning** but don't fail PR checks — triage their alerts
there rather than via a required status check. `scorecard.yml`'s
branch-protection sub-check scores more completely with a fine-grained PAT
(read-only, "Administration: read") added as a repo secret named
`SCORECARD_READ_TOKEN`; without it, that sub-check degrades gracefully
instead of failing.
