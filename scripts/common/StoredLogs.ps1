# Host-only downloads of firmware-owned SD logs. The dispatcher owns paths and the artifact lock.

# No parameters: construct the reflected IEEE CRC32 table used by the serial protocol.
function New-DeviceStoredLogCrcTable {
    $table = [uint32[]]::new(256)
    for ($tableIndex = 0; $tableIndex -lt $table.Length; $tableIndex++) {
        $entry = [uint32]$tableIndex
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($entry -band 1) -ne 0) {
                $entry = [uint32](($entry -shr 1) -bxor [uint32]3988292384)
            } else {
                $entry = [uint32]($entry -shr 1)
            }
        }
        $table[$tableIndex] = $entry
    }
    return ,$table
}

# Bytes contains the complete response chunk, including bytes beyond the pinned download length.
# Table is the 256-entry reflected IEEE CRC32 lookup table generated for this transfer.
# Throws if the supplied lookup table does not contain exactly 256 entries.
function Get-DeviceStoredLogCrc32 {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes,
        [Parameter(Mandatory)][ValidateCount(256,256)][uint32[]]$Table
    )

    $checksum = [uint32]::MaxValue
    foreach ($value in $Bytes) {
        $tableIndex = [int](($checksum -bxor [uint32]$value) -band [uint32]255)
        $checksum = [uint32](($checksum -shr 8) -bxor $Table[$tableIndex])
    }
    return [uint32]($checksum -bxor [uint32]::MaxValue)
}

# Text is one unsigned decimal field from the firmware protocol, with no sign or whitespace.
# Field identifies the rejected field without interpreting it as an application setting.
# Throws when the field is malformed or exceeds the protocol's unsigned 64-bit range.
function ConvertFrom-DeviceStoredLogInteger([string]$Text, [string]$Field) {
    $parsedValue = [uint64]0
    if (-not [uint64]::TryParse($Text, [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$parsedValue)) {
        throw [System.IO.InvalidDataException]::new("SD log response has an invalid $Field value '$Text'.")
    }
    return $parsedValue
}

# Serial is the single open port owned by this download; the helper never opens or resets it.
# Token is the unique eight-hex-digit request identity whose response may be accepted.
# TimeoutSeconds is the complete response deadline; unrelated runtime output does not extend it.
# StoredLog identifies the firmware-owned current or previous log for timeout guidance.
# Throws for response timeout, malformed protocol data, firmware ERROR, or the original serial read failure.
function Read-DeviceStoredLogResponse {
    param(
        [Parameter(Mandatory)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory)][string]$Token,
        [Parameter(Mandatory)][int]$TimeoutSeconds,
        [Parameter(Mandatory)][string]$StoredLog
    )

    $deadline = [Diagnostics.Stopwatch]::StartNew()
    $timeoutMilliseconds = [long]$TimeoutSeconds * 1000
    while ($deadline.ElapsedMilliseconds -lt $timeoutMilliseconds) {
        $remainingMilliseconds = $timeoutMilliseconds - $deadline.ElapsedMilliseconds
        $Serial.ReadTimeout = [int][Math]::Max(1, $remainingMilliseconds)
        try {
            $line = $Serial.ReadLine().TrimEnd([char]13)
        } catch [TimeoutException] {
            # The same request deadline governs every ReadLine, even while runtime output keeps arriving.
            continue
        }
        if ($deadline.ElapsedMilliseconds -ge $timeoutMilliseconds) {
            break
        }
        if ($line -notmatch '^LUNA LOG [^ ]+ ([^ ]+)(?: |$)') {
            continue
        }
        if (-not [string]::Equals($Matches[1], $Token, [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        if ($line -match '^LUNA LOG ERROR [0-9A-Fa-f]{8} (-?[0-9]+) (-?[0-9]+)$') {
            $firmwareError = [int]0
            $nativeError = [int]0
            if (-not [int]::TryParse($Matches[1], [Globalization.NumberStyles]::AllowLeadingSign,
                    [Globalization.CultureInfo]::InvariantCulture, [ref]$firmwareError) -or
                -not [int]::TryParse($Matches[2], [Globalization.NumberStyles]::AllowLeadingSign,
                    [Globalization.CultureInfo]::InvariantCulture, [ref]$nativeError)) {
                throw [System.IO.InvalidDataException]::new('SD log ERROR response contains out-of-range error codes.')
            }
            throw [System.IO.IOException]::new("Firmware rejected SD log '$StoredLog': esp_err=$firmwareError, errno=$nativeError.")
        }
        if ($line -notmatch '^LUNA LOG DATA [0-9A-Fa-f]{8} ([0-9]+) ([0-9]+) ([0-9]+) ([0-9A-Fa-f]{8}) (-|(?:[0-9A-Fa-f]{2}){1,128})$') {
            throw [System.IO.InvalidDataException]::new("Malformed SD log response for request $Token.")
        }

        # Copy named fields before calling helpers; PowerShell's automatic Matches is ambient state.
        $revisionText = $Matches[1]
        $offsetText = $Matches[2]
        $totalText = $Matches[3]
        $checksumText = $Matches[4]
        $encodedData = $Matches[5]
        $revision = ConvertFrom-DeviceStoredLogInteger -Text $revisionText -Field 'revision'
        if ($revision -gt [uint32]::MaxValue) {
            throw [System.IO.InvalidDataException]::new('SD log response revision exceeds the unsigned 32-bit protocol range.')
        }
        $offset = ConvertFrom-DeviceStoredLogInteger -Text $offsetText -Field 'offset'
        $total = ConvertFrom-DeviceStoredLogInteger -Text $totalText -Field 'total length'
        $checksum = [uint32]::Parse($checksumText, [Globalization.NumberStyles]::HexNumber,
            [Globalization.CultureInfo]::InvariantCulture)
        $bytes = [byte[]]::new(0)
        if ($encodedData -ne '-') {
            $bytes = [Convert]::FromHexString($encodedData)
        }
        return [pscustomobject]@{
            Revision = $revision
            Offset = $offset
            Total = $total
            Checksum = $checksum
            Bytes = [byte[]]$bytes
        }
    }

    throw [TimeoutException]::new(
        "Timed out after $TimeoutSeconds seconds waiting for SD log '$StoredLog' response $Token. Close any serial monitor and install firmware with the LUNA LOG protocol before retrying; this command does not reset or update the device.")
}

# RequestedPort is the user-selected Windows COM identity; no probe or owner cleanup is performed.
# RequestedBaud is the firmware serial console's baud rate.
# StoredLog selects the firmware-owned current or previous SD log without exposing arbitrary paths.
# Destination is the dispatcher-resolved artifact output; the existing file survives until transfer completion.
# ResponseTimeoutSeconds bounds each complete response, including all unrelated runtime lines.
# Throws the original serial, firmware, integrity, storage or cleanup error after releasing owned resources.
function Invoke-DeviceStoredLog {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RequestedPort,
        [Parameter(Mandatory)][ValidateRange(1,4000000)][int]$RequestedBaud,
        [Parameter(Mandatory)][ValidateSet('current','previous')][string]$StoredLog,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][ValidateRange(1,86400)][int]$ResponseTimeoutSeconds
    )

    $StoredLog = $StoredLog.ToLowerInvariant()
    $normalizedPort = Normalize-PortName $RequestedPort
    $outputPath = Resolve-ArtifactPath -RequestedPath $Destination -DefaultRelativePath ''
    $outputDirectory = [System.IO.Path]::GetDirectoryName($outputPath)
    $temporaryName = [System.IO.Path]::GetFileName($outputPath) + '.partial-' + [Guid]::NewGuid().ToString('N')
    $temporaryPath = Resolve-ArtifactPath -RequestedPath (Join-Path $outputDirectory $temporaryName)
    $serial = $null
    $output = $null
    $ownsTemporaryFile = $false
    $committed = $false
    $transferFailure = $null
    $cleanupFailures = [System.Collections.Generic.List[object]]::new()

    try {
        $output = [System.IO.File]::Open($temporaryPath, [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        $ownsTemporaryFile = $true
        $serial = [System.IO.Ports.SerialPort]::new($normalizedPort, $RequestedBaud,
            [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $serial.Encoding = [System.Text.Encoding]::ASCII
        $serial.NewLine = "`n"
        $serial.Handshake = [System.IO.Ports.Handshake]::None
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        $serial.WriteTimeout = $ResponseTimeoutSeconds * 1000
        $serial.Open()

        Write-Host "SD log: downloading '$StoredLog' from $normalizedPort at $RequestedBaud baud."
        $crcTable = New-DeviceStoredLogCrcTable
        $tokens = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $revision = [uint64]0
        $pinnedTotal = $null
        $offset = [uint64]0
        $nextProgress = [uint64]65536
        $maximumLength = [uint64](2 * 1024 * 1024 + 4096)
        do {
            do {
                $token = [Guid]::NewGuid().ToString('N').Substring(0,8)
            } while (-not $tokens.Add($token))
            $offsetText = $offset.ToString([Globalization.CultureInfo]::InvariantCulture)
            $revisionText = $revision.ToString([Globalization.CultureInfo]::InvariantCulture)
            # Discard a command fragment from an interrupted earlier serial session.
            $serial.WriteLine("`nLUNA LOG READ $token $StoredLog $offsetText $revisionText")
            $response = Read-DeviceStoredLogResponse -Serial $serial -Token $token `
                -TimeoutSeconds $ResponseTimeoutSeconds -StoredLog $StoredLog

            if ($response.Revision -eq 0) {
                throw [System.IO.InvalidDataException]::new('SD log response has revision zero; the source file cannot be pinned.')
            }
            if ($response.Total -gt $maximumLength -or $response.Offset -gt $response.Total -or
                $response.Bytes.Length -gt ($response.Total - $response.Offset)) {
                throw [System.IO.InvalidDataException]::new('SD log response exceeds its reported file bounds or the 2 MiB + 4096 byte limit.')
            }
            if ($response.Offset -ne $offset) {
                throw [System.IO.InvalidDataException]::new("SD log response offset $($response.Offset) does not match requested offset $offset.")
            }
            if ($null -eq $pinnedTotal) {
                $revision = $response.Revision
                $pinnedTotal = $response.Total
            } elseif ($response.Revision -ne $revision -or $response.Total -lt $pinnedTotal) {
                throw [System.IO.InvalidDataException]::new('The SD log changed revision or shrank during download; the previous output file is preserved.')
            }

            $actualChecksum = Get-DeviceStoredLogCrc32 -Bytes $response.Bytes -Table $crcTable
            if ($actualChecksum -ne $response.Checksum) {
                $actualText = '{0:X8}' -f $actualChecksum
                $expectedText = '{0:X8}' -f $response.Checksum
                throw [System.IO.InvalidDataException]::new("SD log CRC32 mismatch at offset ${offset}: expected $expectedText, received $actualText.")
            }

            # The current log may keep growing. Verify all received bytes, but publish only the initial length.
            $bytesToWrite = [int][Math]::Min([long]$response.Bytes.Length, [long]($pinnedTotal - $offset))
            if ($bytesToWrite -eq 0 -and $offset -lt $pinnedTotal) {
                throw [System.IO.InvalidDataException]::new("SD log returned an empty chunk before the pinned end at offset $offset.")
            }
            $output.Write($response.Bytes, 0, $bytesToWrite)
            $offset += [uint64]$bytesToWrite
            if ($offset -ge $nextProgress) {
                Write-Host "SD log: $offset / $pinnedTotal bytes downloaded."
                $nextProgress += 65536
            }
        } while ($offset -lt $pinnedTotal)

        $output.Flush($true)
        $output.Dispose()
        $output = $null
        $serial.Dispose()
        $serial = $null
        # Both paths share one artifact directory, so completion replaces the old file in a single move.
        $safeTemporaryPath = Resolve-ArtifactPath -RequestedPath $temporaryPath
        $safeOutputPath = Resolve-ArtifactPath -RequestedPath $outputPath
        [System.IO.File]::Move($safeTemporaryPath, $safeOutputPath, $true)
        $committed = $true
    } catch {
        $transferFailure = $_
    } finally {
        if ($null -ne $output) {
            try {
                $output.Dispose()
            } catch {
                $cleanupFailures.Add([pscustomobject]@{ Stage = 'closing partial SD log'; Failure = $_ })
            }
        }
        if ($null -ne $serial) {
            try {
                $serial.Dispose()
            } catch {
                $cleanupFailures.Add([pscustomobject]@{ Stage = 'closing SD log serial port'; Failure = $_ })
            }
        }
        if ($ownsTemporaryFile -and -not $committed) {
            try {
                $ownedPath = Resolve-ArtifactPath -RequestedPath $temporaryPath
                [System.IO.File]::Delete($ownedPath)
            } catch {
                $cleanupFailures.Add([pscustomobject]@{ Stage = 'removing partial SD log'; Failure = $_ })
            }
        }
    }

    foreach ($cleanup in $cleanupFailures) {
        if ($null -eq $transferFailure) {
            $transferFailure = $cleanup.Failure
        } else {
            Write-DeviceFailure -FailureRecord $cleanup.Failure -Operation 'sd-log' -Stage $cleanup.Stage `
                -LogUnavailableReason 'The enclosing command log records this additional cleanup failure.'
        }
    }
    if ($null -ne $transferFailure) {
        throw $transferFailure
    }
    Write-Host "SD log saved: $outputPath ($offset bytes, revision $revision)."
}
