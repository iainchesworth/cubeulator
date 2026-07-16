# Installs the pinned prebuilt Qt via aqtinstall for local Windows development.
# Mirrors what jurplel/install-qt-action does in CI.
#
#   pwsh ci/install-qt.ps1 [-OutDir C:\Qt]
#
# Then configure with -DCMAKE_PREFIX_PATH=<OutDir>/<QtVersion>/msvc2022_64.

param([string]$OutDir = "C:\Qt")

$ErrorActionPreference = "Stop"
$QtVersion = "6.8.3"
$Arch = "win64_msvc2022_64"

python -m pip install --quiet --upgrade aqtinstall
python -m aqt install-qt windows desktop $QtVersion $Arch --outputdir $OutDir

$prefix = Join-Path $OutDir "$QtVersion/msvc2022_64"
Write-Host ""
Write-Host "Qt $QtVersion installed to $prefix"
Write-Host "Configure with:  -DCMAKE_PREFIX_PATH=$prefix"
