[CmdletBinding()]
param(
    [ValidateSet("healthy", "sd-unformatted", "gps-searching")]
    [string]$Scenario = "healthy",

    [ValidateSet("home", "settings", "storage", "chrome-settings", "power-settings", "hardware", "audio", "fft", "fft-detail", "imu", "tof", "tof-settings")]
    [string]$Screen = "home",

    [ValidateSet("active", "command-hidden", "chrome-hidden", "fullscreen", "dimmed", "off", "standby")]
    [string]$IdleState = "active",

    [string]$Capture,

    [switch]$SafeGuide,

    [switch]$NoSound,

    [switch]$PerformanceMode
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Simulator = Join-Path $PSScriptRoot "qsee_simulator.py"

Push-Location $ProjectRoot
try {
    $Arguments = @(
        $Simulator,
        "--scenario",
        $Scenario,
        "--screen",
        $Screen,
        "--idle-state",
        $IdleState
    )
    if ($Capture) {
        $Arguments += @("--capture", $Capture)
    }
    if ($SafeGuide) {
        $Arguments += "--safe-guide"
    }
    if ($NoSound) {
        $Arguments += "--no-sound"
    }
    if ($PerformanceMode) {
        $Arguments += "--performance-mode"
    }
    python -B @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "QSee Windows simulator failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
