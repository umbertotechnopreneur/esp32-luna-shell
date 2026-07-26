[CmdletBinding()]
param(
    [ValidateSet("healthy", "sd-unformatted", "gps-searching")]
    [string]$Scenario = "healthy",

    [ValidateSet("home", "settings", "audio", "fft", "fft-detail", "imu")]
    [string]$Screen = "home",

    [string]$Capture,

    [switch]$NoSound
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Simulator = Join-Path $PSScriptRoot "qsee_simulator.py"

Push-Location $ProjectRoot
try {
    $Arguments = @($Simulator, "--scenario", $Scenario, "--screen", $Screen)
    if ($Capture) {
        $Arguments += @("--capture", $Capture)
    }
    if ($NoSound) {
        $Arguments += "--no-sound"
    }
    python @Arguments
}
finally {
    Pop-Location
}
