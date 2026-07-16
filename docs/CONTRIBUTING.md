# Contributing

Thanks for taking a look at Cubeulator! This guide is short — the goal is
just to save you a round-trip on the handful of things that aren't obvious
from the code.

## Before you start

For anything more than a typo fix, it's worth
[opening an issue](https://github.com/iainchesworth/cubeulator/issues) first
to say what you're thinking — it avoids duplicated effort and lets us agree
on the approach before you sink time into it.

## Branching model

This project follows [gitflow](https://nvie.com/posts/a-successful-git-branching-model/):

- **`develop`** is the integration branch — target your PR here.
- **`main`** only receives tagged releases; day-to-day work never targets
  it directly.
- Branch names must start with `feature/`, `bugfix/`, `release/`,
  `hotfix/`, or `support/` — a required CI check
  (`Validate gitflow branch name`) enforces this on every PR.

## Set up your build

See [Getting started](getting-started.md) for toolchain requirements and
your first build. `-DCUBEULATOR_BUILD_APP=OFF` skips Qt entirely if you're
only touching `cube::lib` and want a faster inner loop.

## Tests are not optional

This project is TDD by convention: new behaviour comes with a test, and
existing behaviour that changes gets its test updated in the same PR. See
[Testing & coverage](testing.md) for where each kind of test lives.

CI enforces an **80% line and branch coverage** floor
(`Coverage (Linux, GCC)`) — a PR that drops coverage below that will fail
the required check.

## Code style

- Formatting follows the repo's
  [`.clang-format`](https://github.com/iainchesworth/cubeulator/blob/develop/.clang-format) —
  run `clang-format` before committing, or configure your editor to do it
  on save.
- [`.clang-tidy`](https://github.com/iainchesworth/cubeulator/blob/develop/.clang-tidy)
  runs as part of the build; compiler warnings are treated as errors, so fix
  them rather than silencing them where you reasonably can.
- Modern C++23 is encouraged over older idioms — see
  [Architecture](architecture.md) for the patterns already in use
  (`std::expected`, deducing `this`, etc.) and follow their lead.

## Opening a pull request

1. Push your `feature/*` or `bugfix/*` branch and open a PR against
   `develop`.
2. Make sure CI is green: build matrix, coverage gate, CodeQL, and the
   branch-name check all have to pass before a PR can merge.
3. Keep PRs focused — a bug fix doesn't need to carry an unrelated
   refactor along with it.

That's it. If anything here is unclear or out of date, flagging it in an
issue is itself a welcome contribution.
