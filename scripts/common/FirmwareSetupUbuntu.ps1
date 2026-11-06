# Description: Name of the prerequisite inspection step.
# Executable: Trusted Ubuntu executable or selected SDK Python path.
# Arguments: Separate native command arguments.
# Throws when inspection cannot complete and reports its native exit code.
function Get-FirmwareUbuntuStepOutput([string]$Description, [string]$Executable, [string[]]$Arguments) {
    $PSNativeCommandUseErrorActionPreference = $false
    try {
        $output = @(& $Executable @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } catch {
        throw [InvalidOperationException]::new("$Description could not run '$Executable': $($_.Exception.Message)", $_.Exception)
    }
    if ($exitCode -ne 0) {
        throw "${Description} failed (exit code $exitCode): $($output -join ' | ')"
    }
    return $output
}

# Path: Candidate installation directory, which may not exist yet.
# RepositoryPath: Canonical absolute Luna Shell checkout path.
# Throws when the path is inside the checkout or unsuitable for ESP-IDF.
function Assert-FirmwareUbuntuInstallPath([string]$Path, [string]$RepositoryPath) {
    $resolved = Get-FirmwareUbuntuStepOutput 'Resolve installation path' '/usr/bin/realpath' @('-m', '--', $Path)
    $relative = [IO.Path]::GetRelativePath($RepositoryPath, $resolved)
    if ($relative -ne '..' -and -not $relative.StartsWith('../', [StringComparison]::Ordinal)) {
        throw "Firmware prerequisite installation path must be outside the Luna Shell checkout: '$resolved'."
    }
    if ($Path -match '\s' -or $resolved -match '\s') {
        throw "Firmware prerequisite installation paths cannot contain whitespace: '$Path'."
    }
}

# Packages: Ubuntu binary package names required by a setup step.
# Throws if the package database cannot be read.
function Get-FirmwareUbuntuMissingPackages([string[]]$Packages) {
    $status = Get-FirmwareUbuntuStepOutput 'Read installed Ubuntu packages' '/usr/bin/dpkg-query' @(
        '-W', '-f=${binary:Package}\t${db:Status-Status}\n'
    )
    $installed = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($line in $status) {
        if ($line -cmatch '^([^:\t]+)(?::[^\t]+)?\tinstalled$') { $null = $installed.Add($Matches[1]) }
    }
    foreach ($name in $Packages) {
        if (-not $installed.Contains($name)) { $name }
    }
}

# Executable: Candidate Python interpreter.
# RequiresVirtualEnvironment: Require an existing ESP-IDF virtual environment with pip and curses.
# Throws when Python cannot be started or the readiness probe itself fails.
function Test-FirmwareUbuntuPython([string]$Executable, [switch]$RequiresVirtualEnvironment) {
    if (-not $Executable -or -not (Test-Path -LiteralPath $Executable -PathType Leaf)) { return $false }

    $probe = @'
import importlib
import sys

is_venv = sys.argv[1] == 'venv'
if sys.version_info[:2] != (3, 13) or (is_venv and sys.prefix == sys.base_prefix):
    print('not-ready')
    sys.exit(0)

required = ('ensurepip', 'venv', 'ssl', 'zlib', 'ctypes', 'curses')
if is_venv:
    required += ('pip',)
try:
    for name in required:
        importlib.import_module(name)
except ImportError:
    print('not-ready')
else:
    print('ready')
'@
    $pythonKind = if ($RequiresVirtualEnvironment) { 'venv' } else { 'base' }
    $result = Get-FirmwareUbuntuStepOutput "Inspect Python 3.13 at '$Executable'" $Executable @('-I', '-B', '-c', $probe, $pythonKind)
    return $result -ceq 'ready'
}

# Prefix: User-owned destination for the pinned CPython source build.
# Throws at the first failed dependency, download, verification, build or install step.
function Install-FirmwareUbuntuCPython([string]$Prefix) {
    $version = '3.13.18'
    $archiveName = "Python-$version.tar.xz"
    $archiveUrl = "https://www.python.org/ftp/python/$version/$archiveName"
    # SHA-256 from https://www.python.org/downloads/release/python-31318/.
    $archiveHash = 'abb0102e6d4ae027509bcb82327d9dd85515b44a3ba8ad538cfc5e9847029f68'
    $packages = @(Get-FirmwareUbuntuMissingPackages @(
        'build-essential', 'pkg-config', 'xz-utils', 'libbz2-dev', 'libffi-dev', 'libgdbm-dev',
        'liblzma-dev', 'libncurses-dev', 'libreadline-dev', 'libsqlite3-dev', 'libssl-dev',
        'uuid-dev', 'zlib1g-dev'
    ))
    if ($packages.Count -gt 0) {
        Invoke-FirmwareSetupStep 'Install CPython build prerequisites' '/usr/bin/sudo' (@('/usr/bin/apt-get', 'install', '-y', '--') + $packages)
    }

    # mktemp owns this private staging directory. Installed Python lives separately.
    $stagingPath = Get-FirmwareUbuntuStepOutput 'Create CPython staging directory' '/usr/bin/mktemp' @('-d', '/tmp/luna-shell-cpython.XXXXXXXX')
    if ($stagingPath -cnotmatch '\A/tmp/luna-shell-cpython\.[a-zA-Z0-9]{8}\z') {
        throw "Unexpected CPython staging directory '$stagingPath'."
    }
    try {
        $archive = Join-Path $stagingPath $archiveName
        Invoke-FirmwareSetupStep "Download CPython $version source" '/usr/bin/wget' @('--no-config', '--https-only', '--output-document', $archive, $archiveUrl)
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $archiveHash) {
            throw "Verify CPython $version source failed: SHA-256 mismatch for '$archiveUrl'."
        }
        Invoke-FirmwareSetupStep "Extract CPython $version source" '/usr/bin/tar' @('-xJf', $archive, '-C', $stagingPath)
        Push-Location -LiteralPath (Join-Path $stagingPath "Python-$version")
        try {
            Invoke-FirmwareSetupStep "Configure CPython $version" './configure' @("--prefix=$Prefix", '--with-ensurepip=install')
            $jobs = [Math]::Min(8, [Environment]::ProcessorCount)
            Invoke-FirmwareSetupStep "Build CPython $version" '/usr/bin/make' @('-j', "$jobs")
            Invoke-FirmwareSetupStep "Install CPython $version" '/usr/bin/make' @('altinstall')
        } finally {
            Pop-Location
        }
    } finally {
        try {
            Remove-Item -LiteralPath $stagingPath -Recurse -Force -ErrorAction Stop
        } catch {
            Write-Warning "CPython staging cleanup failed at '$stagingPath': $($_.Exception.Message)"
        }
    }
}

# IdfPath: Explicit ESP-IDF checkout or empty to use the configured selection.
# ToolsPath: Explicit ESP-IDF tools root or empty to use the configured selection.
# PythonExe: Explicit ESP-IDF virtual environment interpreter or empty to use the configured selection.
# Throws at the first setup failure and preserves prerequisites already installed.
function Install-FirmwareUbuntuPrerequisites([string]$IdfPath, [string]$ToolsPath, [string]$PythonExe) {
    if (-not $IsLinux -or -not (Test-Path -LiteralPath '/etc/os-release' -PathType Leaf) -or
        (Get-Content -LiteralPath '/etc/os-release' -Raw) -cnotmatch '(?m)^ID="?ubuntu"?\r?$') {
        throw 'Automatic Linux firmware setup requires Ubuntu.'
    }
    $account = Get-FirmwareUbuntuStepOutput 'Identify current Ubuntu user' '/usr/bin/id' @('-un')
    if ($account -ceq 'root') { throw 'Run Luna Shell preflight as your regular Ubuntu user. APT uses sudo when needed.' }

    $selection = Get-FirmwareToolchainSelection -IdfPath $IdfPath -ToolsPath $ToolsPath -PythonExe $PythonExe
    $repositoryPath = Get-FirmwareUbuntuStepOutput 'Resolve Luna Shell checkout' '/usr/bin/realpath' @('--', (Join-Path $PSScriptRoot '../..'))
    Assert-LinuxFirmwarePaths -ProjectRoot $repositoryPath -Toolchain $selection
    $currentDirectory = (Get-Location).ProviderPath
    foreach ($property in @('IdfPath', 'ToolsPath', 'PythonExe')) {
        $selection.$property = [IO.Path]::GetFullPath($selection.$property, $currentDirectory)
    }
    $pythonBin = Split-Path -Parent $selection.PythonExe
    $pythonEnv = Split-Path -Parent $pythonBin
    if ((Split-Path -Leaf $pythonBin) -cne 'bin' -or (Split-Path -Leaf $selection.PythonExe) -cnotmatch '\Apython(?:3(?:\.13)?)?\z') {
        throw "Select an ESP-IDF Python 3.13 virtual environment interpreter under its bin directory: '$($selection.PythonExe)'."
    }

    $userProfile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
    $pythonPrefix = Join-Path $userProfile '.local/share/luna-shell/python/3.13.18'
    foreach ($directory in @($selection.IdfPath, $selection.ToolsPath, $pythonEnv, $pythonPrefix, '/tmp')) {
        Assert-FirmwareUbuntuInstallPath -Path $directory -RepositoryPath $repositoryPath
    }
    $sdkPresent = Test-Path -LiteralPath $selection.IdfPath
    if ($sdkPresent) {
        $versionFile = Join-Path $selection.IdfPath 'tools/cmake/version.cmake'
        $versionText = Get-Content -LiteralPath $versionFile -Raw
        foreach ($entry in @('MAJOR 6', 'MINOR 0', 'PATCH 2')) {
            $part, $version = $entry.Split(' ')
            if ($versionText -cnotmatch "(?m)^[ \t]*set\(IDF_VERSION_$part[ \t]+$version\)[ \t]*\r?$") {
                throw "Inspect ESP-IDF checkout failed: ESP-IDF v6.0.2 is required at '$($selection.IdfPath)'."
            }
        }
    }

    $pythonPresent = Test-Path -LiteralPath $selection.PythonExe -PathType Leaf
    $python = $null
    if ($pythonPresent) {
        if (-not (Test-FirmwareUbuntuPython -Executable $selection.PythonExe -RequiresVirtualEnvironment)) {
            throw "Inspect ESP-IDF Python failed: expected a working Python 3.13 virtual environment with pip and curses at '$($selection.PythonExe)'."
        }
        $python = $selection.PythonExe
    } else {
        if ((Test-Path -LiteralPath $pythonEnv) -and (Get-ChildItem -LiteralPath $pythonEnv -Force | Select-Object -First 1)) {
            throw "Prepare ESP-IDF Python failed: '$pythonEnv' contains files but has no selected interpreter. Choose an empty destination or supply a working virtual environment."
        }
        $managedPython = Join-Path $pythonPrefix 'bin/python3.13'
        $pathPython = Get-Command python3.13 -CommandType Application -ErrorAction SilentlyContinue
        foreach ($candidate in @('/usr/bin/python3.13', $managedPython, $pathPython.Path)) {
            if (Test-FirmwareUbuntuPython -Executable $candidate) {
                $python = $candidate
                break
            }
        }
    }

    $missingPackages = @(Get-FirmwareUbuntuMissingPackages @(
        'ca-certificates', 'git', 'wget', 'flex', 'bison', 'gperf', 'cmake', 'ninja-build',
        'libffi-dev', 'libssl-dev', 'libusb-1.0-0'
    ))
    if ($missingPackages.Count -gt 0 -or -not $python) {
        Invoke-FirmwareSetupStep 'Refresh Ubuntu package metadata' '/usr/bin/sudo' @('/usr/bin/apt-get', '-o', 'APT::Update::Error-Mode=any', 'update')
    }
    if ($missingPackages.Count -gt 0) {
        Invoke-FirmwareSetupStep 'Install Ubuntu firmware packages' '/usr/bin/sudo' (@('/usr/bin/apt-get', 'install', '-y', '--') + $missingPackages)
    }
    if (-not $python) {
        $pythonPackages = @(Get-FirmwareUbuntuStepOutput 'Find Ubuntu Python 3.13 packages' '/usr/bin/apt-cache' @('pkgnames', 'python3.13'))
        if ('python3.13' -cin $pythonPackages -and 'python3.13-venv' -cin $pythonPackages) {
            Invoke-FirmwareSetupStep 'Install Ubuntu Python 3.13 and venv' '/usr/bin/sudo' @('/usr/bin/apt-get', 'install', '-y', '--', 'python3.13', 'python3.13-venv')
            $python = '/usr/bin/python3.13'
        } else {
            if (Test-Path -LiteralPath $managedPython) {
                throw "Prepare Python 3.13 failed: existing managed interpreter '$managedPython' is unusable."
            }
            Install-FirmwareUbuntuCPython -Prefix $pythonPrefix
            $python = $managedPython
        }
        if (-not (Test-FirmwareUbuntuPython -Executable $python)) {
            throw "Verify Python 3.13 failed: '$python' is missing or lacks required Python modules."
        }
    }

    if (-not $sdkPresent) {
        $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $selection.IdfPath))
        Invoke-FirmwareSetupStep 'Download ESP-IDF v6.0.2' '/usr/bin/git' @(
            'clone', '--depth', '1', '--branch', 'v6.0.2', '--recurse-submodules', '--shallow-submodules',
            'https://github.com/espressif/esp-idf.git', $selection.IdfPath
        )
    }
    $idfTools = Join-Path $selection.IdfPath 'tools/idf_tools.py'
    if (-not (Test-Path -LiteralPath $idfTools -PathType Leaf)) {
        throw "Inspect ESP-IDF installation failed: '$idfTools' is missing."
    }

    $environment = @{
        IDF_PATH = $selection.IdfPath
        IDF_TOOLS_PATH = $selection.ToolsPath
        IDF_PYTHON_ENV_PATH = $pythonEnv
        PIP_NO_CACHE_DIR = '1'
    }
    $savedEnvironment = @{}
    Push-Location -LiteralPath $selection.IdfPath
    try {
        foreach ($name in $environment.Keys) {
            $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
            [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
        }
        $PSNativeCommandUseErrorActionPreference = $false
        # Register ESP32-S3 even if a different target already passes the SDK check.
        # Espressif's installer skips tool versions already present.
        Invoke-FirmwareSetupStep 'Install ESP-IDF v6.0.2 ESP32-S3 tools' $python @($idfTools, 'install', '--targets=esp32s3')
        $pythonDependenciesMissing = -not $pythonPresent
        if ($pythonPresent) {
            $dependencyCheck = @(& $python $idfTools check-python-dependencies 2>&1)
            $dependencyExitCode = $LASTEXITCODE
            if ($dependencyExitCode -ne 0) {
                $details = $dependencyCheck -join "`n"
                if ($details -cnotmatch "The following Python requirements are not satisfied:|packaging cannot be imported\.|espidf\.constraints\.v6\.0\.txt doesn't exist") {
                    throw "Inspect ESP-IDF Python packages failed (exit code $dependencyExitCode): $details"
                }
                $pythonDependenciesMissing = $true
                $dependencyCheck | Out-Host
            }
        }
        if ($pythonDependenciesMissing) {
            $null = [IO.Directory]::CreateDirectory($pythonEnv)
            Invoke-FirmwareSetupStep 'Install ESP-IDF Python 3.13 environment and packages' $python @($idfTools, 'install-python-env', '--features=core')
        }
    } finally {
        foreach ($name in $savedEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
        }
        Pop-Location
    }

    $toolchain = Resolve-FirmwareToolchainPaths -IdfPath $selection.IdfPath -ToolsPath $selection.ToolsPath -PythonExe $selection.PythonExe
    $accountGroups = (Get-FirmwareUbuntuStepOutput 'Check Ubuntu USB serial group membership' '/usr/bin/id' @('-nG', '--', $account)) -split '\s+'
    if ('dialout' -cnotin $accountGroups) {
        Invoke-FirmwareSetupStep "Grant Ubuntu USB serial access to '$account'" '/usr/bin/sudo' @('/usr/sbin/usermod', '-aG', 'dialout', '--', $account)
    }
    $currentGroups = (Get-FirmwareUbuntuStepOutput 'Check current Ubuntu session groups' '/usr/bin/id' @('-nG')) -split '\s+'
    if ('dialout' -cnotin $currentGroups) {
        Write-Host 'Sign out of Ubuntu and sign in again before flashing over USB to activate dialout membership.'
    }
    return $toolchain
}
