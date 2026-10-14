#!/usr/bin/env python3
"""Is a commit green on every workflow the README has a badge for?

    check_health.py                         # develop's tip
    check_health.py --ref <branch-or-sha>   # another commit
    check_health.py --through-merge         # a merge commit: judge what it merged
    check_health.py --scorecard             # after the release: has Scorecard published?

The README shows a badge for each of CI, CodeQL, OSV-Scanner and Zizmor, and on
`main` each shows the latest run there. A release that reaches `main` with one
of those red (or never run) puts a red badge on the project's front page, and
"the required checks passed on the pull request" does not rule that out: the
nine checks `main` requires are the pull request's own run of the build, test,
coverage, clang and CodeQL legs; they do not include OSV-Scanner or Zizmor, or
the push runs on `develop` that the badges on `main` come to show.

So this reads the workflows the README has badges for (it parses README.md, so
a badge added there is held to the same rule), finds each one's run on the
commit, and refuses unless every one finished successfully. It also lists any
other check run on the commit that is red or still running.

Which run counts: the newest `push` run of the workflow on that commit (what the
badges show). Where there is none (a hotfix branch is built as a pull request,
not pushed) the newest run of any event. `--event` pins it.

    --wait MINUTES    a workflow that has not finished is not a failure: with
                      this, wait for it (polling every --interval seconds, 60
                      by default) up to MINUTES before judging. A run that has
                      failed ends the wait at once. Without it the answer is
                      immediate, and "has not finished" is a failure.

    --through-merge   if the commit is a merge commit, judge its second parent.
                      The release PR is merged into `main` with a merge commit
                      whose second parent is the `develop` tip that was tested;
                      the merge commit itself has only just been pushed and its
                      own runs are hours from done.

    --scorecard       reads api.scorecard.dev for this repository. The badge
                      says "invalid repo path" until Scorecard has run on
                      `main` (it publishes only from the default branch).

Exit status: 0 green, 1 not green (or no Scorecard result yet), 2 could not ask.
Needs `gh`, authenticated (a workflow's GITHUB_TOKEN is enough, with `actions`,
`checks` and `contents` read). Standard library only.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[2]

_BADGE_RE = re.compile(r"actions/workflows/([A-Za-z0-9._-]+\.ya?ml)/badge\.svg")


class HealthError(RuntimeError):
    """The state could not be read."""


def badge_workflows(readme: str) -> list[str]:
    """The workflow files README.md has a status badge for, in the order shown."""
    seen: list[str] = []
    for name in _BADGE_RE.findall(readme):
        if name not in seen:
            seen.append(name)
    return seen


def pick_run(runs: list[dict], event: str) -> dict | None:
    """The run that speaks for a workflow on a commit.

    `event` is 'auto' (the newest push run, else the newest of any event),
    'any', or one event name.
    """
    ordered = sorted(runs, key=lambda r: r.get("created_at", ""), reverse=True)
    if event == "auto":
        pushed = [r for r in ordered if r.get("event") == "push"]
        return (pushed or ordered or [None])[0]
    if event == "any":
        return ordered[0] if ordered else None
    matching = [r for r in ordered if r.get("event") == event]
    return matching[0] if matching else None


def verdict(run: dict | None) -> str:
    """green, red, running or none for one workflow run."""
    if run is None:
        return "none"
    if run.get("status") != "completed":
        return "running"
    # Skipped and neutral prove nothing, so they are not green either.
    return "green" if run.get("conclusion") == "success" else "red"


@dataclass
class Row:
    workflow: str
    state: str  # green, red, running, none
    detail: str = ""  # the conclusion, or the event of a run in progress
    url: str = ""


@dataclass
class Report:
    repo: str
    sha: str
    subject: str
    rows: list[Row]
    checks_green: int = 0
    checks_red: list[str] = field(default_factory=list)
    checks_running: list[str] = field(default_factory=list)  # informational
    partial: str = ""
    note: str = ""
    gave_up_after: int = 0  # minutes waited for workflows that never finished

    @property
    def ok(self) -> bool:
        return not self.problems()

    @property
    def unfinished(self) -> bool:
        """A workflow that has not finished (or has not started): waiting may change the answer."""
        return any(r.state in ("running", "none") for r in self.rows)

    @property
    def failed(self) -> bool:
        """Something has failed: waiting will not make it not so."""
        return any(r.state == "red" for r in self.rows) or bool(self.checks_red)

    def problems(self) -> list[str]:
        found: list[str] = []
        short = self.sha[:8]
        for row in self.rows:
            if row.state == "red" and row.detail == "cancelled":
                found.append(
                    f"{row.workflow} was cancelled on {short} ({row.url}): a newer push replaces a "
                    "push run that is still waiting. Re-run it, or release a commit it did finish on"
                )
            elif row.state == "red":
                found.append(f"{row.workflow} is {row.detail or 'not green'} on {short} ({row.url})")
            elif row.state == "running" and self.gave_up_after:
                found.append(
                    f"{row.workflow} had still not finished on {short} after {self.gave_up_after} "
                    f"minutes ({row.url})"
                )
            elif row.state == "running":
                found.append(f"{row.workflow} has not finished on {short}: wait for it ({row.url})")
            elif row.state == "none":
                found.append(
                    f"{row.workflow} has no run on {short}: it has not run on this commit "
                    "(a newer push may have cancelled it, or it does not run for this branch)"
                )
        found.extend(f"check run failed: {name}" for name in self.checks_red)
        if self.partial:
            found.append(self.partial)
        return found

    def markdown(self) -> str:
        icon = {"green": "✅", "red": "❌", "running": "⏳", "none": "❔"}
        lines = [
            f"### Is `{self.sha[:8]}` green?",
            "",
            f"{self.subject}",
            "",
            "| Workflow | | |",
            "|---|---|---|",
        ]
        for row in self.rows:
            link = f"[{row.detail or row.state}]({row.url})" if row.url else (row.detail or row.state)
            lines.append(f"| `{row.workflow}` | {icon[row.state]} | {link} |")
        lines += [
            "",
            f"Check runs on the commit: {self.checks_green} green, "
            f"{len(self.checks_running)} running, {len(self.checks_red)} red.",
        ]
        if self.note:
            lines += ["", self.note]
        problems = self.problems()
        lines += [""]
        if problems:
            lines += ["**Not green:**", ""] + [f"- {p}" for p in problems]
        else:
            lines += ["**Green.**"]
        return "\n".join(lines) + "\n"


Api = Callable[[str], Any]


def gh_api(path: str) -> Any:
    proc = subprocess.run(["gh", "api", path], capture_output=True, text=True, encoding="utf-8")
    if proc.returncode != 0:
        raise HealthError(f"gh api {path}: {(proc.stderr or proc.stdout).strip()}")
    return json.loads(proc.stdout)


def assess(
    repo: str,
    ref: str,
    workflows: list[str],
    api: Api,
    *,
    event: str = "auto",
    through_merge: bool = False,
    ignore: tuple[str, ...] = (),
) -> Report:
    commit = api(f"repos/{repo}/commits/{ref}")
    note = ""
    parents = commit.get("parents") or []
    if through_merge and len(parents) == 2:
        merged = parents[1]["sha"]
        note = f"`{commit['sha'][:8]}` is a merge commit; judged what it merged, `{merged[:8]}`."
        commit = api(f"repos/{repo}/commits/{merged}")
    sha = commit["sha"]
    message = (commit.get("commit", {}).get("message") or "").strip().splitlines()
    subject = message[0] if message else ""

    rows: list[Row] = []
    for workflow in workflows:
        data = api(f"repos/{repo}/actions/workflows/{workflow}/runs?head_sha={sha}&per_page=100")
        run = pick_run(data.get("workflow_runs", []), event)
        state = verdict(run)
        detail = ""
        if run is not None:
            detail = (run.get("conclusion") or run.get("status") or "") if state != "green" else "success"
        rows.append(Row(workflow, state, detail, (run or {}).get("html_url", "")))

    report = Report(repo, sha, subject, rows, note=note)
    checks: list[dict] = []
    total = 0
    for page in range(1, 21):
        data = api(f"repos/{repo}/commits/{sha}/check-runs?per_page=100&page={page}")
        batch = data.get("check_runs", [])
        total = data.get("total_count", len(checks) + len(batch))
        checks += batch
        if not batch or len(checks) >= total:
            break
    if len(checks) < total:
        # Say so rather than pass on a partial list.
        report.partial = f"only {len(checks)} of {total} check runs could be read"
    for check in checks:
        name, status, conclusion = check["name"], check.get("status"), check.get("conclusion")
        if name in ignore:
            continue
        if status != "completed":
            # Informational: a workflow that is still running is reported above,
            # and a pull request's own checks are that pull request's business.
            report.checks_running.append(name)
        elif conclusion == "success":
            report.checks_green += 1
        elif conclusion in ("skipped", "neutral"):
            continue
        else:
            report.checks_red.append(f"{name} ({conclusion})")
    return report


def assess_waiting(
    assess_once: Callable[[], Report],
    *,
    wait_minutes: float,
    interval: float = 60.0,
    sleep: Callable[[float], None] = time.sleep,
    now: Callable[[], float] = time.monotonic,
    say: Callable[[str], None] = print,
) -> Report:
    """assess_once() until nothing is unfinished, something has failed, or the time is up.

    A workflow that has not finished is not a verdict yet; a failure is one at
    once, so the wait ends there and the failure is reported without sitting out
    the rest of the time.
    """
    deadline = now() + wait_minutes * 60.0
    polls = 0
    while True:
        report = assess_once()
        if report.failed or not report.unfinished:
            return report
        if wait_minutes <= 0 or now() >= deadline:
            if wait_minutes > 0:
                report.gave_up_after = int(wait_minutes)
            return report
        polls += 1
        waiting = ", ".join(r.workflow for r in report.rows if r.state in ("running", "none"))
        say(f"waiting for {waiting} on {report.sha[:8]} (poll {polls})")
        sleep(interval)


def scorecard(repo: str, fetch: Callable[[str], tuple[int, str]]) -> tuple[bool, str]:
    """Has OpenSSF Scorecard published a result for the repository?"""
    status, body = fetch(f"https://api.scorecard.dev/projects/github.com/{repo}")
    if status == 404:
        return False, (
            f"no Scorecard result for {repo} yet, so the README badge reads 'invalid repo path'. "
            "scorecard.yml publishes on a push to main, the weekly schedule and a branch-protection "
            "change, and only from the default branch: until the release has merged there is "
            "nothing to publish."
        )
    if status != 200:
        raise HealthError(f"api.scorecard.dev answered {status}")
    data = json.loads(body)
    commit = (data.get("repo") or {}).get("commit", "?")
    return True, f"Scorecard {data.get('score')} on {str(data.get('date', '?'))[:10]} (commit {commit[:8]})"


def _http_get(url: str) -> tuple[int, str]:
    try:
        with urllib.request.urlopen(url, timeout=30) as response:  # noqa: S310 (a fixed https URL)
            return response.status, response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        return exc.code, ""


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Is a commit green on every workflow the README has a badge for?"
    )
    parser.add_argument("--repo", default="iainchesworthlabs/cubeulator")
    parser.add_argument("--ref", default="develop", help="branch or commit SHA (default develop)")
    parser.add_argument(
        "--workflows", help="comma-separated workflow files; default: the README's badges"
    )
    parser.add_argument("--readme", type=Path, default=ROOT / "README.md")
    parser.add_argument(
        "--event",
        default="auto",
        help="which run counts: auto (push, else any; the default), any, or an event name",
    )
    parser.add_argument("--through-merge", action="store_true", help="judge a merge commit's second parent")
    parser.add_argument(
        "--ignore", action="append", default=[], metavar="CHECK", help="a check run to leave out (repeatable)"
    )
    parser.add_argument("--summary", type=Path, help="append a markdown report here ($GITHUB_STEP_SUMMARY)")
    parser.add_argument(
        "--wait",
        type=float,
        default=0.0,
        metavar="MINUTES",
        help="wait up to this long for workflows that have not finished (default: do not wait)",
    )
    parser.add_argument("--interval", type=float, default=60.0, help="seconds between polls (default 60)")
    parser.add_argument("--scorecard", action="store_true", help="check the Scorecard result instead")
    args = parser.parse_args(argv)

    try:
        if args.scorecard:
            ok, text = scorecard(args.repo, _http_get)
            print(text)
            return 0 if ok else 1

        if args.workflows:
            workflows = [w.strip() for w in args.workflows.split(",") if w.strip()]
        else:
            workflows = badge_workflows(args.readme.read_text(encoding="utf-8"))
        if not workflows:
            print("error: no workflow badges found in README.md and none given", file=sys.stderr)
            return 2

        report = assess_waiting(
            lambda: assess(
                args.repo,
                args.ref,
                workflows,
                gh_api,
                event=args.event,
                through_merge=args.through_merge,
                ignore=tuple(args.ignore),
            ),
            wait_minutes=args.wait,
            interval=args.interval,
            say=lambda line: print(line, flush=True),
        )
    except (HealthError, OSError, json.JSONDecodeError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print(f"{args.ref} @ {report.sha[:8]}  {report.subject[:72]}")
    if report.note:
        print(f"  {report.note.replace('`', '')}")
    for row in report.rows:
        print(f"  {row.workflow:<18} {row.state:<8} {row.detail:<12} {row.url}")
    print(
        f"  check runs on the commit: {report.checks_green} green, "
        f"{len(report.checks_running)} running, {len(report.checks_red)} red"
    )
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as out:
            out.write(report.markdown())
    problems = report.problems()
    for problem in problems:
        print(f"NOT GREEN: {problem}", file=sys.stderr)
    if not problems:
        print("green")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
