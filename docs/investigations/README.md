# Investigations

Some work is a question, not a change: *would a different colour model read the
cube better? Is the solver's last-layer search worth rewriting? Does this
library do what its README says?* Most answers are "no", and that is a fine
outcome. This page is where a question lives while it is open, and what it
leaves behind when it is closed.

## The lane

| | A change | An investigation |
|---|---|---|
| Branch | `feature/*`, `bugfix/*`, ... | `spike/<name>` |
| Opens a PR into `develop` | yes | no need to; if it does, `Validate gitflow branch name` fails by design |
| CI runs on push | no (on the PR) | no |
| Can be merged | yes, through the queue | **never** |
| Ends as | commits on `develop` | a page in this folder, and an archive tag |

A spike is not held to the repository's rules: no tests-first, no coverage floor,
no formatting. It is held to one: **it does not merge**. Whatever deserves to
land is written again, properly, on a `feature/*` or `bugfix/*` branch, with its
tests; the spike is the evidence that it was worth it.

## Running one

```sh
git switch -c spike/colour-model-lab develop
git push -u origin spike/colour-model-lab
```

CI does not run on a push to a spike (it runs on `develop`, `main` and pull
requests), so it costs the queue nothing. To build it somewhere, or run a leg,
dispatch the workflow at the branch:

```sh
gh workflow run ci.yml --ref spike/colour-model-lab -f legs=windows
```

The package a `windows`, `macos` or `linux` leg builds is attached to the run as
an artifact, so you or another agent can install and try the spike without it
being on `develop`. Legs and what they cost: [CI & dependencies](../ci.md).

Several spikes can run at once, by different people or agents; the name is the
only coordination they need, so make it say what is being asked.

## Closing one

When the question is answered, one way or the other, add a page here named
`YYYY-MM-DD-<name>.md`:

```markdown
# <the question, as a question>

**Asked:** 2026-10-14. **Closed:** 2026-10-21. **By:** <who or which agent>.

## What was tried

Two or three paragraphs. What the spike did, on what data, with what measurement.

## What it found

The numbers, the picture, the thing that surprised you. Say how sure it is.

## Decision

Do it / do not / do a smaller version / revisit when X. If it is "do it", link
the issue or the `feature/*` branch that carries it.

## Where the code is

`archive/spike-<name>` (see below), at commit `abc1234`.
```

A page that says "no, and here is why" is worth as much as one that says yes: it
is the thing that stops the same idea being investigated again in three weeks.
Land it as a small docs-only pull request that also adds it to the list at the
end of this page (the site's navigation lists only this page); it needs no
changelog fragment.

## What happens to the branch

A spike whose tip commit is **30 days** old is archived by
[`spike-sweep.yml`](https://github.com/iainchesworthlabs/cubeulator/blob/develop/.github/workflows/spike-sweep.yml)
(Mondays): it is tagged `archive/spike-<name>` at its tip, then the branch is
deleted. Nothing is lost; to pick one up again:

```sh
git fetch origin 'refs/tags/archive/spike-*:refs/tags/archive/spike-*'
git switch -c spike/colour-model-lab archive/spike-colour-model-lab
```

A spike that is still being worked on stays alive by being committed to. One
that should never be swept is a question of whether it is a spike: if it is
going to be a change, it is a `feature/*`.

The precedent is the tag `archive/tracking-first`, which kept the whole
tracking-first pipeline when the project moved on from it.

## Not for secrets, or for people

A spike is a branch on a public repository like any other. It must not hold
camera frames that show a person, the corpus, keys or tokens. A recording of a
cube goes in the private corpus, and the spike's page says which clip it used by
name.

## Closed investigations

- [2026-10-14: does the scan catch a cube turned slowly about one axis and then
  another?](2026-10-14-scan-tumble.md) - no: six faults in a row, each fixed and
  tested; `cube_scan funnel` and `cube_tumble_sim` repeat it.
- [2026-10-15: why is a side near the edge of the square-on gate unread beside
  a much thinner one?](2026-10-15-square-on-dead-band.md) - not the thin side's
  blobs, which are never in the fit: the square grid `read_side` lays first; a
  second look places the blobs through their own fit.

- [2026-10-15: what does one frame of the scan cost in the browser, and does the
  verdict freeze the page?](2026-10-15-web-scan-frame-cost.md) - not freeze: the
  dearest judgement is 12 to 25 ms there, and was repeated on most frames until
  the assembly guard; `cube_scan cost` and the browser ceiling hold it.

- [2026-10-17: can the direct scan read a cube of N x N?](2026-10-17-nxn-scan-s0.md) -
  yes, on drawn cubes: a 5x5 and a 7x7 read at the 3x3's sticker size, 4,240 scans and no
  wrong cube; no recording of one exists, so not yet on a real one. `cube_scan_s0` repeats it.
