# Explicit preparation only. Builds and tests never invoke antivirus scans.
[CmdletBinding()]
param([psobject]$PreviousImports)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$context = & (Join-Path $projectRoot 'Prepare-Imports.ps1') -PreviousImports $PreviousImports -ForPackaging
$toolRoot = Join-Path $projectRoot '.imports\wix-5.0.2'
$oldTools = @()
if ($null -ne $PreviousImports -and $PreviousImports.ToolRoot -eq $toolRoot) { $oldTools = @($PreviousImports.Tools) }
$reuse = $oldTools.Count -gt 0 -and (Test-Path -LiteralPath $toolRoot)
if ($reuse) {
    $reuse = @(Get-ChildItem -LiteralPath $toolRoot -Recurse -File).Count -eq $oldTools.Count
    foreach ($entry in $oldTools) {
        $file = Join-Path $toolRoot $entry.path
        if (-not (Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -ne $entry.sha256) { $reuse = $false; break }
    }
}
if (-not $reuse) {
    if ((Test-Path -LiteralPath $toolRoot) -and @(Get-ChildItem -LiteralPath $toolRoot -Force).Count) {
        throw 'The WiX staging directory has no valid in-memory preparation context. Clean generated outputs before preparing it again.'
    }
    New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
    foreach ($id in @('wix','wixtoolset.ui.wixext')) {
        $archive = Join-Path $toolRoot "$id.5.0.2.nupkg"
        $source = "https://api.nuget.org/v3-flatcontainer/$id/5.0.2/$id.5.0.2.nupkg"
        Invoke-WebRequest -Uri $source -OutFile $archive
        [IO.Compression.ZipFile]::ExtractToDirectory($archive,(Join-Path $toolRoot $id),$true)
    }
    $scanner = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
    & $scanner -Scan -ScanType 3 -File $toolRoot -DisableRemediation | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'WiX import scan did not finish cleanly.' }
}
$context.ToolRoot = $toolRoot
$context.Tools = @(Get-ChildItem -LiteralPath $toolRoot -Recurse -File | ForEach-Object {
    [pscustomobject]@{ path=[IO.Path]::GetRelativePath($toolRoot,$_.FullName); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
Write-Host 'Packaging imports prepared. Keep the returned object only for the current PowerShell session; no scan reports are saved.'
$context
