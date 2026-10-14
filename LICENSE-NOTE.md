# A note on licensing: GPL-3.0-or-later and app stores

Cubeulator is licensed GPL-3.0-or-later, matching
[CountdownSolver](https://github.com/iainchesworth/CountdownSolver) exactly.

## Where things stand

At 0.1.0-beta.1, the first public pre-release:

- **Licence:** GPL-3.0-or-later, as in [LICENSE](LICENSE). Nothing in this
  note changes it.
- **Apple App Store and Mac App Store:** skipped for now. That is a deferral,
  not a decision against them for ever: the question below has to be answered
  before any Apple submission.
- **Everything else:** the Windows, macOS and Linux packages are ordinary
  GPL-3.0 distribution: the licence and the third-party notices are in the
  package and the source is in this repository. Google Play and F-Droid are
  planned once the app can capture on a phone, and no conflict has been found
  with either. The channel-by-channel reading, including the browser build,
  is in [docs/licensing.md](docs/licensing.md); it is an engineering
  analysis, not legal advice.

## The open question

Unlike CountdownSolver, Cubeulator was meant from the start to be offered
through app stores as well as by direct download, and GPLv3 has a documented
history of friction with Apple's App Store terms in particular. Apple's terms
impose restrictions (DRM/FairPlay, device and transfer limits) that a strict
reading of GPLv3 section 10 ("no further restrictions") and section 6
(Installation Information) arguably conflicts with. The FSF and various
commentators have written about this at length; there is no single settled
answer, and outcomes have varied by project.

**This is intentionally left open for now.** Flagging it here, rather than
silently picking GPLv3 or silently switching away from it, means the decision
gets made deliberately, with real store-submission context, rather than by
default months from now when it is expensive to change.

One finding changes how the question has to be read. The app links Qt Quick
3D, which The Qt Company offers only under GPL-3.0-only or a commercial
licence, and that code is not the maintainer's to relicense. A store exception
on Cubeulator's own code would therefore probably not settle Apple's terms by
itself. The routes that would (a Qt commercial licence, drawing the cube
without Quick 3D, or staying off the Apple stores) and what they cost are in
[docs/licensing.md](docs/licensing.md).

**Revisit this before the first Apple submission.** Options worth considering
at that point include a store-specific distribution exception, dual-licensing
(which, because of Qt Quick 3D, would need a Qt commercial licence for the
store build), or re-licensing before any Apple-bound build is made. Whatever
is decided, update this file (or replace it) with the actual resolution and
reasoning.

---

**2026-10-13 - analysis added, conclusion unchanged.** The question above was
still open and still the maintainer's to decide.
[docs/licensing.md](docs/licensing.md) now holds the inventory of third-party
components, the check of Qt's licences (Qt Quick 3D is GPL-3.0-only or
commercial; it matters more than the app's own code does), and the options for
the App Store with their cost.

**2026-10-13 - decision on the Apple stores.** The maintainer decided to skip
the iOS App Store and the Mac App Store for now. Cubeulator remains
GPL-3.0-or-later and ships elsewhere (desktop downloads, Linux
packages, later Google Play and F-Droid). The Apple-store question above is
deferred, not resolved: it returns before any Apple submission, and the
options are in [docs/licensing.md](docs/licensing.md).
