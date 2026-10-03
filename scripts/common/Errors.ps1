# Shared failure presentation for the public console and its interactive menu.

# FailureRecord is the original PowerShell error; its cause is never replaced by the advice.
# Returns a next step only when an existing command rejection identifies the situation.
function Get-DeviceFailureAdvice {
    param([Parameter(Mandatory)][System.Management.Automation.ErrorRecord]$FailureRecord)

    $message = $FailureRecord.Exception.Message
    if ($message -match 'Serial port selection cancelled') {
        return $null
    }
    # Many domain errors already name the exact next command. Keep that guidance once.
    if ($message -match '\b(?:Use|Run) device\.ps1|\b(?:Use|Supply) -Port|\bReconnect the device (?:before|and)|\bClose the reported process') {
        return $null
    }
    if ($message -match 'Invalid serial port|Specify a serial port|Specify the serial port') {
        return 'Select a port from the detected list, or supply -Port COM10 or -Port 10.'
    }
    if ($message -match 'Serial port selection .*requires a terminal') {
        return 'Supply -Port COM10 or -Port 10 when running without interactive terminal input.'
    }
    if ($message -match 'Serial port discovery failed') {
        return 'Inspect the Windows device discovery error before retrying; a failed query does not mean no devices are connected.'
    }
    if ($message -match 'Serial port .*not in the detected list|Serial port .*ambiguous Windows device identities|Serial port .*reports Windows device status') {
        return 'Use device.ps1 discovery to inspect the reported Windows device before retrying.'
    }
    if ($message -match 'Serial port .*not present|Serial port .*disappeared|No serial ports|No present serial') {
        return 'Reconnect the device, then retry without -Port to choose from the detected ports.'
    }
    if ($message -match 'Timed out after .*waiting for serial port') {
        return 'Reconnect the device and run device.ps1 discovery to inspect its Windows device status before retrying.'
    }
    if ($message -match 'Serial port .*(busy|not exclusively openable)') {
        return 'Close the monitor or application using that port, then retry. Inspect the reported owner and probe error first.'
    }
    if ($message -match 'Cannot acquire the exclusive artifact operation lock') {
        return 'Check whether another device.ps1 command is still running and inspect the lock error before retrying.'
    }
    if ($message -match 'Artifact destination must be below|Artifact destinations cannot use alternate data streams|Artifact path contains a reparse point') {
        return 'Choose a regular path below artifacts/; for example -LogPath artifacts/logs/device.log.'
    }
    if ($message -match 'OutputPath and LogPath must be different|SizeOutputFile and LogPath must be different') {
        return 'Use separate files for the command output and its log.'
    }
    if ($message -match 'not valid for|BoardConfig selects|Unknown or ambiguous .* tool') {
        return 'Use device.ps1 help to choose the supported action and options.'
    }
    if ($message -match 'ESP-IDF non trovato|Root degli strumenti Espressif non trovata|Installazione ESP-IDF incompleta|Python ESP-IDF non trovato|Strumento ESP-IDF richiesto non trovato|ESP-IDF .*not found|ESP-IDF .*missing|Espressif tools .*not found|ESP-IDF Python .*not found') {
        return 'Check the installed ESP-IDF 6.0.2 environment and the supplied -IdfPath, -ToolsPath and -PythonExe paths.'
    }
    if ($message -match 'failed.*exit code|exit code.*failed') {
        return 'Read the tool output and the operation log, then correct the first reported error before retrying.'
    }

    $exception = $FailureRecord.Exception
    while ($null -ne $exception) {
        if ($exception -is [UnauthorizedAccessException]) {
            return 'Check access to the file or directory named in the error before retrying.'
        }
        if ($exception -is [System.IO.IOException] -and (($exception.HResult -band 0xffff) -in @(32, 33))) {
            return 'Close the application holding the reported output file, then retry.'
        }
        $exception = $exception.InnerException
    }
    return $null
}

# FailureRecord is the original error with its exception, invocation and script stack.
# Operation identifies the public command or menu activity that failed.
# Stage identifies the dispatcher stage that stopped.
# LogPath is an optional resolved artifact log destination.
# LogDetails allows appending diagnostics only while the caller owns the artifact operation lock.
# LogUnavailableReason explains why the caller cannot safely write a diagnostic log.
# Logging failures are reported separately; the caller remains responsible for returning failure.
# Throws if the host cannot display the failure; it never replaces the original error with a logging error.
function Write-DeviceFailure {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][System.Management.Automation.ErrorRecord]$FailureRecord,
        [Parameter(Mandatory)][string]$Operation,
        [Parameter(Mandatory)][string]$Stage,
        [AllowNull()][AllowEmptyString()][string]$LogPath,
        [switch]$LogDetails,
        [AllowNull()][AllowEmptyString()][string]$LogUnavailableReason
    )

    $savedLog = $null
    $loggingError = $LogUnavailableReason
    if ($LogDetails) {
        try {
            # Recheck ownership boundaries just before writing, even for an earlier resolved path.
            $safeLogPath = Resolve-ArtifactPath -RequestedPath $LogPath -DefaultRelativePath ''
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $safeLogPath) -Force -ErrorAction Stop
            $exceptionCodes = [System.Collections.Generic.List[string]]::new()
            $exception = $FailureRecord.Exception
            while ($null -ne $exception) {
                # Wrapped errors keep the Windows/native code at the level where it actually originated.
                $errorCode = '0x{0:X8}' -f $exception.HResult
                $exceptionCodes.Add("$($exception.GetType().FullName): $errorCode")
                $exception = $exception.InnerException
            }
            $details = @(
                ''
                "FAILED [$Operation / $Stage] at $([DateTimeOffset]::UtcNow.ToString('o'))"
                "Reason: $($FailureRecord.Exception.Message)"
                "Error ID: $($FailureRecord.FullyQualifiedErrorId)"
                "Category: $($FailureRecord.CategoryInfo)"
                "Exception: $($FailureRecord.Exception.ToString())"
                "Exception error codes: $($exceptionCodes -join ' | ')"
                "Invocation: $($FailureRecord.InvocationInfo.PositionMessage)"
                "Script stack: $($FailureRecord.ScriptStackTrace)"
                'Error record:'
                ($FailureRecord | Format-List * -Force | Out-String)
            )
            Add-Content -LiteralPath $safeLogPath -Value $details -Encoding utf8 -ErrorAction Stop
            $savedLog = $safeLogPath
        } catch {
            # A locked or unwritable log is a second failure, never a replacement for the first one.
            $loggingError = $_.Exception.Message
        }
    }

    Write-Host "FAILED: $Operation" -ForegroundColor Red
    Write-Host "  Stage: $Stage" -ForegroundColor DarkGray
    foreach ($line in ($FailureRecord.Exception.Message -split '\r?\n')) {
        Write-Host "  Reason: $line" -ForegroundColor Red
    }
    $advice = Get-DeviceFailureAdvice -FailureRecord $FailureRecord
    if ($advice) {
        Write-Host "  Next: $advice" -ForegroundColor Yellow
    }
    if ($savedLog) {
        Write-Host "  Details: $savedLog" -ForegroundColor Gray
    } else {
        if ([string]::IsNullOrWhiteSpace($loggingError)) {
            $loggingError = 'No operation log is open.'
        }
        Write-Host "  Details not written: $loggingError" -ForegroundColor Yellow
    }

    # Opted-in diagnostics retain their normal PowerShell streams for CLI callers and the menu.
    Write-Verbose "Error ID: $($FailureRecord.FullyQualifiedErrorId)"
    Write-Verbose "Invocation: $($FailureRecord.InvocationInfo.PositionMessage)"
    Write-Verbose "Script stack: $($FailureRecord.ScriptStackTrace)"
    Write-Debug $FailureRecord.Exception.ToString()
}
