#!/usr/bin/env bash
# Is every system library a .deb's own files need provided by what it declares?
#
#   packaging/linux/check-depends.sh <package.deb>
#
# The package carries its own Qt and OpenCV, so its Depends is written down, not
# discovered (packaging/linux/Packaging.cmake: dpkg-shlibdeps would try to
# resolve the bundled libraries). A list written by hand drifts: a library the
# build host happens to have is linked, and nobody notices until a machine
# without it fails to start. This is the check that a list written by hand
# lacks. It
#
#   1. unpacks the .deb and runs ldd over every ELF in it, with the bundled
#      library directory on the path, so that what is left is what the package
#      expects the system to provide;
#   2. fails if any of it cannot be found at all;
#   3. maps what is found to the Debian packages that own it, and fails for any
#      owner that is neither one of the declared Depends nor reachable from one
#      (a declared package pulls in its own dependencies: libgl1 brings
#      libglvnd0, and the declared libglib2.0-0 is libglib2.0-0t64 on a
#      distribution that has done the t64 rename).
#
# Needs dpkg, ldd and apt-cache, which the Linux runners and any Debian-based
# machine have. It does not run the application.
set -euo pipefail

deb="${1:?usage: check-depends.sh <package.deb>}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

dpkg-deb -x "$deb" "$work/root"
dpkg-deb -f "$deb" Depends > "$work/depends.txt"
bundled="$(cd "$work/root" && pwd)"

# Every ELF the package ships: the executable, Qt, the plugins, OpenCV.
mapfile -t elfs < <(find "$work/root" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) | sort)

declare -A owner_of_lib=()
unresolved=0
for f in "${elfs[@]}"; do
    [ "$(head -c4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] || continue
    # ldd consults RUNPATH ($ORIGIN/../lib) itself; the explicit path covers
    # the plugins, which have no RUNPATH of their own and are found by Qt.
    out="$(LD_LIBRARY_PATH="$bundled/usr/lib/cubeulator/lib" ldd "$f" 2>/dev/null || true)"
    while read -r line; do
        case "$line" in
        *"not found"*)
            echo "::error::$(basename "$f") needs ${line%% =>*}, which nothing provides"
            unresolved=$((unresolved + 1))
            ;;
        *"=> /"*)
            path="$(awk '{print $3}' <<<"$line")"
            case "$path" in
            "$bundled"/*) ;;            # bundled: not the system's to provide
            *) owner_of_lib["$path"]=1 ;;
            esac
            ;;
        esac
    done <<<"$out"
done

# Owners of the system libraries that were found.
declare -A owners=()
for lib in "${!owner_of_lib[@]}"; do
    real="$(readlink -f "$lib")"
    pkg="$(dpkg -S "$real" 2>/dev/null | head -1 | cut -d: -f1 || true)"
    [ -n "$pkg" ] && owners["${pkg%%:*}"]="$lib"
done

# What the declared Depends gives: itself and everything reachable from it,
# counting a "t64" rename as the same package.
strip_t64() { sed -E 's/t64$//'; }
declared="$(sed 's/([^)]*)//g; s/|[^,]*//g' "$work/depends.txt" | tr ',' '\n' | sed 's/ //g' | grep -v '^$' | sort -u)"
reach="$work/reach.txt"
: > "$reach"
for d in $declared; do
    for candidate in "$d" "${d}t64"; do
        echo "$candidate" >> "$reach"
        apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts \
            --no-breaks --no-replaces --no-enhances "$candidate" 2>/dev/null |
            grep -E '^[a-z0-9]' >> "$reach" || true
    done
done
strip_t64 < "$reach" | sort -u > "$work/reach.norm"

missing=0
for pkg in "${!owners[@]}"; do
    norm="$(strip_t64 <<<"$pkg")"
    if ! grep -qx -- "$norm" "$work/reach.norm"; then
        echo "::error::the package links ${owners[$pkg]} (package $pkg), which its Depends neither names nor pulls in"
        missing=$((missing + 1))
    fi
done

echo "ELF files looked at: ${#elfs[@]}; system libraries used: ${#owner_of_lib[@]}; owning packages: ${#owners[@]}; declared: $(wc -l <<<"$declared")"
if [ "$unresolved" -ne 0 ] || [ "$missing" -ne 0 ]; then
    echo "::error::the .deb's dependencies are incomplete ($unresolved unresolved, $missing undeclared)"
    exit 1
fi
echo "dependencies ok"
