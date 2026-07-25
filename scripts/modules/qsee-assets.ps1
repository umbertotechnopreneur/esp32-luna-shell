<#
.SYNOPSIS
Passive asset-generation functions for the QSee.ai script entrypoint.

.DESCRIPTION
Owns deterministic boot-art invocation and SFX conversion. Importing the file
does not read or write assets; callers explicitly choose an operation.
#>

function Format-QSeeInvariantDecimal {
    [CmdletBinding()]
    param([Parameter(Mandatory)][double]$Value)

    return $Value.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
}

# Run deterministic boot generation with optional source/output overrides.
function Invoke-QSeeBootAssetGeneration {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ScriptsRoot,
        [Parameter(Mandatory)][string]$PythonCommand,
        [string]$Source,
        [string]$OutputDirectory
    )

    $generator = Join-Path $ScriptsRoot 'generate_boot_assets.py'
    $arguments = @($generator)
    if (-not [string]::IsNullOrWhiteSpace($Source)) {
        $arguments += @('--source', $Source)
    }
    if (-not [string]::IsNullOrWhiteSpace($OutputDirectory)) {
        $arguments += @('--output-dir', $OutputDirectory)
    }

    Write-QSeeInfo 'Generating deterministic boot assets.'
    Invoke-QSeeNativeCommand -FilePath $PythonCommand -ArgumentList $arguments `
        -FailureMessage 'Boot asset generation failed'
    Write-QSeeSuccess 'Boot assets generated.'
}

# Normalize configured SFX, verify each output and write a hash manifest.
function Invoke-QSeeSfxGeneration {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ConfigPath,
        [switch]$NoProgress
    )

    $resolvedConfig = (Resolve-Path -LiteralPath $ConfigPath).Path
    $sfxRoot = Split-Path -Parent $resolvedConfig
    $config = Get-Content -Raw -LiteralPath $resolvedConfig | ConvertFrom-Json
    $ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
    $ffprobe = (Get-Command ffprobe -ErrorAction Stop).Source
    $generated = [System.Collections.Generic.List[object]]::new()
    $assetIndex = 0
    $assetCount = @($config.assets).Count

    foreach ($asset in $config.assets) {
        $assetIndex++
        if (-not $NoProgress) {
            $percent = if ($assetCount -eq 0) {
                100
            }
            else {
                [Math]::Floor(($assetIndex / $assetCount) * 100)
            }
            Write-Progress -Id 20 -Activity 'Generating QSee.ai SFX' `
                -Status "$assetIndex of ${assetCount}: $($asset.id)" `
                -PercentComplete $percent
        }

        $source = Join-Path $sfxRoot $asset.source
        $output = Join-Path $sfxRoot $asset.output
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Missing SFX source: $source"
        }

        $sourcePath = (Resolve-Path -LiteralPath $source).Path
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $output)) | Out-Null
        $duration = [double]$asset.duration_s
        $fadeOut = [double]$asset.fade_out_s
        $fadeOutStart = $duration - $fadeOut
        if ($fadeOutStart -lt 0) {
            throw "fade_out_s exceeds duration_s for $($asset.id)"
        }

        $filter = @(
            "atrim=start=$(Format-QSeeInvariantDecimal $asset.trim_start_s):duration=$(Format-QSeeInvariantDecimal $duration)"
            'asetpts=PTS-STARTPTS'
            (
                "loudnorm=I=$(Format-QSeeInvariantDecimal $config.normalization.integrated_lufs):" +
                "TP=$(Format-QSeeInvariantDecimal $config.normalization.true_peak_db):" +
                "LRA=$(Format-QSeeInvariantDecimal $config.normalization.loudness_range)"
            )
            "afade=t=in:st=0:d=$(Format-QSeeInvariantDecimal $asset.fade_in_s)"
            "afade=t=out:st=$(Format-QSeeInvariantDecimal $fadeOutStart):d=$(Format-QSeeInvariantDecimal $fadeOut)"
        ) -join ','

        # Strip metadata and enable bit-exact flags for reproducible artifacts.
        $ffmpegArguments = @(
            '-hide_banner',
            '-loglevel', 'error',
            '-y',
            '-i', $sourcePath,
            '-af', $filter,
            '-ar', [string]$config.target.sample_rate_hz,
            '-ac', [string]$config.target.channels,
            '-c:a', [string]$config.target.codec,
            '-map_metadata', '-1',
            '-fflags', '+bitexact',
            '-flags:a', '+bitexact',
            $output
        )
        Invoke-QSeeNativeCommand -FilePath $ffmpeg -ArgumentList $ffmpegArguments `
            -FailureMessage "ffmpeg failed for $($asset.id)"

        # Verify the encoded file before recording it in the manifest.
        $probeJson = & $ffprobe `
            -v error `
            -show_entries 'format=duration,size:stream=codec_name,sample_rate,channels,bits_per_sample' `
            -of json `
            $output
        if ($LASTEXITCODE -ne 0) {
            throw "ffprobe failed for $($asset.id) with exit code $LASTEXITCODE"
        }
        $probe = $probeJson | ConvertFrom-Json
        $stream = $probe.streams[0]
        if (
            [int]$stream.sample_rate -ne [int]$config.target.sample_rate_hz -or
            [int]$stream.channels -ne [int]$config.target.channels -or
            [string]$stream.codec_name -ne [string]$config.target.codec
        ) {
            throw "Generated SFX format does not match the target for $($asset.id)"
        }

        $generated.Add([ordered]@{
            id = [string]$asset.id
            purpose = [string]$asset.purpose
            source = [string]$asset.source
            source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePath).Hash.ToLowerInvariant()
            output = [string]$asset.output
            output_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $output).Hash.ToLowerInvariant()
            codec = [string]$stream.codec_name
            sample_rate_hz = [int]$stream.sample_rate
            channels = [int]$stream.channels
            bits_per_sample = [int]$stream.bits_per_sample
            duration_s = [Math]::Round([double]$probe.format.duration, 6)
            bytes = [long]$probe.format.size
        })
        Write-QSeeLog -Message "Generated SFX asset $($asset.id): $output" -Level SUCCESS
    }

    if (-not $NoProgress) {
        Write-Progress -Id 20 -Activity 'Generating QSee.ai SFX' -Completed
    }

    $manifest = [ordered]@{
        schema_version = 1
        target = $config.target
        generated_assets = @($generated)
    }
    $manifestPath = Join-Path $sfxRoot 'generated\sfx.manifest.json'
    $manifestJson = $manifest | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText(
        $manifestPath,
        $manifestJson + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false)
    )

    Write-QSeeSuccess "Generated $($generated.Count) device SFX asset(s)."
    Write-QSeeInfo "Manifest: $manifestPath"
    return [pscustomobject]@{
        GeneratedCount = $generated.Count
        ManifestPath = $manifestPath
    }
}

# Generate boot and audio assets through the two canonical workflows.
function Invoke-QSeeAllAssetGeneration {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ScriptsRoot,
        [Parameter(Mandatory)][string]$PythonCommand,
        [Parameter(Mandatory)][string]$ConfigPath,
        [string]$BootSource,
        [string]$BootOutputDirectory,
        [switch]$NoProgress
    )

    Invoke-QSeeBootAssetGeneration -ScriptsRoot $ScriptsRoot `
        -PythonCommand $PythonCommand -Source $BootSource `
        -OutputDirectory $BootOutputDirectory
    return Invoke-QSeeSfxGeneration -ConfigPath $ConfigPath -NoProgress:$NoProgress
}
