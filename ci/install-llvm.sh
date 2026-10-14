#!/usr/bin/env bash
# Installs a given LLVM major (clang, clang-tidy, clang-format) from
# apt.llvm.org on an Ubuntu host - used by the clang-format and clang-tidy CI
# jobs, and usable locally on Ubuntu.
#
#   ./ci/install-llvm.sh 23                      # clang-23 clang-tidy-23 clang-format-23
#   ./ci/install-llvm.sh 23 clang-format-23      # just the named packages
#
# Why not Ubuntu's own packages: the archive trails LLVM by a release or two
# (26.04 carries 20-22), so the newest Clang only comes from apt.llvm.org.
#
# Why not apt.llvm.org's llvm.sh: that downloads a script and runs it as root.
# This adds the archive's source line instead, scoped to a keyring that holds
# only the key whose fingerprint is pinned below (the one apt.llvm.org
# publishes at https://apt.llvm.org/), so a swapped key file is rejected.
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <llvm-major> [package...]" >&2
    exit 2
fi
LLVM_MAJOR="$1"
shift
if [ "$#" -gt 0 ]; then
    PACKAGES=("$@")
else
    PACKAGES=("clang-${LLVM_MAJOR}" "clang-tidy-${LLVM_MAJOR}" "clang-format-${LLVM_MAJOR}")
fi

EXPECTED_FINGERPRINT="6084F3CF814B57C1CF12EFD515CF4D18AF4F7421"
KEY_URL="https://apt.llvm.org/llvm-snapshot.gpg.key"
# Overridable so the script can be exercised without root.
KEYRING="${LLVM_KEYRING:-/usr/share/keyrings/apt.llvm.org-${LLVM_MAJOR}.gpg}"
SOURCES_LIST="${LLVM_SOURCES_LIST:-/etc/apt/sources.list.d/apt-llvm-org-${LLVM_MAJOR}.list}"

# apt.llvm.org publishes one suite per Ubuntu codename and LLVM major.
CODENAME="$(. /etc/os-release && printf '%s' "${VERSION_CODENAME:-}")"
if [ -z "$CODENAME" ]; then
    echo "install-llvm.sh: no VERSION_CODENAME in /etc/os-release; Ubuntu only." >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export GNUPGHOME="$WORK/gnupg"
mkdir -m 700 "$GNUPGHOME"

curl --fail --silent --show-error --location --retry 5 --retry-delay 5 \
    "$KEY_URL" --output "$WORK/llvm-snapshot.gpg.key"

gpg --batch --quiet --import "$WORK/llvm-snapshot.gpg.key"
if ! gpg --batch --with-colons --fingerprint | awk -F: '$1 == "fpr" { print $10 }' \
        | grep --quiet --line-regexp "$EXPECTED_FINGERPRINT"; then
    echo "install-llvm.sh: $KEY_URL does not contain key $EXPECTED_FINGERPRINT; refusing to trust it." >&2
    exit 1
fi

# Export only the pinned key, so nothing else in the downloaded file is trusted.
gpg --batch --export "$EXPECTED_FINGERPRINT" | sudo tee "$KEYRING" > /dev/null
echo "deb [signed-by=${KEYRING}] https://apt.llvm.org/${CODENAME}/ llvm-toolchain-${CODENAME}-${LLVM_MAJOR} main" \
    | sudo tee "$SOURCES_LIST" > /dev/null

sudo apt-get -o Acquire::Retries=5 update
sudo apt-get -o Acquire::Retries=5 install --yes --no-install-recommends "${PACKAGES[@]}"
