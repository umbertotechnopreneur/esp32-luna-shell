[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $ProjectRoot 'scripts/common/Artifacts.ps1')
$Venv = Resolve-ArtifactPath '' "artifacts/python/simulator"
$Python = Join-Path $Venv "Scripts\python.exe"
$Simulator = Join-Path $ProjectRoot "simulator/windows/lunashell_simulator.py"
$Scenarios = Join-Path $ProjectRoot "simulator/windows/scenarios"
$BoardYaml = Join-Path $ProjectRoot "config\boards\waveshare-esp32s3-touch-amoled-1.75\ui-bootstrap.yaml"
$ShellView = Join-Path $ProjectRoot "components\LUNASHELL_app\package_shell\res\root.json"
$BoardValidator = Join-Path $ProjectRoot "scripts/common/board_config.py"
$BootImage = Join-Path $ProjectRoot "assets\branding\LUNASHELL\generated\LUNASHELL-boot-466x466.png"
$MascotImage = Join-Path $ProjectRoot "assets\mascot\generated\luna-idle-head-170x198.png"
$BootSound = Join-Path $ProjectRoot "assets\sfx\generated\lunashell-boot.wav"
$SimulatorArtifacts = Resolve-ArtifactPath '' "artifacts/simulator/windows"
$Dist = Join-Path $SimulatorArtifacts "dist"
$Work = Join-Path $SimulatorArtifacts "build"
$Executable = Join-Path $Dist "ESP32LunaShellDeviceSimulator.exe"

foreach ($requiredPath in @($Simulator, $Scenarios, $BootImage, $MascotImage, $BootSound, $BoardYaml, $ShellView, $BoardValidator)) {
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

& $Python -m pip install --disable-pip-version-check "pyinstaller==6.14.2" "pillow==11.3.0" "pyyaml==6.0.2"
if ($LASTEXITCODE -ne 0) {
    throw "Simulator packaging dependencies failed to install (exit code $LASTEXITCODE)."
}
& $Python -m PyInstaller `
    --noconfirm `
    --clean `
    --windowed `
    --onefile `
    --name "ESP32LunaShellDeviceSimulator" `
    --distpath $Dist `
    --workpath $Work `
    --specpath $Work `
    --paths $ProjectRoot `
    --add-data "$BootImage;assets/branding/LUNASHELL/generated" `
    --add-data "$MascotImage;assets/mascot/generated" `
    --add-data "$BootSound;assets/sfx/generated" `
    --add-data "$Scenarios;scenarios" `
    --add-data "$BoardYaml;config/boards/waveshare-esp32s3-touch-amoled-1.75" `
    --add-data "$ShellView;components/LUNASHELL_app/package_shell/res" `
    $Simulator
if ($LASTEXITCODE -ne 0) {
    throw "Simulator executable build failed (exit code $LASTEXITCODE)."
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf) -or
    (Get-Item -LiteralPath $Executable).Length -le 0) {
    throw "Simulator build completed without a valid executable: $Executable"
}

Write-Host "Windows simulator built at $Executable"
