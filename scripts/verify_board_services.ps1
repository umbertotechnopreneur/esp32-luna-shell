<# Hardware-free regression tests for the actual shell and build clean helper. #>
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $PSScriptRoot 'tests/shell'
$outputRoot = Join-Path $repoRoot 'artifacts/tests/board-services'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'MSVC x64 tools not found' }
    $vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
    $environmentLines = & cmd.exe /d /c ('"' + $vcvars + '" >nul && set')
    if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed' }
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
}
foreach ($pmu in @(0, 1)) {
    $executable = Join-Path $outputRoot "shell-pmu-$pmu.exe"
    & cl.exe /nologo /std:c++latest /EHsc /W4 "/DCONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS=$pmu" `
        "/I$testRoot" "/I$repoRoot/components/LUNASHELL_app/include" `
        "/Fo$outputRoot/shell-pmu-$pmu.obj" "/Fe$executable" "$testRoot/shell_power_test.cpp"
    if ($LASTEXITCODE -ne 0) { throw "Shell test compile failed (PMU=$pmu)" }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "Shell test failed (PMU=$pmu)" }
}

# Load only the clean helper's AST. The mocked executable never invokes IDF.
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'device.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
$clean = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Invoke-BuildDirectoryClean'
}, $true)
if (-not $clean) { throw 'Clean helper not found' }
. ([scriptblock]::Create($clean.Extent.Text))
$ArtifactsRoot = Join-Path $repoRoot 'artifacts'
function Get-BuildDirectoryPath([string]$Path) { return $Path }
$PythonExe = Join-Path $PSHOME 'pwsh.exe'
$IdfPy = Join-Path $outputRoot 'mock-idf.ps1'
@'
if ($env:IDF_COMPONENT_MANAGER -ne '0') { exit 41 }
exit ([int]$env:LUNASHELL_TEST_CLEAN_EXIT)
'@ | Set-Content -LiteralPath $IdfPy
$originalManager = [Environment]::GetEnvironmentVariable('IDF_COMPONENT_MANAGER', 'Process')
$originalTestExit = [Environment]::GetEnvironmentVariable('LUNASHELL_TEST_CLEAN_EXIT', 'Process')
try {
    foreach ($setting in @($null, '1')) {
        $env:IDF_COMPONENT_MANAGER = $setting
        foreach ($exitCode in @(0, 7)) {
            $env:LUNASHELL_TEST_CLEAN_EXIT = "$exitCode"
            $failed = $false
            try { Invoke-BuildDirectoryClean $outputRoot } catch { $failed = $true }
            if ($failed -ne ($exitCode -ne 0)) { throw 'Clean error propagation failed' }
            if ([Environment]::GetEnvironmentVariable('IDF_COMPONENT_MANAGER', 'Process') -cne $setting) {
                throw 'Clean helper did not restore component manager setting'
            }
        }
    }
    $rejected = $false
    try { Invoke-BuildDirectoryClean $repoRoot } catch { $rejected = $true }
    if (-not $rejected) { throw 'Clean helper accepted a path outside artifacts' }
} finally {
    $env:IDF_COMPONENT_MANAGER = $originalManager
    $env:LUNASHELL_TEST_CLEAN_EXIT = $originalTestExit
}
Write-Host 'Build clean helper: success, failure, environment restoration and path boundary passed'
