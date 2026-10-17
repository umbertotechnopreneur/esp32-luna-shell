# Build-only identity capture. Dot-sourcing defines helpers without probing the host.

# Value is the exact operating-system field; it is never stripped or truncated.
# Field names the bounded field in diagnostics without echoing rejected input.
# MaximumLength is the number of printable ASCII bytes, excluding the terminating NUL.
function Get-FirmwareBuildField {
    param([AllowNull()][AllowEmptyString()][string]$Value,
          [Parameter(Mandatory)][string]$Field,
          [Parameter(Mandatory)][int]$MaximumLength)

    $valueLength = if ($null -eq $Value) { 0 } else { $Value.Length }
    if ([string]::IsNullOrWhiteSpace($Value)) {
        Write-Warning "FirmwareBuildMetadata ${Field}: unavailable (empty or whitespace-only operating-system value; length=$valueLength)."
        return [pscustomobject]@{ Value = ''; State = 'Unavailable' }
    }
    if ($Value.Length -gt $MaximumLength) {
        Write-Warning "FirmwareBuildMetadata ${Field}: unsupported (length=$($Value.Length) exceeds limit=$MaximumLength)."
        return [pscustomobject]@{ Value = ''; State = 'Unsupported' }
    }
    if ($Value -cmatch '[^\x20-\x7e]') {
        Write-Warning "FirmwareBuildMetadata ${Field}: unsupported (nonprintable ASCII character; length=$($Value.Length), limit=$MaximumLength)."
        return [pscustomobject]@{ Value = ''; State = 'Unsupported' }
    }
    return [pscustomobject]@{ Value = $Value; State = 'Available' }
}

# Instant is the actual build-entry instant, or an explicit synthetic fixture instant.
# TimeZone is the compiling OS's local zone, or an explicit fixture zone; no mapping is guessed.
# ComputerName is the exact local machine name, or an explicit synthetic fixture value.
# Throws on timezone conversion failure; unsupported display fields retain an explicit state.
function New-FirmwareBuildMetadata {
    param([Parameter(Mandatory)][DateTimeOffset]$Instant,
          [Parameter(Mandatory)][TimeZoneInfo]$TimeZone,
          [AllowNull()][AllowEmptyString()][string]$ComputerName)

    $zone = Get-FirmwareBuildField -Value $TimeZone.Id -Field 'time zone' -MaximumLength 63
    $computer = Get-FirmwareBuildField -Value $ComputerName -Field 'computer name' -MaximumLength 63
    $localDateTime = ''
    if ($zone.State -ceq 'Available') {
        $local = [TimeZoneInfo]::ConvertTime($Instant, $TimeZone)
        $localDateTime = $local.ToString('dd/MM/yy HH:mm', [Globalization.CultureInfo]::InvariantCulture)
    }
    return [pscustomobject][ordered]@{
        schema_version = 1
        captured_at = $Instant.ToString('O', [Globalization.CultureInfo]::InvariantCulture)
        local_date_time = $localDateTime
        time_zone = $zone.Value
        computer = $computer.Value
        clock_state = $zone.State
        computer_state = $computer.State
    }
}

# No parameters: called only immediately before an actual public firmware build.
# Throws the original acquisition/conversion failure; stale metadata cannot authorize compilation.
function Get-FirmwareBuildMetadata {
    [TimeZoneInfo]::ClearCachedData()
    $zone = [TimeZoneInfo]::Local
    $instant = [DateTimeOffset]::UtcNow
    $computer = [Environment]::MachineName
    return New-FirmwareBuildMetadata -Instant $instant -TimeZone $zone -ComputerName $computer
}

# Value is a previously validated ASCII field, bounded independently of its encoded representation.
# Throws on a non-ASCII/control value rather than generating a different displayed identity.
function ConvertTo-FirmwareBuildCppString {
    param([AllowEmptyString()][Parameter(Mandatory)][string]$Value)
    if ($Value -cmatch '[^\x20-\x7e]') { throw 'Firmware build C++ literal requires printable ASCII.' }
    return '"' + $Value.Replace('\', '\\').Replace('"', '\"') + '"'
}

# BuildDirectory is the selected build directory, below the canonical artifacts owner.
# Metadata is one captured build entry, never read again during serialization.
# Throws on invalid metadata, unsafe paths or any write failure; no compiler is started by this helper.
function Write-FirmwareBuildMetadata {
    param([Parameter(Mandatory)][string]$BuildDirectory,
          [Parameter(Mandatory)][object]$Metadata)

    $generated = Resolve-ArtifactPath (Join-Path $BuildDirectory 'generated') ''
    if ($Metadata.schema_version -ne 1 -or
        $Metadata.clock_state -cnotin @('Available','Unavailable','Unsupported','Error') -or
        $Metadata.computer_state -cnotin @('Available','Unavailable','Unsupported','Error')) {
        throw 'Firmware build metadata has an invalid schema or state.'
    }
    foreach ($field in @('local_date_time','time_zone','computer','captured_at')) {
        if ($Metadata.$field -isnot [string] -or $Metadata.$field -cmatch '[^\x20-\x7e]') {
            throw "Firmware build metadata ${field}: invalid printable ASCII value."
        }
    }
    if ($Metadata.local_date_time.Length -gt 16 -or $Metadata.time_zone.Length -gt 63 -or
        $Metadata.computer.Length -gt 63 -or $Metadata.captured_at.Length -gt 40) {
        throw 'Firmware build metadata exceeds its bounded image representation.'
    }
    if (($Metadata.clock_state -ceq 'Available' -and
         ($Metadata.local_date_time -cnotmatch '^\d{2}/\d{2}/\d{2} \d{2}:\d{2}$' -or
          [string]::IsNullOrWhiteSpace($Metadata.time_zone))) -or
        ($Metadata.clock_state -cne 'Available' -and
         ($Metadata.local_date_time -cne '' -or $Metadata.time_zone -cne '')) -or
        ($Metadata.computer_state -ceq 'Available' -and [string]::IsNullOrWhiteSpace($Metadata.computer)) -or
        ($Metadata.computer_state -cne 'Available' -and $Metadata.computer -cne '')) {
        throw 'Firmware build metadata validity disagrees with its display values.'
    }
    $dateLiteral = ConvertTo-FirmwareBuildCppString $Metadata.local_date_time
    $zoneLiteral = ConvertTo-FirmwareBuildCppString $Metadata.time_zone
    $computerLiteral = ConvertTo-FirmwareBuildCppString $Metadata.computer
    $header = @"
// Captured at actual build entry: $($Metadata.captured_at)
#pragma once
#include "LUNASHELL_build_metadata.hpp"
namespace LUNASHELL::build_metadata_detail {
inline constexpr FirmwareBuildMetadata kCaptured{
    $dateLiteral,
    $zoneLiteral,
    $computerLiteral,
    FirmwareBuildFieldState::$($Metadata.clock_state),
    FirmwareBuildFieldState::$($Metadata.computer_state)
};
}
"@
    $null = New-Item -ItemType Directory -Path $generated -Force -ErrorAction Stop
    $utf8 = [Text.UTF8Encoding]::new($false)
    # Publish the header last. Any failed write stops the build, while the old
    # binary retains the immutable metadata it was actually compiled with.
    $outputs = [ordered]@{
        'firmware-build-metadata.json' = ($Metadata | ConvertTo-Json -Depth 4) + "`n"
        'LUNASHELL_build_metadata_generated.hpp' = $header + "`n"
    }
    foreach ($name in $outputs.Keys) {
        $destination = Resolve-ArtifactPath (Join-Path $generated $name) ''
        $temporary = Resolve-ArtifactPath ($destination + '.tmp') ''
        # Keep a failed temporary artifact for diagnosis. A cleanup exception
        # must not replace the original write/rename failure or start a build.
        [IO.File]::WriteAllText($temporary, $outputs[$name], $utf8)
        Move-Item -LiteralPath $temporary -Destination $destination -Force -ErrorAction Stop
    }
}
