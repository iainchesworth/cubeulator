#!/usr/bin/env bash
# Installs the pinned prebuilt Qt via aqtinstall for local Linux/macOS
# development. Mirrors what jurplel/install-qt-action does in CI.
#
#   ./ci/install-qt.sh [OUTDIR]
#
# Then configure with -DCMAKE_PREFIX_PATH=<OUTDIR>/<QtVersion>/<arch>.
set -euo pipefail

QT_VERSION="6.8.3"
OUT_DIR="${1:-$HOME/Qt}"

case "$(uname -s)" in
    Linux*)  HOST="linux";  ARCH="linux_gcc_64"; PREFIX_ARCH="gcc_64" ;;
    Darwin*) HOST="mac";    ARCH="clang_64";     PREFIX_ARCH="macos" ;;
    *) echo "Unsupported host: $(uname -s)" >&2; exit 1 ;;
esac

python3 -m pip install --quiet --upgrade aqtinstall
python3 -m aqt install-qt "$HOST" desktop "$QT_VERSION" "$ARCH" --outputdir "$OUT_DIR"

echo
echo "Qt $QT_VERSION installed to $OUT_DIR/$QT_VERSION/$PREFIX_ARCH"
echo "Configure with:  -DCMAKE_PREFIX_PATH=$OUT_DIR/$QT_VERSION/$PREFIX_ARCH"
