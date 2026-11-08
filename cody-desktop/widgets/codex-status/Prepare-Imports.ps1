# Explicit dependency preparation. Scan status and integrity records exist only in memory.
[CmdletBinding()]
param([psobject]$PreviousImports, [switch]$ForPackaging, [switch]$WindowsOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$packageRoot = Join-Path $projectRoot '.imports\packages'
$scanner = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
if (-not (Test-Path -LiteralPath $scanner)) { throw 'Microsoft Defender command-line scanner is unavailable. Imports cannot proceed.' }
$defender = Get-MpComputerStatus
if (-not $defender.AntivirusEnabled -or -not $defender.AMServiceEnabled) { throw 'Microsoft Defender is not available for the required scan.' }
$old = @{}
if ($null -ne $PreviousImports) {
    if (-not $PreviousImports.Prepared -or $PreviousImports.ProjectRoot -ne $projectRoot) { throw 'The previous preparation context belongs to another project or is incomplete.' }
    foreach ($entry in $PreviousImports.Packages) { $old[$entry.path] = $entry.sha256 }
}
Push-Location $projectRoot
try {
    $projects = @(
        @{ project='Windows/OpenAIUsage.Windows.csproj'; rid=if ($ForPackaging) { 'win-x64' } }
        if (-not $WindowsOnly) {
            @{ project='Backend/OpenAIUsage.Backend.csproj'; rid=if ($ForPackaging) { 'linux-x64' } }
            @{ project='Tests/OpenAIUsage.Tests.csproj' }
            @{ project='Shared/OpenAIUsage.Core.csproj' }
        }
    )
    foreach ($project in $projects) {
        $arguments = @('restore',$project.project,'--packages',$packageRoot,'--source','https://api.nuget.org/v3/index.json','-p:NuGetAudit=false','--disable-parallel')
        if ($project.rid) { $arguments += @('-r',$project.rid) }
        if ($ForPackaging) { $arguments += '-p:SelfContained=true' }
        & dotnet @arguments | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Dependency staging failed for $($project.project)." }
    }
    $records = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | ForEach-Object {
        [pscustomobject]@{ path=[IO.Path]::GetRelativePath($packageRoot,$_.FullName); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    $changedPackages = @($records | Where-Object { $old[$_.path] -ne $_.sha256 } | ForEach-Object {
        $parts = $_.path -split '[/\\]'
        Join-Path (Join-Path $packageRoot $parts[0]) $parts[1]
    } | Sort-Object -Unique)
    foreach ($target in $changedPackages) {
        # One scoped scan covers the staged archive and its extracted package files.
        & $scanner -Scan -ScanType 3 -File $target -DisableRemediation | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Import scan failed or detected a threat: $target. Do not build or use this import." }
    }
    Write-Host "Dependencies prepared; $($changedPackages.Count) new or changed package directories checked. No scan history is written."
    [pscustomobject]@{ ProjectRoot=$projectRoot; PackageRoot=$packageRoot; Packages=$records; ToolRoot=$null; Tools=@(); Prepared=$true }
}
finally { Pop-Location }
