#!/usr/bin/env bash
# Looks inside the web package make-package.sh wrote and fails if it is not a
# site a person could host.
#
#   packaging/web/check-package.sh <Cubeulator-<version>-web.zip>
#
# The desktop packages are held by packaging/check-package.sh; this is the
# browser build's: the files the page loads, a module that is WebAssembly, the
# licence and the notices - and that a package which says it is threaded is
# built the way a threaded page needs.
set -euo pipefail

zip="${1:?usage: check-package.sh <Cubeulator-<version>-web.zip>}"
[ -f "$zip" ] || { echo "::error::no such file: $zip"; exit 1; }

base="$(basename "$zip" .zip)"
case "$base" in
Cubeulator-*-web) ;;
*) echo "::error::the package is not named Cubeulator-<version>-web.zip: $base"; exit 1 ;;
esac

failures=0
require() {  # require <description> <command...>
    local what="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "ok      $what"
    else
        echo "::error::package check failed: $what"
        failures=$((failures + 1))
    fi
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
unzip -q "$zip" -d "$work"

# One directory, named for the package: unzipping must not scatter files.
top="$(find "$work" -mindepth 1 -maxdepth 1 | wc -l | tr -d ' ')"
require "the zip holds one directory"                         test "$top" -eq 1
dir="$work/$base"
require "that directory is named for the package"             test -d "$dir"

for f in index.html cube_app.js cube_app.wasm qtloader.js cubeulator-isolation.js icon.svg \
         LICENSE.txt THIRD-PARTY-NOTICES.txt HOSTING.txt; do
    require "$f is in the package"                            test -s "$dir/$f"
done

# The module: WebAssembly (the magic number 0x00 'a' 's' 'm'), and a real one.
magic="$(head -c 4 "$dir/cube_app.wasm" 2>/dev/null | od -An -tx1 | tr -d ' \n' || true)"
require "cube_app.wasm is a WebAssembly module"               test "$magic" = "0061736d"
size="$(wc -c < "$dir/cube_app.wasm" 2>/dev/null | tr -d ' ' || true)"
size="${size:-0}"
require "cube_app.wasm is more than a megabyte"               test "$size" -gt 1048576

# The page loads these by name; a rename in the build that the page did not
# follow would be a blank page.
require "index.html loads cube_app.js"                        grep -q "cube_app\.js" "$dir/index.html"
require "index.html loads qtloader.js"                        grep -q "qtloader\.js" "$dir/index.html"
require "index.html loads the isolation worker"               grep -q "cubeulator-isolation\.js" "$dir/index.html"
require "cube_app.js names cube_app.wasm"                     grep -q "cube_app\.wasm" "$dir/cube_app.js"

# Threads: the app is built with them throughout (docs/web.md), and a module
# that is not would load and then hang.
require "cube_app.js is the threaded build (it spawns workers)" grep -q -E "pthread|PThread" "$dir/cube_app.js"

# The notices say what is inside the module, not just that there is a file.
notices="$dir/THIRD-PARTY-NOTICES.txt"
require "the notices name Qt"                                 grep -q -i "qt" "$notices"
require "the notices name OpenCV"                             grep -q -i "opencv" "$notices"
require "the notices carry the browser fonts' licence"        grep -q -i "noto sans" "$notices"
require "the notices are not a placeholder"                   test "$(wc -l < "$notices" | tr -d ' ')" -gt 200
require "the licence is the GPL"                              grep -q "GNU GENERAL PUBLIC LICENSE" "$dir/LICENSE.txt"
version="${base#Cubeulator-}"
version="${version%-web}"
require "HOSTING.txt names this version"                      grep -q "Cubeulator ${version} for the browser" "$dir/HOSTING.txt"
require "HOSTING.txt has no placeholder left"                 bash -c "! grep -q '@VERSION@' '$dir/HOSTING.txt'"

if [ "$failures" -ne 0 ]; then
    echo "::error::$failures package check(s) failed for $zip"
    exit 1
fi
echo "$zip passes."
