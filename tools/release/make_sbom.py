#!/usr/bin/env python3
"""The release's SBOM: what is inside each package, from the build's own inventory.

A scan of an installer sees an installer. What a person (or a scanner of
vulnerabilities, or a licence audit) needs is what the package *holds*: Qt, the
Qt modules and plugins that were deployed, OpenCV and the libraries it pulls in,
the fonts, with versions and licences. The build already knows: it writes
`third_party_notices/third-party-notices.json` for every platform, from the
linked libraries, vcpkg's records and Qt's own SBOM manifest, and refuses to
package while anything is unresolved (docs/licensing.md). This turns that
inventory into SPDX 2.3, one SBOM for the release:

    make_sbom.py --version 0.1.0-beta.1 --tag v0.1.0-beta.1 \\
        --repo iainchesworthlabs/cubeulator --files dist/files \\
        --notices windows=notices-windows.json --notices macos=notices-macos.json \\
        --notices linux=notices-linux.json --output dist/files/Cubeulator-0.1.0-beta.1.spdx.json

What it says:

  * one SPDX package per release file (the .exe, .dmg, .deb, .rpm and the web .zip), with its
    file name, its SHA-256 and SHA-512, and where it will be downloaded from;
  * one package per component that ships in it, with version, the licence it
    declares and the one it is distributed under, copyright and homepage;
  * the document describes each release file, and each release file `CONTAINS`
    its components. Components are per platform (Qt's plugins
    and vcpkg's triplets differ), so one library can appear once per platform
    with its own version.

It refuses to write an SBOM for an inventory that has anything unresolved, and
for a release file whose platform it has no inventory for: a thin SBOM is worse
than none, because it reads as complete.

Standard library only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

SPDX_VERSION = "SPDX-2.3"

# Which inventory a release file belongs to.
PLATFORM_OF_SUFFIX = {".exe": "windows", ".dmg": "macos", ".deb": "linux", ".rpm": "linux", ".zip": "web"}

_ID_SANITISE_RE = re.compile(r"[^A-Za-z0-9.\-]+")
_LICENSE_TOKEN_RE = re.compile(r"[A-Za-z0-9.+\-]+")
_OPERATORS = {"AND", "OR", "WITH"}


class SbomError(RuntimeError):
    """The SBOM cannot be made as asked."""


def spdx_id(*parts: str) -> str:
    """SPDXRef-<parts>, with only the characters SPDX allows."""
    return "SPDXRef-" + "-".join(_ID_SANITISE_RE.sub("-", p).strip("-") for p in parts)


def license_tokens(expression: str) -> list[str]:
    """The identifiers in an SPDX licence expression, operators and parens aside."""
    cleaned = expression.replace("(", " ").replace(")", " ")
    return [t for t in cleaned.split() if t not in _OPERATORS]


def usable_expression(expression: str | None) -> str:
    """The expression if it is made of identifiers SPDX can parse, else NOASSERTION."""
    if not expression:
        return "NOASSERTION"
    tokens = license_tokens(expression)
    if not tokens or any(not _LICENSE_TOKEN_RE.fullmatch(t) for t in tokens):
        return "NOASSERTION"
    return expression.strip()


def file_hashes(path: Path) -> dict[str, str]:
    sha256, sha512 = hashlib.sha256(), hashlib.sha512()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            sha256.update(chunk)
            sha512.update(chunk)
    return {"SHA256": sha256.hexdigest(), "SHA512": sha512.hexdigest()}


def component_package(platform: str, comp: dict) -> dict:
    version = str(comp.get("version") or "").strip()
    declared = usable_expression(comp.get("license"))
    concluded = usable_expression(comp.get("distributedUnder") or comp.get("license"))
    package: dict = {
        "SPDXID": spdx_id(platform, comp["id"], version or "unversioned"),
        "name": comp["name"],
        "downloadLocation": "NOASSERTION",
        "filesAnalyzed": False,
        "licenseConcluded": concluded,
        "licenseDeclared": declared,
        "copyrightText": comp.get("copyright") or "NOASSERTION",
    }
    if version:
        package["versionInfo"] = version
    homepage = comp.get("homepage")
    if homepage:
        package["homepage"] = homepage
    notes = [f"kind: {comp['kind']}"] if comp.get("kind") else []
    notes += [str(v) for v in comp.get("via") or []]
    if comp.get("notes"):
        notes.append(str(comp["notes"]))
    if notes:
        package["comment"] = "; ".join(notes)
    return package


def build(
    *,
    version: str,
    tag: str,
    repo: str,
    files: list[Path],
    notices: dict[str, dict],
    created: datetime,
) -> dict:
    if not files:
        raise SbomError("no release files to describe")

    packages: list[dict] = []
    relationships: list[dict] = []
    extracted: dict[str, dict] = {}
    describes: list[str] = []

    for platform, inventory in notices.items():
        unresolved = inventory.get("unresolved") or []
        if unresolved:
            raise SbomError(
                f"the {platform} inventory has unresolved items, which the build refuses to "
                "package; first one: " + str(unresolved[0])
            )

    components_by_platform: dict[str, list[dict]] = {}
    for platform, inventory in notices.items():
        seen: set[str] = set()
        comps: list[dict] = []
        for comp in inventory.get("components", []):
            pkg = component_package(platform, comp)
            if pkg["SPDXID"] in seen:
                continue
            seen.add(pkg["SPDXID"])
            packages.append(pkg)
            comps.append(pkg)
            for expr in (pkg["licenseConcluded"], pkg["licenseDeclared"]):
                for token in license_tokens(expr):
                    if token.startswith("LicenseRef-") and token not in extracted:
                        extracted[token] = {
                            "licenseId": token,
                            "name": token.removeprefix("LicenseRef-").replace("-", " "),
                            "extractedText": (
                                inventory.get("licenseTexts", {}).get(token)
                                or "A licence option named by a component's own licence "
                                "expression. It is not the option this software is "
                                "distributed under; see licenseConcluded."
                            ),
                        }
        components_by_platform[platform] = comps

    for path in sorted(files, key=lambda p: p.name):
        platform = PLATFORM_OF_SUFFIX.get(path.suffix.lower())
        if platform is None:
            continue
        if platform not in components_by_platform:
            raise SbomError(f"no inventory for {platform}, which {path.name} belongs to")
        pkg_id = spdx_id("File", path.name)
        hashes = file_hashes(path)
        packages.append(
            {
                "SPDXID": pkg_id,
                "name": path.name,
                "versionInfo": version,
                "packageFileName": path.name,
                "downloadLocation": f"https://github.com/{repo}/releases/download/{tag}/{path.name}",
                "filesAnalyzed": False,
                "checksums": [
                    {"algorithm": "SHA256", "checksumValue": hashes["SHA256"]},
                    {"algorithm": "SHA512", "checksumValue": hashes["SHA512"]},
                ],
                "supplier": "Organization: Iain Chesworth Labs",
                "licenseConcluded": "NOASSERTION",
                "licenseDeclared": "GPL-3.0-or-later",
                "copyrightText": "Copyright (C) 2026 Iain Chesworth",
                "homepage": f"https://github.com/{repo}",
                "summary": f"Cubeulator {version} for {platform}",
            }
        )
        describes.append(pkg_id)
        for comp in components_by_platform[platform]:
            relationships.append(
                {"spdxElementId": pkg_id, "relationshipType": "CONTAINS", "relatedSpdxElement": comp["SPDXID"]}
            )

    if not describes:
        raise SbomError("none of the files is a package this tool knows (.exe, .dmg, .deb, .rpm, .zip)")

    # A namespace that is unique to this content, as SPDX asks.
    digest = hashlib.sha256(
        json.dumps([packages, relationships], sort_keys=True).encode("utf-8")
    ).hexdigest()[:16]

    document = {
        "spdxVersion": SPDX_VERSION,
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"Cubeulator {version}",
        "documentNamespace": f"https://github.com/{repo}/releases/{tag}/sbom-{digest}",
        "creationInfo": {
            "created": created.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "creators": ["Organization: Iain Chesworth Labs", "Tool: cubeulator-tools-release-make_sbom"],
            "comment": "Components are the build's own third-party inventory "
            "(third-party-notices.json): what is linked and deployed, per platform.",
        },
        "documentDescribes": describes,
        "packages": packages,
        "relationships": relationships,
    }
    if extracted:
        document["hasExtractedLicensingInfos"] = list(extracted.values())
    return document


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--version", required=True)
    parser.add_argument("--tag", help="default v<version>")
    parser.add_argument("--repo", required=True, help="owner/repo")
    parser.add_argument("--files", type=Path, required=True, help="directory holding the release files")
    parser.add_argument(
        "--notices",
        action="append",
        default=[],
        metavar="PLATFORM=PATH",
        help="a platform's third-party-notices.json (repeat for windows, macos, linux, web)",
    )
    parser.add_argument("--created", help="ISO time for creationInfo; default now (UTC)")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)

    notices: dict[str, dict] = {}
    for item in args.notices:
        platform, _, raw = item.partition("=")
        if not platform or not raw:
            print(f"error: --notices wants PLATFORM=PATH, got '{item}'", file=sys.stderr)
            return 2
        notices[platform] = json.loads(Path(raw).read_text(encoding="utf-8"))

    created = (
        datetime.fromisoformat(args.created.replace("Z", "+00:00"))
        if args.created
        else datetime.now(timezone.utc)
    )
    try:
        document = build(
            version=args.version,
            tag=args.tag or f"v{args.version}",
            repo=args.repo,
            files=[p for p in args.files.iterdir() if p.is_file()],
            notices=notices,
            created=created,
        )
    except SbomError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")
    counts = {p: sum(1 for k in document["packages"] if k["SPDXID"].startswith(f"SPDXRef-{p}-")) for p in notices}
    print(f"{args.output.name}: {len(document['documentDescribes'])} files, components " + ", ".join(f"{p} {n}" for p, n in counts.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
