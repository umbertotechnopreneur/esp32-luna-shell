<#
.SYNOPSIS
Adds or removes the canonical QSee.ai banner from owned C and C++ sources.

.DESCRIPTION
Preview is the default. Pass -Apply to write changes or -Remove -Apply to
remove the exact canonical leading banner. Build outputs, generated files,
managed dependencies, vendor code, and third-party sources are excluded.

.EXAMPLE
pwsh -NoProfile -File .\scripts\add-source-banner.ps1

.EXAMPLE
pwsh -NoProfile -File .\scripts\add-source-banner.ps1 -Apply

.EXAMPLE
pwsh -NoProfile -File .\scripts\add-source-banner.ps1 -Remove -Apply
#>

[CmdletBinding()]
param(
    [switch]$Apply,
    [switch]$Remove,
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [string[]]$SourceRoot = @(
        "main",
        "components/qsee_app",
        "components/qsee_boot",
        "components/qsee_logger",
        "components/qsee_platform"
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$bannerTemplate = @'
/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */
'@

$bannerMarker = "Copyright (c) 2026 QSee.ai. All rights reserved."
$extensions = @(".c", ".cpp", ".h", ".hpp")
$excludedSegments = @(
    ".git",
    ".venv",
    "build",
    "generated",
    "managed_components",
    "third-party",
    "third_party",
    "vendor"
)
$generatedMarkerPattern = '(?im)(auto(?:matically)?[- ]generated|generated file|this file is generated|do not edit)'

function Read-SourceText {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $offset = 0

    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $encoding = [System.Text.UTF8Encoding]::new($true, $true)
        $offset = 3
    }
    elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        $encoding = [System.Text.UnicodeEncoding]::new($false, $true, $true)
        $offset = 2
    }
    elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        $encoding = [System.Text.UnicodeEncoding]::new($true, $true, $true)
        $offset = 2
    }
    else {
        $encoding = [System.Text.UTF8Encoding]::new($false, $true)
    }

    return [pscustomobject]@{
        Text = $encoding.GetString($bytes, $offset, $bytes.Length - $offset)
        Encoding = $encoding
    }
}

$repositoryPath = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
)
$repositoryPrefix = $repositoryPath + [System.IO.Path]::DirectorySeparatorChar
if (-not (Test-Path -LiteralPath $repositoryPath -PathType Container)) {
    throw "Repository root does not exist: $repositoryPath"
}

$candidates = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
foreach ($relativeRoot in $SourceRoot) {
    $sourcePath = [System.IO.Path]::GetFullPath((Join-Path $repositoryPath $relativeRoot))
    if (-not $sourcePath.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Source root escapes the repository: $relativeRoot"
    }
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "Source root does not exist: $relativeRoot"
    }

    Get-ChildItem -LiteralPath $sourcePath -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in $extensions } |
        ForEach-Object { $candidates.Add($_) }
}

$files = @($candidates | Sort-Object FullName -Unique)
$planned = 0
$updated = 0
$skipped = 0

foreach ($file in $files) {
    $relativePath = [System.IO.Path]::GetRelativePath($repositoryPath, $file.FullName).Replace('\', '/')
    if (($relativePath -split '/') | Where-Object { $_ -in $excludedSegments }) {
        Write-Host ("SKIP excluded  {0}" -f $relativePath)
        $skipped++
        continue
    }

    $source = Read-SourceText -Path $file.FullName
    $newline = if ($source.Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $banner = $bannerTemplate.TrimEnd("`r", "`n").Replace("`r`n", "`n").Replace("`n", $newline)

    if ($Remove) {
        $prefixes = @($banner + $newline + $newline, $banner + $newline, $banner)
        $prefix = $prefixes | Where-Object {
            $source.Text.StartsWith($_, [System.StringComparison]::Ordinal)
        } | Select-Object -First 1
        if ($null -eq $prefix) {
            Write-Host ("SKIP no banner {0}" -f $relativePath)
            $skipped++
            continue
        }
        $newText = $source.Text.Substring($prefix.Length)
        $verb = "remove"
    }
    else {
        if ($source.Text.Contains($bannerMarker, [System.StringComparison]::Ordinal)) {
            Write-Host ("SKIP bannered  {0}" -f $relativePath)
            $skipped++
            continue
        }
        $prefixLength = [Math]::Min(4096, $source.Text.Length)
        if ($source.Text.Substring(0, $prefixLength) -match $generatedMarkerPattern) {
            Write-Host ("SKIP generated {0}" -f $relativePath)
            $skipped++
            continue
        }
        $newText = $banner + $newline + $newline + $source.Text
        $verb = "add"
    }

    $planned++
    if (-not $Apply) {
        Write-Host ("PLAN {0,-6} {1}" -f $verb, $relativePath)
        continue
    }

    [System.IO.File]::WriteAllText($file.FullName, $newText, $source.Encoding)
    Write-Host ("UPDATED {0,-3} {1}" -f $verb, $relativePath)
    $updated++
}

$mode = if ($Apply) { "apply" } else { "preview" }
$operation = if ($Remove) { "remove" } else { "add" }
Write-Host ""
Write-Host "QSee.ai source banner: operation=$operation mode=$mode"
Write-Host "Candidates: $($files.Count)"
Write-Host "Planned: $planned"
Write-Host "Updated: $updated"
Write-Host "Skipped: $skipped"

if (-not $Apply) {
    Write-Host "Preview only. Re-run with -Apply to write these changes."
}
