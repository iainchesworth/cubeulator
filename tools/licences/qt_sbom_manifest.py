#!/usr/bin/env python3
"""Condense the SPDX SBOMs that ship inside a Qt install into qt-modules.json.

Qt 6.8 installs carry <prefix>/sbom/<repo>-<version>.spdx.json: one package per
module, plugin, bundled third-party library and third-party attribution, with
SPDX licence expressions, copyright lines, download locations and DEPENDS_ON
edges between them. The files are several megabytes each, so the build does not
read them; it reads the snapshot this script writes, third_party/notices/
qt-modules.json, and the build's notices test checks the snapshot's licences
against a live SBOM again whenever one is present (--check).

    python -I tools/licences/qt_sbom_manifest.py \\
        --sbom windows=C:/Qt/6.8.3/msvc2022_64/sbom \\
        --sbom android=C:/Qt/6.8.3/android_arm64_v8a/sbom \\
        --out third_party/notices/qt-modules.json

    python -I tools/licences/qt_sbom_manifest.py --check \\
        --sbom linux=$QT_ROOT/sbom --out third_party/notices/qt-modules.json

Platforms named here are the ones whose SBOM was read ("verified"). A component
seen on every verified platform is platform-neutral; one seen on only some is
tagged with them.
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


def _clean(value):
    return "" if value in (None, "NOASSERTION", "NONE") else value


def read_sbom_dir(sbom_dir: Path) -> dict[str, dict]:
    """Return {component id: record} for every SBOM in the directory."""
    components: dict[str, dict] = {}
    docs = sorted(sbom_dir.glob("*.spdx.json"))
    if not docs:
        raise SystemExit(f"no *.spdx.json under {sbom_dir}")
    for doc in docs:
        data = json.loads(doc.read_text(encoding="utf-8"))
        local_ids: dict[str, str] = {}
        for pkg in data.get("packages", []):
            m = ID_RE.match(pkg.get("SPDXID", ""))
            if not m:
                continue
            repo, kind, name = m["repo"], KIND[m["kind"]], pkg["name"]
            cid = f"{repo}/{name}"
            local_ids[pkg["SPDXID"]] = cid
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
        # Cross-document edges look like "DocumentRef-qtbase:SPDXRef-Package-...".
        for rel in data.get("relationships", []):
            if rel.get("relationshipType") != "DEPENDS_ON":
                continue
            src = local_ids.get(rel["spdxElementId"])
            dst_raw = rel["relatedSpdxElement"]
            dst = local_ids.get(dst_raw)
            if dst is None and dst_raw.startswith("DocumentRef-"):
                ref, _, spdxid = dst_raw.partition(":")
                m = ID_RE.match(spdxid)
                if m:
                    dst = f"{m['repo']}/{_pkg_name_from_id(spdxid, m)}"
            if src and dst and dst not in components[src]["depends"]:
                components[src]["depends"].append(dst)
    for rec in components.values():
        rec["depends"].sort()
    return components


def _pkg_name_from_id(spdxid: str, m: re.Match) -> str:
    # The package name equals the tail of the SPDXID for every kind we keep.
    return m["name"]


def merge(per_platform: dict[str, dict[str, dict]]) -> dict:
    platforms = sorted(per_platform)
    merged: dict[str, dict] = {}
    for platform in platforms:
        for cid, rec in per_platform[platform].items():
            slot = merged.setdefault(cid, {**rec, "platforms": []})
            slot["platforms"].append(platform)
            if not slot["copyright"]:
                slot["copyright"] = rec["copyright"]
            slot["depends"] = sorted(set(slot["depends"]) | set(rec["depends"]))
    for rec in merged.values():
        if rec["platforms"] == platforms:
            rec["platforms"] = ["*"]
    # Keep depends edges only where the target is a known component.
    for rec in merged.values():
        rec["depends"] = [d for d in rec["depends"] if d in merged]
    files: dict[str, str] = {}
    for cid, rec in merged.items():
        if rec["kind"] == "module":
            files[f"qt6{rec['name'].lower()}"] = cid
            # the shared library of QuickShapesPrivate is Qt6QuickShapes.dll
            if rec["name"].endswith("Private"):
                files.setdefault(f"qt6{rec['name'][:-len('Private')].lower()}", cid)
        elif rec["kind"] == "plugin":
            files[rec["name"].lower()] = cid
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

    per_platform: dict[str, dict[str, dict]] = {}
    for spec in args.sbom:
        platform, _, directory = spec.partition("=")
        if not directory:
            ap.error(f"--sbom wants PLATFORM=DIR, got {spec!r}")
        per_platform[platform] = read_sbom_dir(Path(directory))

    if args.check:
        snapshot = json.loads(args.out.read_text(encoding="utf-8"))
        bad = 0
        for platform, live in per_platform.items():
            print(f"checking snapshot against live SBOM for '{platform}' ...")
            bad += check(snapshot, live)
        print("snapshot matches" if bad == 0 else f"{bad} difference(s)")
        return 1 if bad else 0

    out = merge(per_platform)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(render(out), encoding="utf-8", newline="\n")
    print(f"wrote {args.out}: {len(out['components'])} components, "
          f"platforms {out['verifiedPlatforms']}, Qt {out['qtVersions']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
