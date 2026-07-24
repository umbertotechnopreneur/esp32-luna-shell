[CmdletBinding()]
param(
    [ValidateSet("healthy", "sd-unformatted", "gps-searching")]
    [string]$Scenario = "healthy"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Simulator = Join-Path $PSScriptRoot "qsee_simulator.py"

Push-Location $ProjectRoot
try {
    python $Simulator --scenario $Scenario
}
finally {
    Pop-Location
}
