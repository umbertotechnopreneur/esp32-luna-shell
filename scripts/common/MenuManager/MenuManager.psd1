@{
    RootModule = 'MenuManager.psm1'
    ModuleVersion = '0.1.0'
    GUID = '1e1b9f3e-6a4d-4b8c-9c7a-5f5b0b3d9ead'
    Author = 'Umberto Giacobbi'
    CompanyName = ''
    Copyright = ''
    Description = 'MenuManager: shared interactive menu helpers (single-key menu input, numeric index choice, unified pause helper).'
    PowerShellVersion = '5.1'
    FunctionsToExport = @(
        'Read-MenuChoice',
        'Read-IndexChoice',
        'Write-MenuItem',
        'Write-MenuNumber',
        'Write-MenuSectionHeader',
        'Write-BackItem',
        'Write-MenuExitItem',
        'Wait-ForEnter',
        'Resolve-MenuConsoleWidth',
        'Write-MenuCompactSectionHeader',
        'Write-MenuItemCompact',
        'Write-MenuItemCompactPair',
        'Write-MenuPage',
        'Write-PropertyPage'
    )
    FileList = @('MenuManager.psm1')
    PrivateData = @{ PSData = @{ Tags = @('menu','interactive','ui','helpers') } }
}
