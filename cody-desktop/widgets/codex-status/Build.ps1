[CmdletBinding(DefaultParameterSetName='Default')]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [psobject]$Imports,
    [Parameter(Mandatory, ParameterSetName='MSIX')][string]$MsixPath,
    [Parameter(Mandatory, ParameterSetName='MSIX')][string]$CertificateThumbprint
)
$ErrorActionPreference = 'Stop'
if ($MsixPath) {
    $MsixPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($MsixPath)
    if ([IO.Path]::GetExtension($MsixPath) -ne '.msix' -or
        $MsixPath.StartsWith($PSScriptRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Choose an .msix output path outside the widget directory.'
    }
    if (Test-Path -LiteralPath $MsixPath) { throw "MSIX output already exists: $MsixPath" }
}
$packageRoot = Join-Path $PSScriptRoot '.imports\packages'
if ($null -eq $Imports -or -not $Imports.Prepared -or $Imports.ProjectRoot -ne $PSScriptRoot -or $Imports.PackageRoot -ne $packageRoot) {
    throw 'Run $imports = & ./Prepare-Imports.ps1, then & ./Build.ps1 -Imports $imports in the same PowerShell session.'
}
# Integrity verification only, against the current preparation object in RAM.
$records = @($Imports.Packages)
if (-not $records.Count) { throw 'The preparation context has no package records.' }
foreach ($entry in $records) {
    $file = [IO.Path]::GetFullPath((Join-Path $packageRoot $entry.path))
    if (-not $file.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Package path escapes the import directory.' }
    if (-not (Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Import changed or missing: $($entry.path). Prepare the new import explicitly before building."
    }
}
if (@(Get-ChildItem -LiteralPath $packageRoot -Recurse -File).Count -ne $records.Count) { throw 'Unverified files exist in the import staging area.' }
$completed = $false
Push-Location $PSScriptRoot
try {
    if ($MsixPath) {
        $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64'
        $payload = Join-Path $PSScriptRoot '.imports\msix-payload'
        & dotnet publish 'Windows/OpenAIUsage.Windows.csproj' -c $Configuration -r win-x64 --self-contained true `
            -p:WindowsAppSDKSelfContained=true --no-restore -o $payload
        if ($LASTEXITCODE -ne 0) { throw 'Windows MSIX source publish failed.' }

        [xml]$manifest = Get-Content -LiteralPath 'Packaging/AppxManifest.xml' -Raw
        $manifest.Package.Identity.Version = [Reflection.AssemblyName]::GetAssemblyName(
            (Join-Path $payload 'OpenAIUsage.Windows.dll')).Version.ToString()
        $manifest.Save((Join-Path $payload 'AppxManifest.xml'))

        Add-Type -AssemblyName System.Drawing.Common
        $icon = [Drawing.Bitmap]::new((Join-Path $PSScriptRoot 'Branding\app-icon-512.png'))
        try {
            foreach ($size in @(44, 50, 150)) {
                $logo = [Drawing.Bitmap]::new($icon, $size, $size)
                try { $logo.Save((Join-Path $payload "Branding\msix-logo-$size.png"), [Drawing.Imaging.ImageFormat]::Png) }
                finally { $logo.Dispose() }
            }
        }
        finally { $icon.Dispose() }

        & (Join-Path $sdkBin 'makeappx.exe') pack /d $payload /p $MsixPath /h SHA256
        if ($LASTEXITCODE -ne 0) { throw 'MSIX packaging failed.' }
        & (Join-Path $sdkBin 'signtool.exe') sign /fd SHA256 /sha1 $CertificateThumbprint $MsixPath
        if ($LASTEXITCODE -ne 0) { throw 'MSIX signing failed.' }
        Write-Output "Source-built MSIX: $MsixPath"
        return
    }

    foreach ($project in @('Windows/OpenAIUsage.Windows.csproj','Backend/OpenAIUsage.Backend.csproj','Tests/OpenAIUsage.Tests.csproj')) {
        & dotnet build $project -c $Configuration --no-restore
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $project" }
    }
    & dotnet run --project 'Tests/OpenAIUsage.Tests.csproj' -c $Configuration --no-build --no-restore
    if ($LASTEXITCODE -ne 0) { throw 'Shared behavioral checks failed.' }
    & python -m py_compile 'Linux/openai_usage.py' 'Linux/watchdog.py' 'Linux/tray.py'
    if ($LASTEXITCODE -ne 0) { throw 'GTK Python syntax check failed.' }
    & python 'Tests/test_watchdog.py'
    if ($LASTEXITCODE -ne 0) { throw 'Linux watchdog ownership checks failed.' }
    & dotnet publish 'Windows/OpenAIUsage.Windows.csproj' -c $Configuration --no-restore -o 'artifacts/windows-x64'
    if ($LASTEXITCODE -ne 0) { throw 'Windows source publish failed.' }
    foreach ($resource in @('OpenAIUsage.Windows.pri','App.xbf')) {
        $compiled = Join-Path $PSScriptRoot "Windows\bin\$Configuration\net10.0-windows10.0.26100.0\win-x64\$resource"
        $published = Join-Path $PSScriptRoot "artifacts\windows-x64\$resource"
        if (-not (Test-Path -LiteralPath $published) -or (Get-FileHash -LiteralPath $compiled).Hash -ne (Get-FileHash -LiteralPath $published).Hash) {
            throw "Published XAML resource is missing or stale: $resource"
        }
    }
    & dotnet publish 'Backend/OpenAIUsage.Backend.csproj' -c $Configuration --no-restore --self-contained false -p:UseAppHost=false -o 'artifacts/linux/backend'
    if ($LASTEXITCODE -ne 0) { throw 'Portable backend source publish failed.' }
    $linuxDirectory = Join-Path $PSScriptRoot 'artifacts\linux\Linux'
    New-Item -ItemType Directory -Path $linuxDirectory -Force | Out-Null
    foreach ($name in @('openai_usage.py','watchdog.py','tray.py','run.sh','openai-usage.desktop')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Linux\$name") -Destination (Join-Path $linuxDirectory $name) -Force
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Branding') -Destination (Join-Path $PSScriptRoot 'artifacts\linux\Branding') -Recurse -Force
    $completed = $true
    Write-Output 'Source-built outputs: artifacts/windows-x64 and artifacts/linux. GTK execution requires a Linux desktop and installed runtime prerequisites.'
}
finally {
    Pop-Location
    try {
        & (Join-Path $PSScriptRoot 'Packaging\Clean-BuildOutputs.ps1') -KeepDebugDeliverables:$completed
    }
    finally {
        $Imports.Prepared = $false
        $Imports.Packages = @()
        $Imports.Tools = @()
    }
}
