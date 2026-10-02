# Shared artifact ownership for scripts/device.ps1. The dispatcher supplies ProjectRoot.

# Path is the absolute target whose existing ancestors must be ordinary filesystem entries.
# Throws when an ancestor is a reparse point or cannot be inspected.
function Assert-DeviceArtifactAncestors {
    param([Parameter(Mandatory)][string]$Path)

    $current = [System.IO.Path]::GetFullPath($Path)
    while (-not [string]::IsNullOrEmpty($current)) {
        if (Test-Path -LiteralPath $current -ErrorAction Stop) {
            $entry = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Artifact path contains a reparse point: $current"
            }
        }
        $current = [System.IO.Path]::GetDirectoryName($current)
    }
}

# No parameters: resolve the dispatcher-owned repository artifact directory without creating it.
# Throws when ProjectRoot is absent, invalid, or passes through a reparse point.
function Get-DeviceArtifactsRoot {
    if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
        throw 'Artifact operations require the dispatcher ProjectRoot.'
    }
    $root = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot 'artifacts'))
    Assert-DeviceArtifactAncestors -Path $root
    return $root
}

# RequestedPath is an explicit repository-relative or absolute output path, or empty to use the default.
# DefaultRelativePath is the repository-relative artifact destination used when no path was supplied.
# Throws for an empty destination, artifact root, path escape, alternate stream, or reparse ancestor.
function Resolve-ArtifactPath {
    param(
        [AllowNull()][AllowEmptyString()][string]$RequestedPath,
        [AllowNull()][AllowEmptyString()][string]$DefaultRelativePath
    )

    $candidate = if ([string]::IsNullOrWhiteSpace($RequestedPath)) {
        if ([string]::IsNullOrWhiteSpace($DefaultRelativePath) -or
            [System.IO.Path]::IsPathRooted($DefaultRelativePath)) {
            throw 'An artifact destination requires an explicit path or a repository-relative default.'
        }
        $DefaultRelativePath
    } else {
        $RequestedPath
    }
    $root = Get-DeviceArtifactsRoot
    $resolved = if ([System.IO.Path]::IsPathRooted($candidate)) {
        [System.IO.Path]::GetFullPath($candidate)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $candidate))
    }
    $relative = [System.IO.Path]::GetRelativePath($root, $resolved)
    if ($relative -eq '.' -or $relative -eq '..' -or
        $relative.StartsWith('..\', [StringComparison]::Ordinal) -or
        $relative.StartsWith('../', [StringComparison]::Ordinal) -or
        [System.IO.Path]::IsPathRooted($relative)) {
        throw "Artifact destination must be below '$root': $resolved"
    }
    if ($relative.Contains(':')) {
        throw "Artifact destinations cannot use alternate data streams: $resolved"
    }
    Assert-DeviceArtifactAncestors -Path $resolved
    return $resolved
}

# Path is one owned artifact selected for deletion, including all of its descendants.
# Throws before deletion when the tree contains a reparse point or an unreadable entry.
function Assert-DeviceArtifactTree {
    param([Parameter(Mandatory)][string]$Path)

    Assert-DeviceArtifactAncestors -Path $Path
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push($Path)
    while ($pending.Count -gt 0) {
        $entry = Get-Item -LiteralPath ($pending.Pop()) -Force -ErrorAction Stop
        if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to delete an artifact tree containing a reparse point: $($entry.FullName)"
        }
        if ($entry.PSIsContainer) {
            foreach ($child in Get-ChildItem -LiteralPath $entry.FullName -Force -ErrorAction Stop) {
                $pending.Push($child.FullName)
            }
        }
    }
}

# ProtectedPaths contains active outputs whose whole top-level artifact directory must survive cleanup.
# Throws for invalid protection paths, unsafe trees, inspection failures, or failed deletions.
function Clear-DeviceArtifacts {
    param([string[]]$ProtectedPaths = @())

    $root = Get-DeviceArtifactsRoot
    $preserved = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in @('sdkconfig', 'board-config', 'python', 'managed_components', '.device.lock')) {
        $null = $preserved.Add($name)
    }
    foreach ($path in $ProtectedPaths) {
        if ([string]::IsNullOrWhiteSpace($path)) {
            throw 'A protected artifact path cannot be empty.'
        }
        $resolved = Resolve-ArtifactPath -RequestedPath $path
        $relative = [System.IO.Path]::GetRelativePath($root, $resolved)
        $topLevelName = ($relative -split '[\\/]', 2)[0]
        $null = $preserved.Add($topLevelName)
    }
    if (-not (Test-Path -LiteralPath $root -ErrorAction Stop)) {
        return
    }
    if (-not (Test-Path -LiteralPath $root -PathType Container -ErrorAction Stop)) {
        throw "Artifact root is not a directory: $root"
    }

    $removals = @(
        Get-ChildItem -LiteralPath $root -Force -ErrorAction Stop |
            Where-Object {
                -not $preserved.Contains($_.Name) -and
                -not ($_.PSIsContainer -and $_.Name -like 'build-*')
            }
    )
    # Preflight every removable tree before the first mutation; never follow directory links.
    foreach ($entry in $removals) {
        $resolved = Resolve-ArtifactPath -RequestedPath $entry.FullName
        Assert-DeviceArtifactTree -Path $resolved
    }
    foreach ($entry in $removals) {
        $resolved = Resolve-ArtifactPath -RequestedPath $entry.FullName
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction Stop
        Write-Host "Artifacts: removed $resolved"
    }
}

# No parameters: acquire the exclusive command-output lock until the caller disposes the stream.
# Throws when the artifact directory cannot be created or the operation lock cannot be acquired.
function Open-DeviceArtifactLock {
    $root = Get-DeviceArtifactsRoot
    if (-not (Test-Path -LiteralPath $root -ErrorAction Stop)) {
        $null = New-Item -ItemType Directory -Path $root -ErrorAction Stop
    }
    $lockPath = Resolve-ArtifactPath -RequestedPath (Join-Path $root '.device.lock')
    try {
        return [System.IO.File]::Open($lockPath, [System.IO.FileMode]::OpenOrCreate,
            [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    } catch {
        throw [System.IO.IOException]::new(
            "Cannot acquire the exclusive artifact operation lock '$lockPath': $($_.Exception.Message)",
            $_.Exception)
    }
}
