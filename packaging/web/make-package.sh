#!/usr/bin/env bash
# Assembles the browser build's release package: one zip holding the site the
# build wrote (the very directory docs.yml publishes under /app/), the licence,
# the third-party notices and a short note on hosting it.
#
#   packaging/web/make-package.sh <web build directory> <version> <output directory>
#
# <web build directory> is build/web-emscripten-release: its bin/ is the site
# and its third_party_notices/ holds what the build attributed.
#
# The desktop packages are made by CPack, which refuses to package while a
# third-party notice is unresolved (docs/licensing.md). A static site has no
# installer to refuse, so this does: it reads the build's own inventory and
# stops on anything unresolved, which is what keeps a web release from shipping
# with no notices for Qt's static libraries.
#
# The package is checked by packaging/web/check-package.sh.
set -euo pipefail

build="${1:?usage: make-package.sh <web build directory> <version> <output directory>}"
version="${2:?usage: make-package.sh <web build directory> <version> <output directory>}"
out="${3:?usage: make-package.sh <web build directory> <version> <output directory>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

site="$build/bin"
notices_dir="$build/third_party_notices"

fail() { echo "::error::$*"; exit 1; }

[ -f "$site/index.html" ] || fail "no site under $site (index.html is missing): was the web preset built?"
[ -f "$site/cube_app.wasm" ] || fail "no cube_app.wasm under $site"
[ -f "$notices_dir/THIRD-PARTY-NOTICES.txt" ] || fail "no THIRD-PARTY-NOTICES.txt under $notices_dir"
[ -f "$notices_dir/third-party-notices.json" ] || fail "no third-party-notices.json under $notices_dir"

# Nothing unresolved, and Qt's SBOMs were read for this platform.
python3 - "$notices_dir/third-party-notices.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1], encoding="utf-8"))
unresolved = doc.get("unresolved") or []
if unresolved:
    print("::error::the web build's third-party inventory has %d unresolved item(s); first: %s" % (len(unresolved), unresolved[0]))
    sys.exit(1)
if doc.get("platform") != "web":
    print("::error::the inventory is for platform %r, not 'web'" % doc.get("platform"))
    sys.exit(1)
if not doc.get("qtPlatformVerified"):
    print("::error::the inventory says Qt's SBOMs were not read for the web platform (qtPlatformVerified is false)")
    sys.exit(1)
print("ok      the third-party inventory: %d components, nothing unresolved" % len(doc.get("components", [])))
PY

name="Cubeulator-${version}-web"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
stage="$work/$name"
mkdir -p "$stage"

cp -R "$site/." "$stage/"
cp "$root/LICENSE" "$stage/LICENSE.txt"
cp "$notices_dir/THIRD-PARTY-NOTICES.txt" "$stage/THIRD-PARTY-NOTICES.txt"
sed "s/@VERSION@/${version}/g" "$root/packaging/web/HOSTING.txt.in" > "$stage/HOSTING.txt"

mkdir -p "$out"
out="$(cd "$out" && pwd)"
rm -f "$out/$name.zip"
(cd "$work" && zip -X -r -q "$out/$name.zip" "$name")
echo "wrote $out/$name.zip"
unzip -l "$out/$name.zip" | tail -n 3
