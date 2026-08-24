<#
.SYNOPSIS
Preview or apply the canonical banner to LUNASHELL-owned source files.

.DESCRIPTION
Without -Apply this command is read-only and prints the planned changes. It
never invokes an ESP-IDF build or device operation. Generated, third-party,
managed-component, vendor, and build output paths are excluded.

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
    [switch]$NoProgress,
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [string[]]$SourceRoot = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$bannerTemplate = @'
/*
 * LUNASHELL firmware
 *
 * Copyright (c) 2026
 */
'@
$bannerMarker = "LUNASHELL firmware`n *`n * Copyright (c) 2026"
$legacyAuthorMarker = 'Author:' + ' Umberto Giacobbi'
$legacyContactMarker = 'Contact: hello@' + 'LUNASHELL.ai'
$legacyWebsiteMarker = 'Author website: https://umbertogiacobbi' + '.biz'
$extensions = @('.c', '.cpp', '.h', '.hpp')
$allowedSourceRoots = @(
    'main',
    'components/LUNASHELL_app',
    'components/LUNASHELL_boot',
    'components/LUNASHELL_logger',
    'components/LUNASHELL_platform'
)
if ($SourceRoot.Count -eq 0) {
    $SourceRoot = @($allowedSourceRoots)
}
$excludedSegments = @(
    '.git', '.venv', 'build', 'generated', 'managed_components',
    'third-party', 'third_party', 'vendor',
    'esp32_s3_touch_amoled_1_75'
)
$generatedMarkerPattern = (
    '(?im)(auto(?:matically)?[- ]generated|generated file|' +
    'this file is generated|do not edit)'
)

$repositoryPath = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
)
if (-not (Test-Path -LiteralPath $repositoryPath -PathType Container)) {
    throw "Repository root does not exist: $repositoryPath"
}
$repositoryPrefix = $repositoryPath + [System.IO.Path]::DirectorySeparatorChar
$mode = if ($Apply) { 'apply' } else { 'preview' }
$operation = if ($Remove) { 'remove' } else { 'add' }

function Get-RelativeSourcePath {
    param([Parameter(Mandatory)][string]$Path)

    return [System.IO.Path]::GetRelativePath($repositoryPath, $Path).Replace(
        '\',
        '/'
    )
}

function Test-ExcludedSourcePath {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [switch]$LeafIsFile
    )

    $segments = @($RelativePath -split '/')
    for ($index = 0; $index -lt $segments.Count; $index++) {
        $segment = $segments[$index]
        $isFileLeaf = $LeafIsFile -and $index -eq ($segments.Count - 1)
        if (
            $segment -in $excludedSegments -or
            (-not $isFileLeaf -and (
                $segment -like 'build-*' -or
                $segment -like 'build_*'
            ))
        ) {
            return $true
        }
    }
    return $false
}

function Read-SourceText {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $offset = 0
    if (
        $bytes.Length -ge 3 -and
        $bytes[0] -eq 0xEF -and
        $bytes[1] -eq 0xBB -and
        $bytes[2] -eq 0xBF
    ) {
        $encoding = [System.Text.UTF8Encoding]::new($true, $true)
        $offset = 3
    }
    elseif (
        $bytes.Length -ge 2 -and
        $bytes[0] -eq 0xFF -and
        $bytes[1] -eq 0xFE
    ) {
        $encoding = [System.Text.UnicodeEncoding]::new($false, $true, $true)
        $offset = 2
    }
    elseif (
        $bytes.Length -ge 2 -and
        $bytes[0] -eq 0xFE -and
        $bytes[1] -eq 0xFF
    ) {
        $encoding = [System.Text.UnicodeEncoding]::new($true, $true, $true)
        $offset = 2
    }
    else {
        $encoding = [System.Text.UTF8Encoding]::new($false, $true)
    }

    try {
        $text = $encoding.GetString($bytes, $offset, $bytes.Length - $offset)
    }
    catch {
        throw "Unsupported or invalid text encoding in '${Path}': $($_.Exception.Message)"
    }

    return [pscustomobject]@{
        Text = $text
        Encoding = $encoding
    }
}

function Get-LeadingHeaderClassification {
    param([Parameter(Mandatory)][string]$Text)

    $match = [regex]::Match(
        $Text,
        '\A/\*.*?\*/(?:\r?\n){0,2}',
        [System.Text.RegularExpressions.RegexOptions]::Singleline
    )
    if (-not $match.Success) {
        return [pscustomobject]@{
            Kind = if ($Text.StartsWith('/*', [StringComparison]::Ordinal)) {
                'Unrecognized'
            }
            else {
                'None'
            }
            Length = 0
        }
    }

    $normalized = $match.Value.Replace("`r`n", "`n")
    if ($normalized.Contains($bannerMarker, [StringComparison]::Ordinal)) {
        return [pscustomobject]@{ Kind = 'Canonical'; Length = $match.Length }
    }
    if (
        $normalized.Contains(
            $legacyAuthorMarker,
            [StringComparison]::Ordinal
        ) -and
        $normalized.Contains(
            $legacyContactMarker,
            [StringComparison]::Ordinal
        ) -and
        $normalized.Contains(
            $legacyWebsiteMarker,
            [StringComparison]::Ordinal
        )
    ) {
        return [pscustomobject]@{ Kind = 'LegacyPersonal'; Length = $match.Length }
    }
    return [pscustomobject]@{ Kind = 'Unrecognized'; Length = $match.Length }
}

function Write-SourceOutcome {
    param(
        [Parameter(Mandatory)][string]$Status,
        [Parameter(Mandatory)][string]$RelativePath
    )

    Write-Host ('{0,-15} {1}' -f $Status, $RelativePath)
}

$candidateFiles = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
foreach ($relativeRoot in $SourceRoot) {
    $sourcePath = [System.IO.Path]::GetFullPath(
        (Join-Path $repositoryPath $relativeRoot)
    )
    if (-not $sourcePath.StartsWith(
        $repositoryPrefix,
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw "Source root escapes the repository: $relativeRoot"
    }
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "Source root does not exist: $relativeRoot"
    }

    $rootRelative = Get-RelativeSourcePath -Path $sourcePath
    $rootAllowed = @(
        $allowedSourceRoots | Where-Object {
            $rootRelative.Equals(
                $_,
                [StringComparison]::OrdinalIgnoreCase
            ) -or $rootRelative.StartsWith(
                "$_/",
                [StringComparison]::OrdinalIgnoreCase
            )
        }
    ).Count -gt 0
    if (-not $rootAllowed) {
        throw "Source root is not LUNASHELL-owned: $relativeRoot"
    }
    if (Test-ExcludedSourcePath -RelativePath $rootRelative) {
        throw "Source root is excluded by policy: $relativeRoot"
    }

    $reparsePoint = @(
        Get-Item -LiteralPath $sourcePath -Force
        Get-ChildItem -LiteralPath $sourcePath -Recurse -Force
    ) |
        Where-Object {
            ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
        } |
        Select-Object -First 1
    if ($null -ne $reparsePoint) {
        throw (
            "Source root contains a reparse point and cannot be modified " +
            "safely: $(Get-RelativeSourcePath -Path $reparsePoint.FullName)"
        )
    }

    Get-ChildItem -LiteralPath $sourcePath -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in $extensions } |
        ForEach-Object { $candidateFiles.Add($_) }
}

$files = @($candidateFiles | Sort-Object FullName -Unique)
$manualReviewFiles = [System.Collections.Generic.List[string]]::new()
if ($Apply -and -not $Remove) {
    foreach ($file in $files) {
        $relativePath = Get-RelativeSourcePath -Path $file.FullName
        if (Test-ExcludedSourcePath -RelativePath $relativePath -LeafIsFile) {
            continue
        }
        $source = Read-SourceText -Path $file.FullName
        $normalizedPrefix = $source.Text.Substring(
            0,
            [Math]::Min(4096, $source.Text.Length)
        ).Replace("`r`n", "`n")
        if ($normalizedPrefix -match $generatedMarkerPattern) {
            continue
        }
        $classification = Get-LeadingHeaderClassification -Text $source.Text
        if ($classification.Kind -eq 'Unrecognized') {
            $manualReviewFiles.Add($relativePath)
        }
    }
    if ($manualReviewFiles.Count -gt 0) {
        throw (
            'Source banner apply refused before writing: unrecognized leading ' +
            'comments require manual review: ' +
            ($manualReviewFiles -join ', ')
        )
    }
}
$planned = 0
$plannedReplace = 0
$updated = 0
$skippedBanner = 0
$skippedMissingBanner = 0
$skippedGenerated = 0
$skippedExcluded = 0
$skippedLeadingHeader = 0
$fileIndex = 0

Write-Host "Source banner: operation=$operation mode=$mode"
Write-Host "Found $($files.Count) owned source candidates."

try {
    foreach ($file in $files) {
        $fileIndex++
        $relativePath = Get-RelativeSourcePath -Path $file.FullName
        if (-not $NoProgress) {
            $percent = if ($files.Count -eq 0) {
                100
            }
            else {
                [Math]::Floor(($fileIndex / $files.Count) * 100)
            }
            Write-Progress -Id 30 `
                -Activity "LUNASHELL source banners: $operation ($mode)" `
                -Status "$fileIndex of $($files.Count): $relativePath" `
                -PercentComplete $percent
        }

        if (Test-ExcludedSourcePath -RelativePath $relativePath -LeafIsFile) {
            Write-SourceOutcome -Status 'SKIP excluded' `
                -RelativePath $relativePath
            $skippedExcluded++
            continue
        }

        $source = Read-SourceText -Path $file.FullName
        $newline = if ($source.Text.Contains("`r`n")) { "`r`n" } else { "`n" }
        $banner = $bannerTemplate.TrimEnd("`r", "`n").Replace(
            "`r`n",
            "`n"
        ).Replace("`n", $newline)

        if ($Remove) {
            $prefixes = @($banner + $newline + $newline, $banner + $newline, $banner)
            $prefix = $prefixes |
                Where-Object {
                    $source.Text.StartsWith($_, [StringComparison]::Ordinal)
                } |
                Select-Object -First 1
            if ($null -eq $prefix) {
                Write-SourceOutcome -Status 'SKIP no banner' `
                    -RelativePath $relativePath
                $skippedMissingBanner++
                continue
            }

            $planned++
            if (-not $Apply) {
                Write-SourceOutcome -Status 'PLAN remove' `
                    -RelativePath $relativePath
                continue
            }

            [System.IO.File]::WriteAllText(
                $file.FullName,
                $source.Text.Substring($prefix.Length),
                $source.Encoding
            )
            Write-SourceOutcome -Status 'REMOVED' -RelativePath $relativePath
            $updated++
            continue
        }

        $normalizedPrefix = $source.Text.Substring(
            0,
            [Math]::Min(4096, $source.Text.Length)
        ).Replace("`r`n", "`n")
        if ($normalizedPrefix.Contains($bannerMarker, [StringComparison]::Ordinal)) {
            Write-SourceOutcome -Status 'SKIP bannered' `
                -RelativePath $relativePath
            $skippedBanner++
            continue
        }
        if ($normalizedPrefix -match $generatedMarkerPattern) {
            Write-SourceOutcome -Status 'SKIP generated' `
                -RelativePath $relativePath
            $skippedGenerated++
            continue
        }

        $classification = Get-LeadingHeaderClassification -Text $source.Text
        if ($classification.Kind -eq 'Unrecognized') {
            Write-SourceOutcome -Status 'REVIEW header' `
                -RelativePath $relativePath
            $skippedLeadingHeader++
            continue
        }
        if ($classification.Kind -eq 'LegacyPersonal') {
            $planned++
            $plannedReplace++
            if (-not $Apply) {
                Write-SourceOutcome -Status 'PLAN replace' `
                    -RelativePath $relativePath
                continue
            }
            [System.IO.File]::WriteAllText(
                $file.FullName,
                $banner + $newline + $newline + $source.Text.Substring(
                    $classification.Length
                ),
                $source.Encoding
            )
            Write-SourceOutcome -Status 'REPLACED' -RelativePath $relativePath
            $updated++
            continue
        }

        $planned++
        if (-not $Apply) {
            Write-SourceOutcome -Status 'PLAN add' -RelativePath $relativePath
            continue
        }

        [System.IO.File]::WriteAllText(
            $file.FullName,
            $banner + $newline + $newline + $source.Text,
            $source.Encoding
        )
        Write-SourceOutcome -Status 'UPDATED' -RelativePath $relativePath
        $updated++
    }
}
finally {
    if (-not $NoProgress) {
        Write-Progress -Id 30 -Activity 'LUNASHELL source banners' -Completed
    }
}

Write-Host ''
Write-Host "Mode: $mode"
Write-Host "Operation: $operation"
Write-Host "Candidates: $($files.Count)"
Write-Host "Planned: $planned"
Write-Host "Planned legacy replacements: $plannedReplace"
Write-Host "Updated: $updated"
Write-Host "Skipped (already bannered): $skippedBanner"
Write-Host "Skipped (no removable banner): $skippedMissingBanner"
Write-Host "Skipped (generated): $skippedGenerated"
Write-Host "Skipped (excluded): $skippedExcluded"
Write-Host "Manual review (unrecognized leading header): $skippedLeadingHeader"

if (-not $Apply) {
    $applyCommand = if ($Remove) { '-Remove -Apply' } else { '-Apply' }
    Write-Host "Preview only. Re-run with $applyCommand to modify the listed files."
}
