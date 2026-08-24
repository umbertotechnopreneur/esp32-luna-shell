[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Venv = Join-Path $ProjectRoot ".venv-simulator"
$Python = Join-Path $Venv "Scripts\python.exe"
$Simulator = Join-Path $PSScriptRoot "lunashell_simulator.py"
$Scenarios = Join-Path $PSScriptRoot "scenarios"
$BootImage = Join-Path $ProjectRoot "assets\branding\LUNASHELL\generated\LUNASHELL-boot-466x466.png"
$BootSound = Join-Path $ProjectRoot "assets\sfx\generated\lunashell-boot.wav"
$Dist = Join-Path $PSScriptRoot "dist"
$Work = Join-Path $PSScriptRoot "build"
$Executable = Join-Path $Dist "LUNASHELLDeviceSimulator.exe"

foreach ($requiredPath in @($Simulator, $Scenarios, $BootImage, $BootSound)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required simulator input is missing: $requiredPath"
    }
}

if (-not (Test-Path $Python)) {
    $systemPython = Get-Command python -CommandType Application `
        -ErrorAction Stop | Select-Object -First 1
    & $systemPython.Source -m venv $Venv
    if ($LASTEXITCODE -ne 0 -or
        -not (Test-Path -LiteralPath $Python -PathType Leaf)) {
        throw "Simulator virtual-environment creation failed (exit code $LASTEXITCODE)."
    }
}

& $Python -m pip install --disable-pip-version-check "pyinstaller==6.14.2" "pillow==11.3.0"
if ($LASTEXITCODE -ne 0) {
    throw "Simulator packaging dependencies failed to install (exit code $LASTEXITCODE)."
}
& $Python -m PyInstaller `
    --noconfirm `
    --clean `
    --windowed `
    --onefile `
    --name "LUNASHELLDeviceSimulator" `
    --distpath $Dist `
    --workpath $Work `
    --specpath $Work `
    --add-data "$BootImage;assets/branding/LUNASHELL/generated" `
    --add-data "$BootSound;assets/sfx/generated" `
    --add-data "$Scenarios;scenarios" `
    $Simulator
if ($LASTEXITCODE -ne 0) {
    throw "Simulator executable build failed (exit code $LASTEXITCODE)."
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf) -or
    (Get-Item -LiteralPath $Executable).Length -le 0) {
    throw "Simulator build completed without a valid executable: $Executable"
}

Write-Host "Windows simulator built at $Executable"
