[CmdletBinding()]
param(
    [ValidateSet("healthy", "sd-unformatted", "gps-searching")]
    [string]$Scenario = "healthy",

    [ValidateSet("home", "settings", "chrome-settings", "power-settings", "hardware", "audio", "fft", "fft-detail", "imu")]
    [string]$Screen = "home",

    [ValidateSet("active", "command-hidden", "chrome-hidden", "dimmed", "off", "standby")]
    [string]$IdleState = "active",

    [string]$Capture,

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
    if ($NoSound) {
        $Arguments += "--no-sound"
    }
    if ($PerformanceMode) {
        $Arguments += "--performance-mode"
    }
    python @Arguments
}
finally {
    Pop-Location
}
