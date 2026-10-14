#!/usr/bin/env bash
# Does a pull request that changes what a person sees carry a changelog fragment?
#
#   check-fragment.sh <base> <head> [true]
#
# <base>...<head> is the pull request's own change. The third argument is
# "true" when the pull request carries the `no-changelog` label, which says the
# change is not one a reader of the changelog wants to hear about.
#
# Two things are held:
#
#   1. every fragment the pull request ADDS to changelog.d/ is well formed
#      (name, type, one item opening with a bold lead-in), so a bad one is
#      found here and not when a release is prepared;
#   2. a change under the paths a person using the app can notice (src/,
#      packaging/, cmake/, third_party/, vcpkg.json, the top-level
#      CMakeLists.txt, the string inventory) adds one, or is labelled.
#
# What a fragment is, and why: changelog.d/README.md.
#
# Plain bash and git, so that it runs on the shared self-hosted runners that
# have nothing else.
set -euo pipefail

base="${1:?usage: check-fragment.sh <base> <head> [true]}"
head="${2:?usage: check-fragment.sh <base> <head> [true]}"
opted_out="${3:-false}"

dir="changelog.d"
kinds='added|changed|deprecated|removed|fixed|security'
name_re="^[A-Za-z0-9][A-Za-z0-9._-]*\\.(${kinds})\\.md\$"

mapfile -t changed < <(git diff --name-only --diff-filter=ACMR "$base...$head")
mapfile -t added < <(git diff --name-only --diff-filter=A "$base...$head")

failures=0
fail() {
    echo "::error::$*"
    failures=$((failures + 1))
}

# --- 1. the fragments this change adds -----------------------------------------
fragments_added=0
for path in "${added[@]}"; do
    case "$path" in
    "$dir"/*) ;;
    *) continue ;;
    esac
    name="${path#"$dir"/}"
    [ "$name" = "README.md" ] && continue
    case "$name" in */*) fail "$path: fragments sit directly in $dir/"; continue ;; esac

    if ! [[ "$name" =~ $name_re ]]; then
        fail "$path: the name must be <slug>.<type>.md, <type> one of ${kinds//|/, }"
        continue
    fi
    fragments_added=$((fragments_added + 1))

    content="$(git show "$head:$path")"
    # trailing blank lines are not part of the item
    content="$(printf '%s' "$content" | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}')"
    first="${content%%$'\n'*}"
    if [ -z "$content" ]; then
        fail "$path: is empty"
    elif [[ "$first" != \*\** ]]; then
        fail "$path: must open with a **bold lead-in** naming the change"
    fi
    if [[ "$first" == "- "* || "$first" == "* "* ]]; then
        fail "$path: write the item without its leading dash; the dash is added"
    fi
    if printf '%s\n' "$content" | grep -q '^[[:space:]]*$'; then
        fail "$path: is one item, so one paragraph: it has a blank line in it"
    fi
done

# --- 2. a change a person can see needs one --------------------------------------
visible=()
for path in "${changed[@]}"; do
    case "$path" in
    src/* | packaging/* | cmake/* | third_party/* | vcpkg.json | CMakeLists.txt | docs/design/strings.json)
        visible+=("$path")
        ;;
    esac
done

if [ "${#visible[@]}" -gt 0 ] && [ "$fragments_added" -eq 0 ] && [ "$opted_out" != "true" ]; then
    fail "this changes ${#visible[@]} file(s) a person using the app can notice (first: ${visible[0]}) and adds no changelog fragment. Add $dir/<slug>.<type>.md (see $dir/README.md), or label the pull request no-changelog."
fi

if [ "$failures" -ne 0 ]; then
    exit 1
fi
if [ "$fragments_added" -gt 0 ]; then
    echo "changelog: $fragments_added fragment(s), well formed"
elif [ "${#visible[@]}" -gt 0 ]; then
    echo "changelog: no fragment, and the pull request is labelled no-changelog"
else
    echo "changelog: nothing here a reader of the changelog would notice"
fi
