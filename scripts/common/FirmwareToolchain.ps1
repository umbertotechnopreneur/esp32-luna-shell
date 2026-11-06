# Selection: Linux path selected by the project.
# Activated: Original ESP-IDF environment path, if set.
function Test-FirmwareActivatedPath([string]$Selection, [string]$Activated) {
    if ([string]::IsNullOrWhiteSpace($Selection) -or [string]::IsNullOrWhiteSpace($Activated)) { return $false }
    $resolvedSelection = Resolve-Path -LiteralPath $Selection -ErrorAction SilentlyContinue
    $resolved = Resolve-Path -LiteralPath $Activated -ErrorAction SilentlyContinue
    if ($null -eq $resolvedSelection -or $null -eq $resolved) { return $false }
    $activatedPath = [IO.Path]::TrimEndingDirectorySeparator($resolved.ProviderPath)
    return $activatedPath -ceq [IO.Path]::TrimEndingDirectorySeparator($resolvedSelection.ProviderPath)
}

# IdfPath: Explicit ESP-IDF checkout or empty to use the host's configured selection.
# ToolsPath: Explicit ESP-IDF tools root or empty to use the host's configured selection.
# PythonExe: Explicit ESP-IDF Python executable or empty to select its virtual environment.
# Throws on unsupported hosts. Returns prospective paths before installation.
function Get-FirmwareToolchainSelection([string]$IdfPath, [string]$ToolsPath, [string]$PythonExe) {
    if (-not $IdfPath) { $IdfPath = $env:LUNASHELL_IDF_PATH }
    if (-not $ToolsPath) { $ToolsPath = $env:LUNASHELL_IDF_TOOLS_PATH }
    if (-not $PythonExe) { $PythonExe = $env:LUNASHELL_IDF_PYTHON }

    $activatedToolsPath = $null
    if ($IsWindows) {
        $legacyRoot = 'E:\Espressif'
        $idfRelativePath = '.espressif/v6.0.2/esp-idf'
        $defaultToolsPath = if (Test-Path -LiteralPath ([IO.Path]::Combine($legacyRoot, $idfRelativePath)) -PathType Container) {
            $legacyRoot
        } else {
            Join-Path "$env:SystemDrive\" 'Espressif'
        }
        if (-not $ToolsPath) { $ToolsPath = $defaultToolsPath }
        if (-not $IdfPath) { $IdfPath = Join-Path $defaultToolsPath $idfRelativePath }
    } elseif ($IsLinux) {
        $managedRoot = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)) '.local/share/luna-shell/esp-idf'
        if (-not $IdfPath) { $IdfPath = $env:IDF_PATH }
        $activatedToolsPath = $env:IDF_TOOLS_PATH
        if (-not $activatedToolsPath) {
            $activatedToolsPath = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)) '.espressif'
        }
        if (-not $ToolsPath) {
            $ToolsPath = if (Test-FirmwareActivatedPath $IdfPath $env:IDF_PATH) {
                $activatedToolsPath
            } else {
                Join-Path $managedRoot 'tools-v6.0.2'
            }
        }
        if (-not $IdfPath) { $IdfPath = Join-Path $managedRoot 'v6.0.2' }
    } else {
        throw 'The firmware workflow supports Windows and Linux.'
    }

    $matchesActivatedIdf = $IsLinux -and (Test-FirmwareActivatedPath $IdfPath $env:IDF_PATH)
    $matchesActivatedTools = $IsLinux -and (Test-FirmwareActivatedPath $ToolsPath $activatedToolsPath)

    if (-not $PythonExe) {
        if ($IsWindows) {
            $idfInstallName = Split-Path -Leaf (Split-Path -Parent $IdfPath)
            $PythonExe = Join-Path $ToolsPath "tools\python\$idfInstallName\venv\Scripts\python.exe"
        } elseif ($matchesActivatedIdf -and $matchesActivatedTools -and $env:IDF_PYTHON_ENV_PATH) {
            $PythonExe = Join-Path $env:IDF_PYTHON_ENV_PATH 'bin/python'
        } else {
            # ESP-IDF names its standard Python environment with the SDK's major.minor version.
            $PythonExe = Join-Path $ToolsPath 'python_env/idf6.0_py3.13_env/bin/python'
        }
    }
    return [pscustomobject]@{
        IdfPath = $IdfPath
        ToolsPath = $ToolsPath
        PythonExe = $PythonExe
        MatchesActivatedTools = $matchesActivatedIdf -and $matchesActivatedTools
    }
}

# IdfPath: Explicit ESP-IDF checkout or empty to use the host's configured selection.
# ToolsPath: Explicit ESP-IDF tools root or empty to use the host's configured selection.
# PythonExe: Explicit ESP-IDF Python executable or empty to select its virtual environment.
# Throws when the selected SDK, tools root or interpreter is missing.
function Resolve-FirmwareToolchainPaths([string]$IdfPath, [string]$ToolsPath, [string]$PythonExe) {
    $selected = Get-FirmwareToolchainSelection -IdfPath $IdfPath -ToolsPath $ToolsPath -PythonExe $PythonExe
    if (-not (Test-Path -LiteralPath $selected.IdfPath -PathType Container)) {
        throw "ESP-IDF not found at '$($selected.IdfPath)'. Run device.ps1 preflight or supply -IdfPath."
    }
    if (-not (Test-Path -LiteralPath $selected.ToolsPath -PathType Container)) {
        throw "Espressif tools directory not found at '$($selected.ToolsPath)'. Run device.ps1 preflight or supply -ToolsPath."
    }
    if (-not (Test-Path -LiteralPath $selected.PythonExe -PathType Leaf)) {
        throw "ESP-IDF Python not found at '$($selected.PythonExe)'. Run device.ps1 preflight or supply -PythonExe."
    }
    $pythonPath = (Resolve-Path -LiteralPath $selected.PythonExe).ProviderPath
    $pythonEnvPath = Split-Path -Parent (Split-Path -Parent $pythonPath)
    return [pscustomobject]@{
        IdfPath = (Resolve-Path -LiteralPath $selected.IdfPath).ProviderPath
        ToolsPath = (Resolve-Path -LiteralPath $selected.ToolsPath).ProviderPath
        PythonExe = $pythonPath
        PythonEnvPath = $pythonEnvPath
        Activated = $selected.MatchesActivatedTools -and
            (Test-FirmwareActivatedPath $pythonEnvPath $env:IDF_PYTHON_ENV_PATH) -and
            $null -ne (Get-Command 'xtensa-esp32s3-elf-gcc' -CommandType Application -ErrorAction SilentlyContinue)
    }
}

# Toolchain: Resolved host paths from Resolve-FirmwareToolchainPaths.
# Throws when the SDK's declared version or the interpreter version is unreadable.
function Get-FirmwareToolchainVersions([pscustomobject]$Toolchain) {
    $versionPath = Join-Path $Toolchain.IdfPath 'tools/cmake/version.cmake'
    $versionSource = Get-Content -LiteralPath $versionPath -Raw
    $versionParts = foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
        $pattern = '(?m)^[ \t]*set\(IDF_VERSION_' + $part + '[ \t]+([0-9]+)\)[ \t]*\r?$'
        $matchesForPart = [regex]::Matches($versionSource, $pattern)
        if ($matchesForPart.Count -ne 1) {
            throw "Invalid ESP-IDF version metadata: $versionPath"
        }
        $matchesForPart[0].Groups[1].Value
    }
    $pythonOutput = @(& $Toolchain.PythonExe --version 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "ESP-IDF Python version probe failed (exit code $LASTEXITCODE)"
    }
    $pythonVersion = [regex]::Match(($pythonOutput -join "`n").Trim(), '\APython ([0-9]+\.[0-9]+\.[0-9]+)\z')
    if (-not $pythonVersion.Success) {
        throw "Invalid ESP-IDF Python version output: $($pythonOutput -join ' | ')"
    }
    return [pscustomobject]@{
        Idf = $versionParts -join '.'
        Python = $pythonVersion.Groups[1].Value
    }
}

# Description: Installation or verification stage displayed to the user.
# Executable: Path to the native program for this stage.
# Arguments: Individual native arguments passed without evaluating shell code.
# Throws with the failed stage and original launch error or exit code.
function Invoke-FirmwareSetupStep([string]$Description, [string]$Executable, [string[]]$Arguments) {
    Write-Host "==> $Description"
    $PSNativeCommandUseErrorActionPreference = $false
    try {
        & $Executable @Arguments 2>&1 | ForEach-Object { Write-Host "$_" }
        $exitCode = $LASTEXITCODE
    } catch {
        throw [InvalidOperationException]::new(
            "$Description could not run '$Executable': $($_.Exception.Message)", $_.Exception)
    }
    if ($exitCode -ne 0) {
        throw "$Description failed (exit code $exitCode from '$Executable')."
    }
}

# ProjectRoot: Firmware checkout that will be used on this host.
# IdfPath: Optional override for the ESP-IDF checkout.
# ToolsPath: Optional override for the Espressif tools root.
# PythonExe: Optional override for the ESP-IDF Python executable.
# Throws at the first failed host setup or verification step.
function Invoke-FirmwarePreflight([string]$ProjectRoot, [string]$IdfPath, [string]$ToolsPath, [string]$PythonExe) {
    Write-Host "PowerShell $($PSVersionTable.PSVersion)"
    if ($IsLinux -and $ProjectRoot -match '\s') {
        throw "Use a space-free checkout path on Ubuntu. Found '$ProjectRoot'."
    }
    $toolchain = if ($IsWindows) {
        . (Join-Path $PSScriptRoot 'FirmwareSetupWindows.ps1')
        Install-FirmwareWindowsPrerequisites -IdfPath $IdfPath -ToolsPath $ToolsPath -PythonExe $PythonExe
    } elseif ($IsLinux) {
        . (Join-Path $PSScriptRoot 'FirmwareSetupUbuntu.ps1')
        Install-FirmwareUbuntuPrerequisites -IdfPath $IdfPath -ToolsPath $ToolsPath -PythonExe $PythonExe
    } else {
        throw 'Firmware preflight supports Windows and Ubuntu.'
    }
    Assert-LinuxFirmwarePaths -ProjectRoot $ProjectRoot -Toolchain $toolchain
    $versions = Get-FirmwareToolchainVersions -Toolchain $toolchain
    Write-Host "ESP-IDF $($versions.Idf) ($($toolchain.IdfPath))"
    Write-Host "Python $($versions.Python) ($($toolchain.PythonExe))"
    if ($versions.Idf -cne '6.0.2' -or $versions.Python -cnotmatch '^3\.13\.\d+$') {
        throw 'This repository requires ESP-IDF 6.0.2 and its Python 3.13.x environment.'
    }

    $idfToolsPy = Join-Path $toolchain.IdfPath 'tools/idf_tools.py'
    if (-not (Test-Path -LiteralPath $idfToolsPy -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $toolchain.IdfPath 'tools/idf.py') -PathType Leaf)) {
        throw "Incomplete ESP-IDF installation at '$($toolchain.IdfPath)'."
    }
    $environment = @{
        IDF_TOOLS_PATH = $toolchain.ToolsPath
        IDF_PYTHON_ENV_PATH = $toolchain.PythonEnvPath
    }
    $savedEnvironment = @{}
    try {
        foreach ($name in $environment.Keys) {
            $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
            [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
        }
        Invoke-FirmwareSetupStep -Description 'Verify installed ESP-IDF tools' -Executable $toolchain.PythonExe `
            -Arguments @($idfToolsPy, '--idf-path', $toolchain.IdfPath, 'check')
        Invoke-FirmwareSetupStep -Description 'Verify the ESP-IDF Python environment' -Executable $toolchain.PythonExe `
            -Arguments @($idfToolsPy, '--idf-path', $toolchain.IdfPath, 'check-python-dependencies')
    } finally {
        foreach ($name in $savedEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
        }
    }
    if ($IsLinux) {
        $missing = @(foreach ($name in @('git', 'cmake', 'ninja', 'stat')) {
            if (-not (Get-Command $name -CommandType Application -ErrorAction SilentlyContinue)) { $name }
        })
        if ($missing.Count -gt 0) {
            throw "Verify Ubuntu executable paths failed. Missing from PATH: $($missing -join ', ')."
        }
    }
    Write-Host 'Firmware host prerequisites are available.'
}

# ProjectRoot: Selected firmware checkout.
# Toolchain: Resolved host installation and virtual environment paths.
# Throws for Linux paths containing whitespace, which ESP-IDF does not support.
function Assert-LinuxFirmwarePaths([string]$ProjectRoot, [pscustomobject]$Toolchain) {
    if (-not $IsLinux) { return }
    foreach ($path in @($ProjectRoot, $Toolchain.IdfPath, $Toolchain.ToolsPath, $Toolchain.PythonExe)) {
        if ($path -match '\s') {
            throw "Use space-free checkout and ESP-IDF toolchain paths on Linux. Found '$path'."
        }
    }
}

# Toolchain: Resolved host paths and whether a matching Linux environment is already active.
# IdfToolsPy: Exact SDK's installed idf_tools.py path.
# Throws when ESP-IDF cannot export the selected installed tools.
function Import-FirmwareToolchainEnvironment([pscustomobject]$Toolchain, [string]$IdfToolsPy) {
    $env:IDF_PATH = $Toolchain.IdfPath
    $env:IDF_TOOLS_PATH = $Toolchain.ToolsPath
    $env:IDF_PYTHON_ENV_PATH = $Toolchain.PythonEnvPath

    if ($Toolchain.Activated) { return }

    # ESP-IDF emits literal key/value pairs. Expand only the documented final PATH token.
    $existingPath = $env:PATH
    $pathToken = if ($IsWindows) { '%PATH%' } else { '$PATH' }
    $pathSuffix = [string][IO.Path]::PathSeparator + $pathToken
    $toolExports = & $Toolchain.PythonExe $IdfToolsPy export --format key-value
    if ($LASTEXITCODE -ne 0) {
        throw "Impossibile esportare la toolchain ESP-IDF da $($Toolchain.IdfPath) (exit code $LASTEXITCODE)"
    }
    foreach ($line in $toolExports) {
        if ($line -notmatch '^([A-Z][A-Z0-9_]*)=(.*)$') { continue }
        $name = $Matches[1]
        $value = $Matches[2]
        if ($name -eq 'PATH' -and $value.EndsWith($pathSuffix, [StringComparison]::Ordinal)) {
            $value = if ($existingPath) {
                $value.Substring(0, $value.Length - $pathToken.Length) + $existingPath
            } else {
                $value.Substring(0, $value.Length - $pathSuffix.Length)
            }
        }
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}
