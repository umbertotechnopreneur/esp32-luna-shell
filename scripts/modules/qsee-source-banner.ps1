<#
.SYNOPSIS
Passive source-ownership banner workflow for QSee.ai-owned C and C++ files.

.DESCRIPTION
Defines one preview/apply function. Importing the file does not enumerate or
modify source files.
#>

function Invoke-QSeeSourceBanner {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string[]]$SourceRoot,
        [switch]$Apply,
        [switch]$Remove,
        [switch]$NoProgress
    )

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
    $bannerMarker = 'Copyright (c) 2026 QSee.ai. All rights reserved.'
    $extensions = @('.c', '.cpp', '.h', '.hpp')
    $excludedSegments = @(
        '.git', '.venv', 'build', 'generated', 'managed_components',
        'third-party', 'third_party', 'vendor'
    )
    $generatedMarkerPattern = '(?im)(auto(?:matically)?[- ]generated|generated file|this file is generated|do not edit)'

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
    Write-QSeeLog -Message "Source banner: operation=$operation mode=$mode repository=$repositoryPath"

    function Get-RelativeQSeeSourcePath {
        param([Parameter(Mandatory)][string]$Path)

        return [System.IO.Path]::GetRelativePath($repositoryPath, $Path).Replace('\', '/')
    }

    function Test-QSeeExcludedSourcePath {
        param([Parameter(Mandatory)][string]$RelativePath)

        foreach ($segment in ($RelativePath -split '/')) {
            if (
                $segment -in $excludedSegments -or
                $segment -like 'build-*' -or
                $segment -like 'build_*'
            ) {
                return $true
            }
        }
        return $false
    }

    # Decode supported text files while retaining the original BOM policy.
    function Read-QSeeSourceText {
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

        try {
            $text = $encoding.GetString($bytes, $offset, $bytes.Length - $offset)
        }
        catch {
            throw "Unsupported or invalid text encoding in '$Path': $($_.Exception.Message)"
        }

        return [pscustomobject]@{
            Text = $text
            Encoding = $encoding
        }
    }

    function Write-QSeeSourceOutcome {
        param(
            [Parameter(Mandatory)][string]$Status,
            [Parameter(Mandatory)][string]$RelativePath,
            [ValidateSet('INFO', 'WARN', 'DEBUG', 'SUCCESS')][string]$Level = 'INFO'
        )

        $message = '{0,-15} {1}' -f $Status, $RelativePath
        Write-Host $message
        Write-QSeeLog -Message $message -Level $Level
    }

    $candidateFiles = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
    foreach ($relativeRoot in $SourceRoot) {
        $sourcePath = [System.IO.Path]::GetFullPath((Join-Path $repositoryPath $relativeRoot))
        if (-not $sourcePath.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Source root escapes the repository: $relativeRoot"
        }
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
            throw "Source root does not exist: $relativeRoot"
        }

        $rootRelative = Get-RelativeQSeeSourcePath -Path $sourcePath
        if (Test-QSeeExcludedSourcePath -RelativePath $rootRelative) {
            throw "Source root is excluded by policy: $relativeRoot"
        }

        Get-ChildItem -LiteralPath $sourcePath -Recurse -File |
            Where-Object { $_.Extension.ToLowerInvariant() -in $extensions } |
            ForEach-Object { $candidateFiles.Add($_) }
    }

    $files = @($candidateFiles | Sort-Object FullName -Unique)
    $planned = 0
    $updated = 0
    $skippedBanner = 0
    $skippedMissingBanner = 0
    $skippedGenerated = 0
    $skippedExcluded = 0
    $fileIndex = 0

    Write-QSeeInfo "Found $($files.Count) owned source candidates."
    foreach ($file in $files) {
        $fileIndex++
        $relativePath = Get-RelativeQSeeSourcePath -Path $file.FullName
        if (-not $NoProgress) {
            $percent = if ($files.Count -eq 0) {
                100
            }
            else {
                [Math]::Floor(($fileIndex / $files.Count) * 100)
            }
            Write-Progress -Id 30 -Activity "QSee.ai source banners: $operation ($mode)" `
                -Status "$fileIndex of $($files.Count): $relativePath" `
                -PercentComplete $percent
        }

        if (Test-QSeeExcludedSourcePath -RelativePath $relativePath) {
            Write-QSeeSourceOutcome -Status 'SKIP excluded' `
                -RelativePath $relativePath -Level WARN
            $skippedExcluded++
            continue
        }

        $source = Read-QSeeSourceText -Path $file.FullName
        $newline = if ($source.Text.Contains("`r`n")) { "`r`n" } else { "`n" }
        $banner = $bannerTemplate.TrimEnd("`r", "`n").Replace("`r`n", "`n").Replace("`n", $newline)

        if ($Remove) {
            $prefixes = @($banner + $newline + $newline, $banner + $newline, $banner)
            $prefix = $prefixes |
                Where-Object { $source.Text.StartsWith($_, [StringComparison]::Ordinal) } |
                Select-Object -First 1
            if ($null -eq $prefix) {
                Write-QSeeSourceOutcome -Status 'SKIP no banner' `
                    -RelativePath $relativePath -Level DEBUG
                $skippedMissingBanner++
                continue
            }

            $planned++
            if (-not $Apply) {
                Write-QSeeSourceOutcome -Status 'PLAN remove' -RelativePath $relativePath
                continue
            }

            [System.IO.File]::WriteAllText(
                $file.FullName,
                $source.Text.Substring($prefix.Length),
                $source.Encoding
            )
            Write-QSeeSourceOutcome -Status 'REMOVED' `
                -RelativePath $relativePath -Level SUCCESS
            $updated++
            continue
        }

        if ($source.Text.Contains($bannerMarker, [StringComparison]::Ordinal)) {
            Write-QSeeSourceOutcome -Status 'SKIP bannered' `
                -RelativePath $relativePath -Level DEBUG
            $skippedBanner++
            continue
        }

        $prefixLength = [Math]::Min(4096, $source.Text.Length)
        if ($source.Text.Substring(0, $prefixLength) -match $generatedMarkerPattern) {
            Write-QSeeSourceOutcome -Status 'SKIP generated' `
                -RelativePath $relativePath -Level WARN
            $skippedGenerated++
            continue
        }

        $planned++
        if (-not $Apply) {
            Write-QSeeSourceOutcome -Status 'PLAN add' -RelativePath $relativePath
            continue
        }

        [System.IO.File]::WriteAllText(
            $file.FullName,
            $banner + $newline + $newline + $source.Text,
            $source.Encoding
        )
        Write-QSeeSourceOutcome -Status 'UPDATED' `
            -RelativePath $relativePath -Level SUCCESS
        $updated++
    }

    if (-not $NoProgress) {
        Write-Progress -Id 30 -Activity 'QSee.ai source banners' -Completed
    }

    Write-Host ''
    Write-Host "Mode: $mode"
    Write-Host "Operation: $operation"
    Write-Host "Candidates: $($files.Count)"
    Write-Host "Planned: $planned"
    Write-Host "Updated: $updated"
    Write-Host "Skipped (already bannered): $skippedBanner"
    Write-Host "Skipped (no removable banner): $skippedMissingBanner"
    Write-Host "Skipped (generated): $skippedGenerated"
    Write-Host "Skipped (excluded): $skippedExcluded"

    if (-not $Apply) {
        $applyCommand = if ($Remove) { '-Remove -Apply' } else { '-Apply' }
        Write-QSeeInfo "Preview only. Re-run with $applyCommand to modify the listed files."
    }

    return [pscustomobject]@{
        Mode = $mode
        Operation = $operation
        Candidates = $files.Count
        Planned = $planned
        Updated = $updated
        SkippedBanner = $skippedBanner
        SkippedMissingBanner = $skippedMissingBanner
        SkippedGenerated = $skippedGenerated
        SkippedExcluded = $skippedExcluded
    }
}
