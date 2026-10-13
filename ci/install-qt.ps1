# Installs the pinned prebuilt Qt via aqtinstall for local Windows development.
# Mirrors what jurplel/install-qt-action does in CI.
#
#   pwsh ci/install-qt.ps1 [-OutDir C:\Qt]
#
# Then configure with -DCMAKE_PREFIX_PATH=<OutDir>/<QtVersion>/msvc2022_64.

param([string]$OutDir = "C:\Qt")

$ErrorActionPreference = "Stop"
$QtVersion = "6.12.0"
$Arch = "win64_msvc2022_64"

# Qt 6.11+ moved the Windows kits into per-architecture repository folders. The
# last aqtinstall release (3.3.0) predates that and fails with "Failed to locate
# XML data" for 6.12.0; the fix ("Support Qt 6.11+ for Windows x64") is on its
# master branch but unreleased, so install the same pinned commit CI uses. Needs
# git. Go back to a plain `pip install --upgrade aqtinstall` once a release
# newer than 3.3.0 exists.
$AqtSource = "git+https://github.com/miurahr/aqtinstall.git@076e1659807d0b362a3ed684d54c2e9c775eb9c7"
python -m pip install --quiet --upgrade $AqtSource
python -m aqt install-qt windows desktop $QtVersion $Arch -m qtquick3d qtshadertools --outputdir $OutDir

$prefix = Join-Path $OutDir "$QtVersion/msvc2022_64"
Write-Host ""
Write-Host "Qt $QtVersion installed to $prefix"
Write-Host "Configure with:  -DCMAKE_PREFIX_PATH=$prefix"
