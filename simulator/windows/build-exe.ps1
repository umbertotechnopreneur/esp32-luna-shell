[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Venv = Join-Path $ProjectRoot ".venv-simulator"
$Python = Join-Path $Venv "Scripts\python.exe"
$Simulator = Join-Path $PSScriptRoot "qsee_simulator.py"
$BootImage = Join-Path $ProjectRoot "assets\branding\qsee\generated\qsee-boot-466x466.png"
$BootSound = Join-Path $ProjectRoot "assets\sfx\generated\qsee-boot.wav"
$Dist = Join-Path $PSScriptRoot "dist"
$Work = Join-Path $PSScriptRoot "build"

if (-not (Test-Path $Python)) {
    python -m venv $Venv
}

& $Python -m pip install --disable-pip-version-check "pyinstaller==6.14.2" "pillow==11.3.0"
& $Python -m PyInstaller `
    --noconfirm `
    --clean `
    --windowed `
    --onefile `
    --name "QSeeDeviceSimulator" `
    --distpath $Dist `
    --workpath $Work `
    --specpath $Work `
    --add-data "$BootImage;assets/branding/qsee/generated" `
    --add-data "$BootSound;assets/sfx/generated" `
    $Simulator

Write-Host "Windows simulator built at $Dist\QSeeDeviceSimulator.exe"
