[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

Push-Location $repoRoot
try {
    & python ".\scripts\generate_boot_assets.py"
    if ($LASTEXITCODE -ne 0) {
        throw "Boot asset generation failed with exit code $LASTEXITCODE"
    }

    & pwsh -NoProfile -File ".\scripts\generate-sfx.ps1"
    if ($LASTEXITCODE -ne 0) {
        throw "SFX generation failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
