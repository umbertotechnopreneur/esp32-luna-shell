[CmdletBinding()]
param(
    [string]$ConfigPath = (
        Join-Path (Split-Path -Parent $PSScriptRoot) "assets\sfx\sfx-config.json"
    )
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedConfig = (Resolve-Path -LiteralPath $ConfigPath).Path
$sfxRoot = Split-Path -Parent $resolvedConfig
$config = Get-Content -Raw -LiteralPath $resolvedConfig | ConvertFrom-Json

$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
$ffprobe = (Get-Command ffprobe -ErrorAction Stop).Source
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

function Format-Decimal([double]$Value) {
    return $Value.ToString("0.###", $invariant)
}

$generated = @()

foreach ($asset in $config.assets) {
    $source = Join-Path $sfxRoot $asset.source
    $output = Join-Path $sfxRoot $asset.output

    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing SFX source: $source"
    }

    $sourcePath = (Resolve-Path -LiteralPath $source).Path
    $outputDirectory = Split-Path -Parent $output
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

    $duration = [double]$asset.duration_s
    $fadeOut = [double]$asset.fade_out_s
    $fadeOutStart = $duration - $fadeOut
    if ($fadeOutStart -lt 0) {
        throw "fade_out_s exceeds duration_s for $($asset.id)"
    }

    $filterParts = @(
        "atrim=start=$(Format-Decimal $asset.trim_start_s):duration=$(Format-Decimal $duration)"
        "asetpts=PTS-STARTPTS"
        "loudnorm=I=$(Format-Decimal $config.normalization.integrated_lufs):TP=$(Format-Decimal $config.normalization.true_peak_db):LRA=$(Format-Decimal $config.normalization.loudness_range)"
        "afade=t=in:st=0:d=$(Format-Decimal $asset.fade_in_s)"
        "afade=t=out:st=$(Format-Decimal $fadeOutStart):d=$(Format-Decimal $fadeOut)"
    )
    $filter = $filterParts -join ","

    $arguments = @(
        "-hide_banner"
        "-loglevel", "error"
        "-y"
        "-i", $sourcePath
        "-af", $filter
        "-ar", [string]$config.target.sample_rate_hz
        "-ac", [string]$config.target.channels
        "-c:a", [string]$config.target.codec
        "-map_metadata", "-1"
        "-fflags", "+bitexact"
        "-flags:a", "+bitexact"
        $output
    )

    & $ffmpeg @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed for $($asset.id) with exit code $LASTEXITCODE"
    }

    $probe = & $ffprobe `
        -v error `
        -show_entries "format=duration,size:stream=codec_name,sample_rate,channels,bits_per_sample" `
        -of json `
        $output | ConvertFrom-Json

    if ($LASTEXITCODE -ne 0) {
        throw "ffprobe failed for $($asset.id) with exit code $LASTEXITCODE"
    }

    $stream = $probe.streams[0]
    if (
        [int]$stream.sample_rate -ne [int]$config.target.sample_rate_hz -or
        [int]$stream.channels -ne [int]$config.target.channels -or
        [string]$stream.codec_name -ne [string]$config.target.codec
    ) {
        throw "Generated SFX format does not match the target for $($asset.id)"
    }

    $generated += [ordered]@{
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
        duration_s = [math]::Round([double]$probe.format.duration, 6)
        bytes = [long]$probe.format.size
    }
}

$manifest = [ordered]@{
    schema_version = 1
    target = $config.target
    generated_assets = $generated
}

$manifestPath = Join-Path $sfxRoot "generated\sfx.manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Output "Generated $($generated.Count) device SFX asset(s)."
Write-Output "Manifest: $manifestPath"
