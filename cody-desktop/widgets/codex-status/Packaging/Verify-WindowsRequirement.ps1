# Evaluate only the OS prerequisite in a safe MSI session. No installation is performed.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackagePath)
$ErrorActionPreference = 'Stop'
$installer = New-Object -ComObject WindowsInstaller.Installer
$installer.UILevel = 2
$database = $installer.OpenDatabase([IO.Path]::GetFullPath($PackagePath),0)
$view = $database.OpenView('SELECT `Condition` FROM `LaunchCondition`')
$view.Execute()
$condition = $null
while ($row = $view.Fetch()) {
    $candidate = $row.StringData(1)
    if ($candidate -match '\bWindowsBuild\b') { throw 'Installer uses an obsolete Windows build check.' }
    if ($candidate -match '\bOPENAI_OS_BUILD\b') { $condition = $candidate }
}
$view.Close()
if ($null -eq $condition) { throw 'The installer has no native Windows build prerequisite.' }
# Option 1 prevents changes to the current computer state (MsiOpenPackageEx safe session).
$session = $installer.OpenPackage([IO.Path]::GetFullPath($PackagePath),1)
$search = $session.DoAction('AppSearch')
if ($search -ne 1) { throw "Registry search failed: $search" }
$actual = $session.Property('OPENAI_OS_BUILD')
$expected = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').CurrentBuildNumber
if ($actual -ne $expected) { throw "Installer detected build '$actual', expected '$expected'." }
if ($session.EvaluateCondition($condition) -ne 1) { throw "This compatible Windows system was incorrectly rejected (build $actual)." }
function Set-SessionProperty([string]$Name,[string]$Value) {
    [void]$session.GetType().InvokeMember('Property',[Reflection.BindingFlags]::SetProperty,$null,$session,@($Name,$Value))
}
Set-SessionProperty 'Installed' ''
Set-SessionProperty 'VersionNT64' '603'
foreach ($case in @(
    @{Build='9600';Allowed=$false}, @{Build='17763';Allowed=$false},
    @{Build='19040';Allowed=$false}, @{Build='19041';Allowed=$true},
    @{Build='22000';Allowed=$true}, @{Build='26300';Allowed=$true}, @{Build='';Allowed=$false}
)) {
    Set-SessionProperty 'OPENAI_OS_BUILD' $case.Build
    $allowed = $session.EvaluateCondition($condition) -eq 1
    if ($allowed -ne $case.Allowed) { throw "Wrong prerequisite decision for build '$($case.Build)'." }
}
Set-SessionProperty 'OPENAI_OS_BUILD' $actual
Set-SessionProperty 'VersionNT64' ''
if ($session.EvaluateCondition($condition) -ne 0) { throw '32-bit Windows was incorrectly accepted.' }
Set-SessionProperty 'Installed' '1'
if ($session.EvaluateCondition($condition) -ne 1) { throw 'Maintenance/removal should remain possible.' }
Write-Output "Windows prerequisite passed: actual build $actual, seven build cases, architecture and maintenance checks. Safe MSI session only; no installation."
