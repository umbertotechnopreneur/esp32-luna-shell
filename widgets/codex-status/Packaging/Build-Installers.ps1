[CmdletBinding()]
param([psobject]$Imports)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$appVersion = ([xml](Get-Content -LiteralPath (Join-Path $projectRoot 'Directory.Build.props') -Raw)).Project.PropertyGroup.Version
function Assert-VerifiedImport([string]$Root,[object[]]$Entries) {
    if (-not $Entries.Count) { throw 'The current preparation context has no import records.' }
    foreach ($entry in $Entries) {
        $file = [IO.Path]::GetFullPath((Join-Path $Root $entry.path))
        if (-not $file.StartsWith($Root + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Import path escapes its staging directory.' }
        if (-not (Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -ne $entry.sha256) { throw "Unverified import: $file" }
    }
    if (@(Get-ChildItem -LiteralPath $Root -Recurse -File).Count -ne $Entries.Count) { throw "New unverified files in $Root" }
}
$packageRoot = Join-Path $projectRoot '.imports\packages'
$toolRoot = Join-Path $projectRoot '.imports\wix-5.0.2'
if ($null -eq $Imports -or -not $Imports.Prepared -or $Imports.ProjectRoot -ne $projectRoot -or $Imports.PackageRoot -ne $packageRoot -or $Imports.ToolRoot -ne $toolRoot) {
    throw 'Run $imports = & ./Packaging/Prepare-Packaging.ps1, then & ./Packaging/Build-Installers.ps1 -Imports $imports in the same PowerShell session.'
}
Assert-VerifiedImport $packageRoot @($Imports.Packages)
Assert-VerifiedImport $toolRoot @($Imports.Tools)
$installerRoot = Join-Path $projectRoot 'artifacts\installers'
$payloadRoot = Join-Path $projectRoot ('artifacts\packaging\' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
$windowsPayload = Join-Path $payloadRoot 'windows-x64'
$linuxPayload = Join-Path $payloadRoot 'linux-amd64'
New-Item -ItemType Directory -Path $installerRoot,$windowsPayload,(Join-Path $linuxPayload 'Linux') -Force | Out-Null
$completed = $false
Push-Location $projectRoot
try {
    # Recreate assets offline from the verified import cache, including after a cleanup.
    $packageRoot = Join-Path $projectRoot '.imports\packages'
    foreach ($restore in @(
        @{project='Windows/OpenAIUsage.Windows.csproj'; rid='win-x64'},
        @{project='Backend/OpenAIUsage.Backend.csproj'; rid='linux-x64'},
        @{project='Tests/OpenAIUsage.Tests.csproj'; rid=$null},
        @{project='Shared/OpenAIUsage.Core.csproj'; rid=$null}
    )) {
        $restoreArguments = @('restore',$restore.project,'--packages',$packageRoot,'--source',$packageRoot,'-p:NuGetAudit=false','-p:SelfContained=true')
        if ($restore.rid) { $restoreArguments += @('-r',$restore.rid) }
        & dotnet @restoreArguments
        if ($LASTEXITCODE -ne 0) { throw "Verified offline restore failed: $($restore.project)" }
    }
    Assert-VerifiedImport $packageRoot @($Imports.Packages)
    # Rebuild forces compilation even when old outputs carry future timestamps.
    & dotnet build 'Windows/OpenAIUsage.Windows.csproj' -t:Rebuild -c Release -r win-x64 --self-contained true --no-restore
    if ($LASTEXITCODE -ne 0) { throw 'Clean Windows source rebuild failed.' }

    & dotnet publish 'Windows/OpenAIUsage.Windows.csproj' -c Release -r win-x64 --self-contained true --no-build --no-restore -o $windowsPayload
    if ($LASTEXITCODE -ne 0) { throw 'Current Windows source publish failed.' }
    & dotnet build 'Backend/OpenAIUsage.Backend.csproj' -t:Rebuild -c Release -r linux-x64 --self-contained true --no-restore
    if ($LASTEXITCODE -ne 0) { throw 'Clean Linux backend source rebuild failed.' }
    & dotnet publish 'Backend/OpenAIUsage.Backend.csproj' -c Release -r linux-x64 --self-contained true --no-build --no-restore -o (Join-Path $linuxPayload 'backend')
    if ($LASTEXITCODE -ne 0) { throw 'Current Linux backend source publish failed.' }
    & dotnet build 'Tests/OpenAIUsage.Tests.csproj' -t:Rebuild -c Release --no-restore
    if ($LASTEXITCODE -ne 0) { throw 'Clean test source rebuild failed.' }
    & dotnet run --project 'Tests/OpenAIUsage.Tests.csproj' -c Release --no-build --no-restore
    if ($LASTEXITCODE -ne 0) { throw 'Behavioral checks failed.' }
    & python -m py_compile 'Linux/openai_usage.py' 'Linux/watchdog.py' 'Linux/tray.py' 'Packaging/package.py'
    if ($LASTEXITCODE -ne 0) { throw 'Python syntax checks failed.' }
    & python 'Tests/test_watchdog.py'
    if ($LASTEXITCODE -ne 0) { throw 'Linux watchdog ownership checks failed.' }
    foreach ($file in @('openai_usage.py','watchdog.py','tray.py','run.sh','openai-usage.desktop')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot "Linux\$file") -Destination (Join-Path $linuxPayload "Linux\$file")
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Branding') -Destination (Join-Path $linuxPayload 'Branding') -Recurse
    # Preserve notices from the already-verified dependency imports in both packages.
    $notices = Get-ChildItem -LiteralPath (Join-Path $projectRoot '.imports\packages') -Directory | ForEach-Object {
        $id = $_.Name
        Get-ChildItem -LiteralPath $_.FullName -Directory | ForEach-Object {
            $dependencyVersion = $_.Name
            Get-ChildItem -LiteralPath $_.FullName -File | Where-Object { $_.Name -match '(?i)^(license|third[-_]?party|notice)' } | ForEach-Object {
                [pscustomobject]@{Source=$_.FullName; Name="$id-$dependencyVersion-$($_.Name)"}
            }
        }
    }
    foreach ($payload in @($windowsPayload,$linuxPayload)) {
        $noticeDir = Join-Path $payload 'licenses'
        New-Item -ItemType Directory -Path $noticeDir -Force | Out-Null
        foreach ($notice in $notices) { Copy-Item -LiteralPath $notice.Source -Destination (Join-Path $noticeDir $notice.Name) }
    }
    foreach ($resource in @('OpenAIUsage.Windows.pri','App.xbf','hostfxr.dll','coreclr.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $windowsPayload $resource))) { throw "Windows payload missing: $resource" }
    }
    foreach ($resource in @('OpenAIUsage.Backend','libhostfxr.so','libcoreclr.so')) {
        if (-not (Test-Path -LiteralPath (Join-Path $linuxPayload "backend\$resource"))) { throw "Linux payload missing: $resource" }
    }
    $compiledVersion = (Get-Item -LiteralPath (Join-Path $windowsPayload 'OpenAIUsage.Windows.dll')).VersionInfo.FileVersion
    if ($compiledVersion -ne "$appVersion.0") { throw "Stale Windows binary: $compiledVersion, expected $appVersion.0" }
    $wxs = Join-Path $payloadRoot 'OpenAIUsage.wxs'
    & python 'Packaging/package.py' wix $windowsPayload $wxs
    if ($LASTEXITCODE -ne 0) { throw 'WiX source generation failed.' }
    $msi = Join-Path $installerRoot "OpenAIUsage-$appVersion-windows-x64.msi"
    & dotnet '.imports/wix-5.0.2/wix/tools/net6.0/any/wix.dll' build $wxs -arch x64 -o $msi -pdbtype none -intermediatefolder (Join-Path $payloadRoot 'wix-intermediate')
    if ($LASTEXITCODE -ne 0) { throw 'Windows installer compilation or validation failed.' }
    & (Join-Path $PSScriptRoot 'Verify-WindowsRequirement.ps1') -PackagePath $msi
    $deb = Join-Path $installerRoot "openai-usage_$appVersion-1_amd64.deb"
    & python 'Packaging/package.py' deb $linuxPayload $deb
    if ($LASTEXITCODE -ne 0) { throw 'Ubuntu package construction or verification failed.' }
    $manifest = @($msi,$deb) | ForEach-Object { [pscustomobject]@{file=[IO.Path]::GetFileName($_); bytes=(Get-Item -LiteralPath $_).Length; sha256=(Get-FileHash -LiteralPath $_).Hash} }
    $completed = $true
    Write-Output ($manifest | ConvertTo-Json -Compress)
}
finally {
    Pop-Location
    try {
        & (Join-Path $PSScriptRoot 'Clean-BuildOutputs.ps1') -KeepDeliverables:$completed
    }
    finally {
        $Imports.Prepared = $false
        $Imports.Packages = @()
        $Imports.Tools = @()
    }
}
