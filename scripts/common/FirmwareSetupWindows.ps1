# PackageId: Exact package identifier in the official WinGet source.
# Description: Installation stage displayed to the user.
# Throws when WinGet is unavailable or the package installation fails.
function Install-WindowsFirmwarePackage([string]$PackageId, [string]$Description) {
    $winget = Get-Command 'winget.exe' -CommandType Application -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw "$Description requires WinGet from Microsoft App Installer. 'winget.exe' was not found."
    }
    Invoke-FirmwareSetupStep -Description $Description -Executable $winget.Source -Arguments @(
        'install', '--id', $PackageId, '--exact', '--source', 'winget', '--silent', '--disable-interactivity',
        '--accept-package-agreements', '--accept-source-agreements'
    )

    # Keep the invoking shell's custom paths and pick up the installer's persistent PATH changes.
    $registeredPaths = @(
        [Environment]::GetEnvironmentVariable('Path', 'Machine')
        [Environment]::GetEnvironmentVariable('Path', 'User')
    )
    $paths = ($env:PATH -split ';') + ($registeredPaths -split ';')
    $env:PATH = ($paths | Where-Object { $_ } | Select-Object -Unique) -join ';'
}

# Executable: Python interpreter to probe for the required 3.13 release series.
# Throws if the interpreter cannot be launched.
function Test-WindowsFirmwarePython([string]$Executable) {
    $PSNativeCommandUseErrorActionPreference = $false
    $versionOutput = @(& $Executable --version 2>&1)
    return $LASTEXITCODE -eq 0 -and ($versionOutput -join "`n") -cmatch '^Python 3\.13\.\d+$'
}

# Returns the first usable Python 3.13 found on PATH or by the Windows Python launcher.
# Throws if an installed interpreter or launcher cannot be started.
function Find-WindowsFirmwarePython {
    $PSNativeCommandUseErrorActionPreference = $false
    foreach ($command in @(Get-Command 'python.exe' -All -CommandType Application -ErrorAction SilentlyContinue)) {
        # Ignore Microsoft Store stubs to keep a missing Python probe non-interactive.
        if ($command.Source -notmatch '[\\/]Microsoft[\\/]WindowsApps[\\/]' -and
            (Test-WindowsFirmwarePython $command.Source)) {
            return $command.Source
        }
    }
    $launcher = Get-Command 'py.exe' -CommandType Application -ErrorAction SilentlyContinue
    if ($launcher -and $launcher.Source -notmatch '[\\/]Microsoft[\\/]WindowsApps[\\/]') {
        $output = @(& $launcher.Source -3.13 -c 'import sys; print(sys.executable)' 2>&1)
        if ($LASTEXITCODE -eq 0 -and $output.Count -eq 1 -and
            (Test-Path -LiteralPath $output[0] -PathType Leaf) -and
            (Test-WindowsFirmwarePython $output[0])) {
            return $output[0]
        }
    }
}

# Python: Verified Python 3.13 interpreter for the SDK's official tools utility.
# IdfTools: Absolute path to the selected SDK's idf_tools.py.
# Action: Read-only vendor check that can report a missing prerequisite.
# Throws on check failures unrelated to missing prerequisites.
function Test-WindowsFirmwareRequirement([string]$Python, [string]$IdfTools, [string]$Action) {
    $PSNativeCommandUseErrorActionPreference = $false
    $result = @(& $Python $IdfTools $Action 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) { return $true }

    $output = $result -join "`n"
    # Only recover from the pinned SDK's explicit missing-prerequisite messages.
    $missingPattern = if ($Action -eq 'check') {
        'The following required tools were not found:'
    } else {
        'The following Python requirements are not satisfied:|packaging cannot be imported\.|espidf\.constraints\.v6\.0\.txt doesn''t exist'
    }
    if ($output -match $missingPattern -and $output -notmatch 'Traceback \(most recent call last\)') {
        Write-Host $output
        return $false
    }
    throw "ESP-IDF prerequisite check '$Action' failed (exit code $exitCode).`n$output"
}

# IdfPath: Optional checkout override. A missing checkout is installed here.
# ToolsPath: Optional ESP-IDF tools root override.
# PythonExe: Optional ESP-IDF virtual environment Python override.
# Throws on the first failed installation stage and retains completed stages.
function Install-FirmwareWindowsPrerequisites([string]$IdfPath, [string]$ToolsPath, [string]$PythonExe) {
    $selection = Get-FirmwareToolchainSelection -IdfPath $IdfPath -ToolsPath $ToolsPath -PythonExe $PythonExe
    $currentFolder = (Get-Location).ProviderPath
    $projectRoot = [IO.Path]::TrimEndingDirectorySeparator((Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).ProviderPath)
    foreach ($name in @('IdfPath', 'ToolsPath', 'PythonExe')) {
        $path = [IO.Path]::GetFullPath($selection.$name, $currentFolder)
        if ($path -eq $projectRoot -or $path.StartsWith("$projectRoot\", [StringComparison]::OrdinalIgnoreCase)) {
            throw "Select a Windows ESP-IDF installation outside the firmware checkout. Found '$path'."
        }
        Assert-DeviceArtifactAncestors -Path $path
        $selection.$name = $path
    }

    $idfTools = Join-Path $selection.IdfPath 'tools/idf_tools.py'
    $sdkExists = Test-Path -LiteralPath $idfTools -PathType Leaf
    if (-not $sdkExists -and (Test-Path -LiteralPath $selection.IdfPath)) {
        throw "The selected SDK destination exists without tools/idf_tools.py: '$($selection.IdfPath)'."
    }
    $pythonExists = Test-Path -LiteralPath $selection.PythonExe -PathType Leaf
    if ($pythonExists -and -not (Test-WindowsFirmwarePython $selection.PythonExe)) {
        throw "The selected ESP-IDF Python executable must run Python 3.13.x: '$($selection.PythonExe)'."
    }
    $pythonEnvPath = Split-Path -Parent (Split-Path -Parent $selection.PythonExe)
    if ($selection.PythonExe -ne [IO.Path]::Combine($pythonEnvPath, 'Scripts', 'python.exe') -or
        ($pythonExists -and -not (Test-Path -LiteralPath (Join-Path $pythonEnvPath 'pyvenv.cfg') -PathType Leaf))) {
        throw "Select the Python executable in an ESP-IDF virtual environment's Scripts directory: '$($selection.PythonExe)'."
    }
    foreach ($folder in @('tools', 'dist')) {
        Assert-DeviceArtifactAncestors -Path (Join-Path $selection.ToolsPath $folder)
    }

    $git = Get-Command 'git.exe' -CommandType Application -ErrorAction SilentlyContinue
    if (-not $git) {
        Install-WindowsFirmwarePackage -PackageId 'Git.Git' -Description 'Install Git for Windows'
        $git = Get-Command 'git.exe' -CommandType Application -ErrorAction SilentlyContinue
        if (-not $git) { throw "Git for Windows installation completed, but 'git.exe' was not found on PATH." }
    }

    $python = $selection.PythonExe
    if (-not $pythonExists) {
        $python = Find-WindowsFirmwarePython
        if (-not $python) {
            Install-WindowsFirmwarePackage -PackageId 'Python.Python.3.13' -Description 'Install Python 3.13 for Windows'
            $python = Find-WindowsFirmwarePython
            if (-not $python) { throw 'Python 3.13 installation completed, but no working Python 3.13.x was found.' }
        }
    }

    if (-not $sdkExists) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $selection.IdfPath) -Force -ErrorAction Stop | Out-Null
        Invoke-FirmwareSetupStep -Description 'Install ESP-IDF 6.0.2 from Espressif GitHub' -Executable $git.Source `
            -Arguments @('clone', '--branch', 'v6.0.2', '--depth', '1', '--recurse-submodules', '--shallow-submodules',
                'https://github.com/espressif/esp-idf.git', $selection.IdfPath)
        if (-not (Test-Path -LiteralPath $idfTools -PathType Leaf)) {
            throw "ESP-IDF installation completed, but '$idfTools' is missing."
        }
    }

    $versions = Get-FirmwareToolchainVersions -Toolchain ([pscustomobject]@{ IdfPath = $selection.IdfPath; PythonExe = $python })
    if ($versions.Idf -cne '6.0.2') { throw "Selected SDK '$($selection.IdfPath)' is ESP-IDF $($versions.Idf). Expected 6.0.2." }

    $savedIdfPath = $env:IDF_PATH
    $savedToolsPath = $env:IDF_TOOLS_PATH
    $savedPythonEnvPath = $env:IDF_PYTHON_ENV_PATH
    try {
        $env:IDF_PATH = $selection.IdfPath
        $env:IDF_TOOLS_PATH = $selection.ToolsPath
        $env:IDF_PYTHON_ENV_PATH = $pythonEnvPath
        # A successful vendor check may be limited to a previously selected non-S3 target.
        $s3Tools = @{
            'xtensa-esp-elf' = 'xtensa-esp32s3-elf-gcc.exe'
            'xtensa-esp-elf-gdb' = 'xtensa-esp-elf-gdb-no-python.exe'
            'esp32ulp-elf' = 'esp32ulp-elf-as.exe'
        }
        $missingS3Tool = $s3Tools.Keys | Where-Object {
            $toolName = $_
            $package = Join-Path $selection.ToolsPath "tools/$toolName"
            $found = Get-ChildItem -LiteralPath $package -Directory -ErrorAction SilentlyContinue | Where-Object {
                Test-Path -LiteralPath (Join-Path $_.FullName "$toolName/bin/$($s3Tools[$toolName])") -PathType Leaf
            } | Select-Object -First 1
            -not $found
        } | Select-Object -First 1
        $vendorToolsReady = $sdkExists -and (Test-WindowsFirmwareRequirement $python $idfTools 'check')
        if (-not $vendorToolsReady -or $missingS3Tool) {
            Invoke-FirmwareSetupStep -Description 'Install missing ESP32-S3 build tools' -Executable $python `
                -Arguments @($idfTools, '--non-interactive', 'install', '--targets=esp32s3')
        }

        if (-not $pythonExists) {
            if ((Test-Path -LiteralPath $pythonEnvPath) -and @(Get-ChildItem -LiteralPath $pythonEnvPath -Force).Count) {
                throw "The selected ESP-IDF virtual environment contains files but has no Python executable: '$pythonEnvPath'."
            }
            New-Item -ItemType Directory -Path $pythonEnvPath -Force -ErrorAction Stop | Out-Null
        }
        if (-not $pythonExists -or -not (Test-WindowsFirmwareRequirement $python $idfTools 'check-python-dependencies')) {
            if ($pythonExists) {
                Invoke-FirmwareSetupStep -Description 'Verify pip in the selected ESP-IDF virtual environment' `
                    -Executable $python -Arguments @('-m', 'pip', '--version')
            }
            Invoke-FirmwareSetupStep -Description 'Install missing ESP-IDF Python dependencies' -Executable $python `
                -Arguments @($idfTools, '--non-interactive', 'install-python-env')
        }
    } finally {
        $env:IDF_PATH = $savedIdfPath
        $env:IDF_TOOLS_PATH = $savedToolsPath
        $env:IDF_PYTHON_ENV_PATH = $savedPythonEnvPath
    }

    return Resolve-FirmwareToolchainPaths -IdfPath $selection.IdfPath -ToolsPath $selection.ToolsPath -PythonExe $selection.PythonExe
}
