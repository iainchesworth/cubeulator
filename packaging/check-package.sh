#!/usr/bin/env bash
# Looks inside the package CPack just wrote and fails if it is not one a person
# could install. Used by CI (build-and-test) and by the release workflow, so
# that what is checked before a release is what is checked on every push.
#
#   packaging/check-package.sh <directory CPack wrote the package to>
#
# A package that built but is missing its Qt (a deploy script that quietly did
# nothing on a platform), or its icon, or its notices, is worse than a red job.
#
# The identifier is read from cmake/AppIdentity.cmake, the one place it is set.
set -euo pipefail

dir="${1:?usage: check-package.sh <package directory>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

app_id="$(sed -n 's/^set(CUBEULATOR_APP_ID "\(.*\)")$/\1/p' "$root/cmake/AppIdentity.cmake")"
[ -n "$app_id" ] || { echo "::error::cannot read CUBEULATOR_APP_ID from cmake/AppIdentity.cmake"; exit 1; }

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

one() {  # one <glob>: the single file that matches, or fail
    local matches=("$@")
    if [ "${#matches[@]}" -ne 1 ] || [ ! -e "${matches[0]}" ]; then
        echo "::error::expected exactly one of: $*"
        exit 1
    fi
    printf '%s' "${matches[0]}"
}

case "$(uname -s)" in
Linux)
    deb="$(one "$dir"/Cubeulator-*.deb)"
    rpm="$(one "$dir"/Cubeulator-*.rpm)"
    echo "== $deb"
    dpkg-deb -f "$deb" Package Version Maintainer Depends Recommends
    contents="$(mktemp)"
    dpkg-deb -c "$deb" > "$contents"
    rpm_contents="$(mktemp)"
    rpm -qlp "$rpm" > "$rpm_contents"

    require "the executable is in the package's private directory" grep -q 'usr/lib/cubeulator/bin/cubeulator$' "$contents"
    require "/usr/bin/cubeulator launches it"                       grep -q ' ./usr/bin/cubeulator' "$contents"
    require "Qt is bundled (.deb)"                                  grep -q 'libQt6Core\.so' "$contents"
    require "Qt is bundled (.rpm)"                                  grep -q 'libQt6Core\.so' "$rpm_contents"
    require "the QML modules are bundled"                           grep -q 'lib/cubeulator/qml/QtQuick' "$contents"
    require "the xcb platform plugin is bundled"                    grep -q 'libqxcb\.so' "$contents"
    require "the desktop entry is named for the identifier"         grep -q "usr/share/applications/${app_id}\.desktop" "$contents"
    require "the AppStream metadata is named for the identifier"    grep -q "usr/share/metainfo/${app_id}\.metainfo\.xml" "$contents"
    require "the scalable icon is named for the identifier"         grep -q "icons/hicolor/scalable/apps/${app_id}\.svg" "$contents"
    require "the 256 px icon is named for the identifier"           grep -q "icons/hicolor/256x256/apps/${app_id}\.png" "$contents"
    require "the licence is installed"                              grep -q 'usr/share/doc/cubeulator/LICENSE\.txt' "$contents"
    require "the third-party notices are installed"                 grep -q 'usr/share/doc/cubeulator/THIRD-PARTY-NOTICES\.txt' "$contents"
    # Files or directories directly in /usr that are not bin, lib or share: Qt's
    # deployment puts its translations in <prefix>/translations unless told
    # otherwise, which is /usr/translations.
    require "nothing is dropped in /usr itself"                     bash -c "! grep -E ' \./usr/[^/]+/?\$' '$contents' | grep -v -E ' \./usr/(bin|lib|share)/?\$'"
    require "Qt's translations are in the private directory"        grep -q 'usr/lib/cubeulator/translations/qt_de\.qm' "$contents"
    require "qt.conf points Qt at them"                             bash -c "dpkg-deb --fsys-tarfile '$deb' | tar -xO ./usr/lib/cubeulator/bin/qt.conf | grep -qx 'Translations = lib/cubeulator/translations'"
    # What the package cannot give honest dependencies for, or does not carry.
    require "the GTK 3 theme plugin is not bundled"                 bash -c "! grep -q 'libqgtk3\.so' '$contents'"
    require "no Wayland client libraries or integrations (xcb only)" bash -c "! grep -q -E 'libQt6Wayland|libQt6WlShell|plugins/wayland-' '$contents'"
    require "no QML debugger or profiler plugins"                   bash -c "! grep -q 'plugins/qmltooling' '$contents'"
    require "the .deb recommends a CJK font"                        bash -c "dpkg-deb -f '$deb' Recommends | grep -q fonts-noto-cjk"
    require "the .deb names its maintainer"                         bash -c "dpkg-deb -f '$deb' Maintainer | grep -q ."

    # The metadata a software centre and a menu read.
    unpacked="$(mktemp -d)"
    dpkg-deb -x "$deb" "$unpacked"
    if command -v desktop-file-validate >/dev/null; then
        require "the desktop entry validates"                       desktop-file-validate "$unpacked/usr/share/applications/${app_id}.desktop"
    else
        echo "::warning::desktop-file-validate is not installed: the desktop entry was not validated"
    fi
    if command -v appstreamcli >/dev/null; then
        require "the AppStream metadata validates"                  appstreamcli validate --no-net "$unpacked/usr/share/metainfo/${app_id}.metainfo.xml"
    else
        echo "::warning::appstreamcli is not installed: the AppStream metadata was not validated"
    fi

    # Every system library the files link against is one the .deb's Depends
    # provides (packaging/linux/check-depends.sh says why a list written by hand
    # needs this). Runs ldd over every ELF, so it needs a Linux with the
    # libraries the build used, which this is.
    # Run in the open, not through require(): when it fails, which libraries
    # are missing is the whole message.
    if bash "$root/packaging/linux/check-depends.sh" "$deb"; then
        echo "ok      the .deb's Depends covers every system library it links"
    else
        echo "::error::package check failed: the .deb's Depends does not cover every system library it links"
        failures=$((failures + 1))
    fi
    ;;

Darwin)
    dmg="$(one "$dir"/Cubeulator-*.dmg)"
    echo "== $dmg"
    mount="$(mktemp -d)"
    hdiutil attach "$dmg" -mountpoint "$mount" -nobrowse -quiet
    trap 'hdiutil detach "$mount" -quiet || true' EXIT
    app="$mount/Cubeulator.app"
    plist="$app/Contents/Info.plist"

    require "the executable is in the bundle"                       test -x "$app/Contents/MacOS/Cubeulator"
    require "Qt is bundled"                                         test -d "$app/Contents/Frameworks/QtCore.framework"
    require "the QML modules are bundled"                           test -d "$app/Contents/Resources/qml/QtQuick"
    require "the cocoa platform plugin is bundled"                  test -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib"
    require "the icon is in the bundle"                             test -f "$app/Contents/Resources/Cubeulator.icns"
    require "the bundle id is the identifier"                       bash -c "/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' '$plist' | grep -qx '$app_id'"
    require "the camera usage string is set"                        bash -c "/usr/libexec/PlistBuddy -c 'Print :NSCameraUsageDescription' '$plist' | grep -q ."
    require "the version is set"                                    bash -c "/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' '$plist' | grep -q ."
    require "the app carries a signature (ad-hoc or better)"        codesign --verify --deep "$app"
    require "no QML debugger or profiler plugins"                   test ! -d "$app/Contents/PlugIns/qmltooling"

    # A library the build host had and a person's Mac does not: Homebrew's
    # libomp was one the release workflow installed, and nothing in the bundle
    # may load a library from outside it and the system.
    outside="$(
        while IFS= read -r -d '' f; do
            otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' |
                grep -E '^/(opt|usr/local|Users|private)/' || true
        done < <(find "$app" -type f \( -name '*.dylib' -o -perm -u+x \) -print0) | sort -u
    )"
    if [ -n "$outside" ]; then
        echo "libraries loaded from outside the bundle and the system:"
        echo "$outside" | head -10
    fi
    require "nothing in the bundle loads a library from the build host"   test -z "$outside"
    require "the licence is on the disk image"                      test -f "$mount/LICENSE.txt"
    require "the third-party notices are on the disk image"         test -f "$mount/THIRD-PARTY-NOTICES.txt"
    /usr/libexec/PlistBuddy -c 'Print :NSCameraUsageDescription' "$plist"
    codesign -dv "$app" 2>&1 | sed -n '1,8p' || true
    ;;

MINGW* | MSYS* | CYGWIN*)
    installer="$(one "$dir"/Cubeulator-*.exe)"
    echo "== $installer"
    sevenzip="/c/Program Files/7-Zip/7z.exe"
    [ -x "$sevenzip" ] || sevenzip="$(command -v 7z || true)"
    [ -n "$sevenzip" ] || { echo "::error::7-Zip not found, cannot look inside the installer"; exit 1; }
    contents="$(mktemp)"
    "$sevenzip" l "$installer" > "$contents"

    require "the executable is in bin"                              grep -q 'bin[\\/]Cubeulator\.exe' "$contents"
    require "Qt is bundled"                                         grep -q 'bin[\\/]Qt6Core\.dll' "$contents"
    require "the windows platform plugin is bundled"                grep -q 'plugins[\\/]platforms[\\/]qwindows\.dll' "$contents"
    require "the QML modules are bundled"                           grep -q 'qml[\\/]QtQuick' "$contents"
    require "OpenCV and its codecs came along (transitive DLLs)"    grep -q 'bin[\\/]libpng16\.dll' "$contents"
    require "the C++ runtime is installed app-locally"              grep -q 'bin[\\/]vcruntime140\.dll' "$contents"
    require "no 25 MB runtime installer is bundled"                 bash -c "! grep -q 'vc_redist' '$contents'"
    require "no QML debugger or profiler plugins"                   bash -c "! grep -q 'qmltooling' '$contents'"
    require "the licence is installed"                              grep -q 'LICENSE\.txt' "$contents"
    require "the third-party notices are installed"                 grep -q 'THIRD-PARTY-NOTICES\.txt' "$contents"
    ;;

*)
    echo "::error::unsupported platform $(uname -s)"
    exit 1
    ;;
esac

if [ "$failures" -ne 0 ]; then
    echo "::error::$failures package check(s) failed"
    exit 1
fi
echo "package ok"
