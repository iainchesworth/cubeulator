# Licence texts

Canonical licence texts, one file per SPDX identifier (`<id>.txt`), read by the
notices generator (`cmake/ThirdPartyNoticesGenerate.cmake`) for components
whose own text is not already supplied by their vcpkg `copyright` file or a
`licenseFile` in `manifest.json`. `GPL-3.0-only` and `GPL-3.0-or-later` are
served from the repository's `LICENSE`.

Provenance of each file, so a reviewer knows what was copied and what was typed:

| file | source |
|---|---|
| `Apache-2.0.txt` | verbatim copy of `assets/models/palm_detection_mediapipe_LICENSE.txt` (the unmodified Apache-2.0 text) |
| `MPL-2.0.txt` | verbatim extract from vcpkg's `share/eigen3/copyright` (x64-windows, baseline 5b69863) |
| `libpng-2.0.txt`, `Libpng.txt` | verbatim copy of vcpkg's `share/libpng/copyright` (it holds both licence versions) |
| `LicenseRef-*.txt` | verbatim `extractedText` from the SPDX SBOMs in the Qt 6.8.3 install (`<prefix>/sbom/*.spdx.json`) |
| `MIT.txt`, `BSD-2-Clause.txt`, `BSD-3-Clause.txt`, `ISC.txt`, `Zlib.txt` | the standard SPDX template wording, typed by hand without the per-project copyright line (the notices print each component's own copyright line above a reference to this text) |

A licence id the build needs but this directory lacks is reported as
*unresolved* and fails the notices test. At the time of writing the texts for
`FTL`, `Unicode-3.0`, `AML-glslang`, `Bison-exception-2.2`, `X11`, `HPND`,
`Imlib2` and `MIT-open-group` still have to be added, byte-exact, from
<https://github.com/spdx/license-list-data> (`text/<id>.txt`); see
`docs/licensing.md`.
