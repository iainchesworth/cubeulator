#!/usr/bin/env python3
"""Condense the SPDX SBOMs that ship inside a Qt install into qt-modules.json.

Qt 6.8+ installs carry <prefix>/sbom/<repo>-<version>.spdx.json: one package per
module, plugin, bundled third-party library and third-party attribution, with
SPDX licence expressions, copyright lines, download locations, DEPENDS_ON edges
between them, and the binary files each package contains. The files are several
megabytes each, so the build does not read them; it reads the snapshot this
script writes, third_party/notices/qt-modules.json, and the build's notices test
checks the snapshot's licences against a live SBOM again whenever one is present
(--check).

    python -I tools/licences/qt_sbom_manifest.py \\
        --sbom windows=<qt>/msvc2022_64/sbom \\
        --sbom android=<qt>/android_arm64_v8a/sbom \\
        --sbom macos=<qt>/macos/sbom \\
        --out third_party/notices/qt-modules.json

    python -I tools/licences/qt_sbom_manifest.py --check \\
        --sbom linux=$QT_ROOT/sbom --out third_party/notices/qt-modules.json

Platforms named here are the ones whose SBOM was read ("verified"). A component
seen on every verified platform is platform-neutral; one seen on only some is
tagged with them.

The "files" map in the output turns the name of a library the build links or
deploys (qt6core, qwindows, qdarwincamerapermission ...) into the component that
contains it. It is read from the SBOMs' own list of packaged binaries, not
guessed from package names: a plugin's package is called
QDarwinCameraPermissionPlugin, its library libqdarwincamerapermission.a.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ID_RE = re.compile(
    r"^SPDXRef-Package-(?P<repo>[a-z0-9]+)-"
    r"(?P<kind>qt-module|qt-plugin|qt-bundled-3rdparty-module|qt-3rdparty-sources)-"
    r"(?P<name>.+)$"
)
KIND = {
    "qt-module": "module",
    "qt-plugin": "plugin",
    "qt-bundled-3rdparty-module": "bundled",
    "qt-3rdparty-sources": "attribution",
}
# when two components claim the same library file name, the more specific wins
KIND_RANK = {"module": 0, "plugin": 1, "bundled": 2, "attribution": 3}


def _clean(value):
    return "" if value in (None, "NOASSERTION", "NONE") else value


def file_key(path: str) -> str:
    """The key the build uses for a library: cmake/ThirdPartyNoticesGenerate.cmake
    cn_qt_file_key() applies exactly this normalisation to a link input."""
    name = path.replace("\\", "/").rsplit("/", 1)[-1].lower()
    name = re.sub(r"\.(lib|a|dylib|dll|framework|so(\.[0-9]+)*)$", "", name)
    name = re.sub(r"^lib", "", name)
    name = re.sub(r"_(arm64-v8a|armeabi-v7a|x86_64|x86)$", "", name)
    return name


def read_sbom_dir(sbom_dir: Path) -> tuple[dict[str, dict], dict[str, str]]:
    """Return ({component id: record}, {library key: component id})."""
    docs = sorted(sbom_dir.glob("*.spdx.json"))
    if not docs:
        raise SystemExit(f"no *.spdx.json under {sbom_dir}")
    loaded = [json.loads(doc.read_text(encoding="utf-8")) for doc in docs]

    # Pass 1: every package of every document, so a cross-document edge can be
    # resolved from the SPDXID alone (the "DocumentRef-<repo>-<hash>:" prefix
    # and the "-<hash>" suffix on the id change from build to build).
    components: dict[str, dict] = {}
    by_spdxid: dict[str, str] = {}
    for data in loaded:
        for pkg in data.get("packages", []):
            m = ID_RE.match(pkg.get("SPDXID", ""))
            if not m:
                continue
            repo, kind, name = m["repo"], KIND[m["kind"]], pkg["name"]
            cid = f"{repo}/{name}"
            by_spdxid[pkg["SPDXID"]] = cid
            components[cid] = {
                "kind": kind,
                "repo": repo,
                "name": name,
                "version": _clean(pkg.get("versionInfo")),
                "license": _clean(pkg.get("licenseConcluded")),
                "copyright": _clean(pkg.get("copyrightText")),
                "download": _clean(pkg.get("downloadLocation")),
                "depends": [],
            }

    # Pass 2: edges and packaged binaries.
    binaries: dict[str, str] = {}
    for data in loaded:
        binary_files = {
            f["SPDXID"]: f["fileName"]
            for f in data.get("files", [])
            if "BINARY" in f.get("fileTypes", []) and f.get("fileName")
        }
        for rel in data.get("relationships", []):
            kind = rel.get("relationshipType")
            src = by_spdxid.get(rel["spdxElementId"])
            if src is None:
                continue
            dst_raw = rel["relatedSpdxElement"]
            if kind == "DEPENDS_ON":
                dst = by_spdxid.get(dst_raw.partition(":")[2] or dst_raw)
                if dst and dst not in components[src]["depends"]:
                    components[src]["depends"].append(dst)
            elif kind == "CONTAINS" and dst_raw in binary_files:
                key = file_key(binary_files[dst_raw])
                if not key:
                    continue
                current = binaries.get(key)
                if current is None or KIND_RANK[components[src]["kind"]] < KIND_RANK[components[current]["kind"]]:
                    binaries[key] = src
    for rec in components.values():
        rec["depends"].sort()
    return components, binaries


def merge(per_platform: dict[str, tuple[dict[str, dict], dict[str, str]]]) -> dict:
    platforms = sorted(per_platform)
    merged: dict[str, dict] = {}
    files: dict[str, str] = {}
    edges: dict[str, dict[str, set[str]]] = {}   # cid -> platform -> its DEPENDS_ON targets
    for platform in platforms:
        components, binaries = per_platform[platform]
        for cid, rec in components.items():
            slot = merged.setdefault(cid, {**rec, "platforms": []})
            slot["platforms"].append(platform)
            if not slot["copyright"]:
                slot["copyright"] = rec["copyright"]
            edges.setdefault(cid, {})[platform] = set(rec["depends"])
        for key, cid in binaries.items():
            previous = files.setdefault(key, cid)
            if previous != cid:
                print(f"note: library '{key}' maps to {previous} and (on {platform}) {cid}; keeping the first",
                      file=sys.stderr)
    for rec in merged.values():
        if rec["platforms"] == platforms:
            rec["platforms"] = ["*"]
    # An edge present on every platform the component exists on is "depends";
    # one present on only some is listed under "platformDepends" for those. The
    # union would drag a macOS-only dependency (Gui -> DBus) onto Windows.
    for cid, rec in merged.items():
        per = edges[cid]
        common = set.intersection(*per.values()) if per else set()
        rec["depends"] = sorted(d for d in common if d in merged)
        extras = {}
        for platform, targets in sorted(per.items()):
            only = sorted(d for d in targets - common if d in merged)
            if only:
                extras[platform] = only
        if extras:
            rec["platformDepends"] = extras
    # Fallbacks for a library the packaged-binary lists did not name: the
    # package's own name, which is right for modules (Qt6<Name>) on every
    # platform and for some plugins.
    for cid, rec in merged.items():
        if rec["kind"] == "module":
            files.setdefault(f"qt6{rec['name'].lower()}", cid)
        elif rec["kind"] == "plugin":
            files.setdefault(rec["name"].lower(), cid)
    files = {k: v for k, v in files.items() if v in merged}
    versions = sorted({r["version"] for r in merged.values() if r["kind"] == "module"})
    return {
        "generator": "tools/licences/qt_sbom_manifest.py",
        "note": (
            "Generated from the SPDX SBOMs in a Qt install. Do not edit by hand; "
            "re-run the generator after a Qt upgrade."
        ),
        "qtVersions": versions,
        "verifiedPlatforms": platforms,
        "files": dict(sorted(files.items())),
        "components": dict(sorted(merged.items())),
    }


def render(doc: dict) -> str:
    """Compact, diff-friendly JSON: one line per component and per file key."""

    def dump(value):
        return json.dumps(value, ensure_ascii=False, separators=(", ", ": "))

    lines = ["{"]
    for key in ("generator", "note", "qtVersions", "verifiedPlatforms"):
        lines.append(f"  {dump(key)}: {dump(doc[key])},")
    for section, last in (("files", False), ("components", True)):
        lines.append(f'  "{section}": {{')
        items = list(doc[section].items())
        for i, (k, v) in enumerate(items):
            comma = "," if i + 1 < len(items) else ""
            lines.append(f"    {dump(k)}: {dump(v)}{comma}")
        lines.append("  }" if last else "  },")
    lines.append("}")
    return "\n".join(lines) + "\n"


def check(snapshot: dict, live: dict[str, dict]) -> int:
    """Compare snapshot licences with a live SBOM; return the number of problems."""
    problems = 0
    for cid, rec in live.items():
        snap = snapshot["components"].get(cid)
        if snap is None:
            continue  # a module the snapshot never needed; --check is about drift
        if snap["license"] != rec["license"]:
            problems += 1
            print(f"LICENCE CHANGED  {cid}: snapshot '{snap['license']}' -> live '{rec['license']}'")
        if snap["version"] != rec["version"]:
            problems += 1
            print(f"VERSION CHANGED  {cid}: snapshot '{snap['version']}' -> live '{rec['version']}'")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sbom", action="append", required=True, metavar="PLATFORM=DIR")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--check", action="store_true", help="compare, do not write")
    args = ap.parse_args()

    per_platform: dict[str, tuple[dict[str, dict], dict[str, str]]] = {}
    for spec in args.sbom:
        platform, _, directory = spec.partition("=")
        if not directory:
            ap.error(f"--sbom wants PLATFORM=DIR, got {spec!r}")
        per_platform[platform] = read_sbom_dir(Path(directory))

    if args.check:
        snapshot = json.loads(args.out.read_text(encoding="utf-8"))
        bad = 0
        for platform, (live, _binaries) in per_platform.items():
            print(f"checking snapshot against live SBOM for '{platform}' ...")
            bad += check(snapshot, live)
        print("snapshot matches" if bad == 0 else f"{bad} difference(s)")
        return 1 if bad else 0

    out = merge(per_platform)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(render(out), encoding="utf-8", newline="\n")
    print(f"wrote {args.out}: {len(out['components'])} components, {len(out['files'])} library keys, "
          f"platforms {out['verifiedPlatforms']}, Qt {out['qtVersions']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
