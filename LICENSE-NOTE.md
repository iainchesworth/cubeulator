# A note on licensing: GPL-3.0-or-later and app stores

Cubeulator is licensed GPL-3.0-or-later, matching
[CountdownSolver](https://github.com/iainchesworth/CountdownSolver) exactly.
Unlike CountdownSolver, Cubeulator is intended to ship to app stores (Apple
App Store, Google Play), and GPLv3 has a documented history of friction with
Apple's App Store Distribution Agreement in particular — Apple's terms
impose restrictions (DRM/FairPlay, resale/transfer limits) that a strict
reading of GPLv3 §6 ("no further restrictions") arguably conflicts with. The
FSF and various commentators have written about this at length; there is no
single settled answer, and outcomes have varied by project.

**This is intentionally left open, not resolved, by this spec.** Flagging it
here rather than silently picking GPLv3 (or silently switching away from it)
means the decision gets made deliberately, with real store-submission
context, rather than by default months from now when it's expensive to
change.

**Revisit this before first store submission** — not before first commit.
Options worth considering at that point include: a store-specific
distribution exception (many GPL projects add one), dual-licensing, or
re-licensing before any store-bound code is written. Whatever is decided,
update this file (or replace it) with the actual resolution and reasoning.
