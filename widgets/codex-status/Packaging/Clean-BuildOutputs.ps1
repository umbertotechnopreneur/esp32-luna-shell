[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [switch]$KeepDeliverables,
    [switch]$KeepDebugDeliverables
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd([IO.Path]::DirectorySeparatorChar)
$projectPrefix = $projectRoot + [IO.Path]::DirectorySeparatorChar

# Reject redirected ancestors before deriving any generated-output targets.
$ancestor = Get-Item -LiteralPath $projectRoot -Force
while ($null -ne $ancestor) {
    if (($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Cleanup refuses a reparse point: $($ancestor.FullName)"
    }
    $ancestor = $ancestor.Parent
}

$versionPath = Join-Path $projectRoot 'Directory.Build.props'
$appVersion = [string](([xml](Get-Content -LiteralPath $versionPath -Raw)).Project.PropertyGroup.Version)
if ($appVersion -notmatch '^\d+\.\d+\.\d+(?:\.\d+)?$') {
    throw "Cleanup requires a numeric application version in $versionPath."
}

function Assert-OutputPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if (-not $absolute.StartsWith($projectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target is outside the project: $absolute"
    }
    $current = $absolute
    while ($current.Length -ge $projectRoot.Length) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Cleanup refuses a reparse point: $current"
        }
        if ($current.Equals($projectRoot, [StringComparison]::OrdinalIgnoreCase)) { break }
        $current = [IO.Path]::GetDirectoryName($current)
    }
    return $absolute
}

function Measure-SafeOutputTree([string]$Path) {
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push((Assert-OutputPath $Path))
    [long]$bytes = 0
    [long]$files = 0
    while ($pending.Count -gt 0) {
        $current = Assert-OutputPath $pending.Pop()
        $item = Get-Item -LiteralPath $current -Force
        if ($item.PSIsContainer) {
            # Enumerate one level at a time so a junction is rejected before traversal.
            foreach ($child in Get-ChildItem -LiteralPath $current -Force) {
                if (($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    throw "Cleanup refuses a reparse point: $($child.FullName)"
                }
                $pending.Push($child.FullName)
            }
        }
        else {
            $bytes += $item.Length
            $files++
        }
    }
    return [pscustomobject]@{ bytes = $bytes; files = $files }
}

$targets = [Collections.Generic.List[string]]::new()
$artifactsPath = Join-Path $projectRoot 'artifacts'
$installerPath = Join-Path $artifactsPath 'installers'
$retainedInstallerNames = if ($KeepDeliverables) { @("OpenAIUsage-$appVersion-windows-x64.msi", "openai-usage_${appVersion}-1_amd64.deb") } else { @() }
$retainedNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in $retainedInstallerNames) {
    [void]$retainedNames.Add($name)
}

if ((Test-Path -LiteralPath $artifactsPath) -and -not ($KeepDeliverables -or $KeepDebugDeliverables)) {
    $targets.Add((Assert-OutputPath $artifactsPath))
}
elseif (Test-Path -LiteralPath $artifactsPath) {
    [void](Assert-OutputPath $artifactsPath)
    foreach ($child in Get-ChildItem -LiteralPath $artifactsPath -Force) {
        [void](Assert-OutputPath $child.FullName)
        if ($KeepDebugDeliverables -and $child.PSIsContainer -and $child.Name -in @('windows-x64', 'linux')) {
            continue
        }
        if ($KeepDeliverables -and $child.Name -ieq 'installers' -and $child.PSIsContainer) {
            foreach ($installerChild in Get-ChildItem -LiteralPath $installerPath -Force) {
                [void](Assert-OutputPath $installerChild.FullName)
                if ($installerChild.PSIsContainer -or -not $retainedNames.Contains($installerChild.Name)) {
                    $targets.Add($installerChild.FullName)
                }
            }
        }
        else {
            $targets.Add($child.FullName)
        }
    }
}

# Imported packages/tools are disposable build inputs; retain no scan reports or inventories.
$importsPath = Join-Path $projectRoot '.imports'
if (Test-Path -LiteralPath $importsPath) { $targets.Add((Assert-OutputPath $importsPath)) }

foreach ($project in @('Windows', 'Shared', 'Backend', 'Tests')) {
    foreach ($directory in @('bin', 'obj')) {
        $target = Join-Path $projectRoot "$project\$directory"
        if (Test-Path -LiteralPath $target) { $targets.Add((Assert-OutputPath $target)) }
    }
}
foreach ($directory in @('Linux', 'Tests', 'Packaging')) {
    $target = Join-Path $projectRoot "$directory\__pycache__"
    if (Test-Path -LiteralPath $target) { $targets.Add((Assert-OutputPath $target)) }
}

# Preflight every candidate before removing anything. Assets, sources and user
# state are never candidates; only the explicit generated paths above qualify.
$plan = foreach ($target in $targets) {
    $measurement = Measure-SafeOutputTree $target
    [pscustomobject]@{ path = $target; bytes = $measurement.bytes; files = $measurement.files }
}
[long]$candidateBytes = 0
[long]$candidateFiles = 0
foreach ($entry in $plan) { $candidateBytes += $entry.bytes; $candidateFiles += $entry.files }
[long]$bytesRemoved = 0
[long]$filesRemoved = 0
$removedTargets = [Collections.Generic.List[string]]::new()
foreach ($entry in $plan) {
    if ($PSCmdlet.ShouldProcess($entry.path, "Remove generated build output ($($entry.files) files, $($entry.bytes) bytes)")) {
        # Recheck both containment and every descendant immediately before deletion.
        $measurement = Measure-SafeOutputTree $entry.path
        $absoluteTarget = Assert-OutputPath $entry.path
        Remove-Item -LiteralPath $absoluteTarget -Recurse -Force -ErrorAction Stop
        $bytesRemoved += $measurement.bytes
        $filesRemoved += $measurement.files
        $removedTargets.Add($absoluteTarget)
    }
}

[pscustomobject]@{
    projectRoot = $projectRoot
    version = $appVersion
    keepDeliverables = [bool]$KeepDeliverables
    keepDebugDeliverables = [bool]$KeepDebugDeliverables
    bytesRemoved = $bytesRemoved
    filesRemoved = $filesRemoved
    targetsRemoved = $removedTargets.ToArray()
    candidateBytes = $candidateBytes
    candidateFiles = $candidateFiles
    candidateTargets = $targets.Count
    retainedInstallerNames = $retainedInstallerNames
}
