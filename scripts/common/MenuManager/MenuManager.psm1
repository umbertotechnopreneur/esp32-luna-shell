function Use-WriteColor {
    return (Get-Command Write-Color -ErrorAction SilentlyContinue) -ne $null
}

function Read-MenuChoice {
    param(
        [string]$Prompt = "Enter choice",
        [int]$MaxIndex = 9,
        [string[]]$AllowedChoices,
        [switch]$IgnoreEmptyInput
    )

    $useSingleKey = $false
    if ($AllowedChoices -and $AllowedChoices.Count -gt 0) {
        $useSingleKey = ($AllowedChoices | Where-Object { $_.Length -ne 1 }).Count -eq 0
    }
    elseif ($MaxIndex -lt 10) { $useSingleKey = $true }

    if (Use-WriteColor) {
        # Avoid passing an empty string to Write-Color (some environments may
        # provide a Write-Color implementation that rejects empty strings).
        Write-Color ' ' -NoNewLine
        Write-Color ("  $Prompt") -Color DarkGray -NoNewLine
        Write-Color ": " -Color DarkGray -NoNewLine
    }
    else {
        Write-Host ""
        Write-Host ("  {0}: " -f $Prompt) -ForegroundColor DarkGray -NoNewline
    }

    if ($useSingleKey) {
        try {
            while ($true) {
                $key = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
                # ESC -> explicit cancel/back
                if ($key.VirtualKeyCode -eq 27) {
                    $global:ToolsMenuSkipPause = $true
                    Write-Host ''
                    return $null
                }
                # Enter -> empty input (default)
                if ([int][char]$key.Character -eq 13) {
                    if ($IgnoreEmptyInput.IsPresent) {
                        continue
                    }

                    Write-Host ''
                    return ''
                }

                # Prefer the printable character, but fall back to numpad virtual-key codes
                $ch = [string]$key.Character
                if ([string]::IsNullOrEmpty($ch) -and $key.VirtualKeyCode -ge 96 -and $key.VirtualKeyCode -le 105) {
                    $ch = (($key.VirtualKeyCode - 96).ToString())
                }

                # Ignore non-printable/control keys (Ctrl+*, arrows, function keys, etc.).
                if ([string]::IsNullOrEmpty($ch)) { continue }
                $codePoint = [int][char]$ch
                if ($codePoint -lt 32 -or $codePoint -eq 127) { continue }

                if ($AllowedChoices -and $AllowedChoices.Count -gt 0) {
                    if ($AllowedChoices -contains $ch) {
                        $global:ToolsMenuSkipPause = [bool]($ch -match '^[0qQxX]$')
                        Write-Host $ch
                        return $ch
                    }
                    else { continue }
                }

                # For implicit single-key numeric menus, accept digits only.
                if ($useSingleKey -and -not ($ch -match '^[0-9]$')) { continue }

                $global:ToolsMenuSkipPause = [bool]($ch -match '^[0qQxX]$')
                Write-Host $ch
                return $ch
            }
        }
        catch {
            # Fall through to Read-Host fallback
        }
    }

    while ($true) {
        try {
            $raw = Read-Host
        }
        catch {
            return ''
        }

        if ([string]::IsNullOrWhiteSpace($raw)) {
            if ($IgnoreEmptyInput.IsPresent) {
                continue
            }

            return ''
        }

        $t = $raw.Trim()
        if ($t.ToLowerInvariant() -in @('esc','exit','quit','back')) {
            $global:ToolsMenuSkipPause = $true
            return $null
        }
        $global:ToolsMenuSkipPause = [bool]($t -match '^[0qQxX]$')
        return $t
    }
}

function Read-IndexChoice {
    param(
        [Parameter(Position=0)]
        [AllowEmptyString()]
        [string]$Prompt = '',

        [Parameter(Mandatory)][int]$Min,
        [Parameter(Mandatory)][int]$Max
    )

    while ($true) {
        $promptText = if ([string]::IsNullOrWhiteSpace($Prompt)) {
            "  Enter number [{0}-{1}], or 0 / Esc to go back: " -f $Min, $Max
        }
        else {
            "  {0}: " -f $Prompt
        }

        if (Use-WriteColor) {
            Write-Color $promptText -Color Yellow -NoNewLine
        }
        else {
            Write-Host $promptText -NoNewline
        }

        if ($Host.UI -and $Host.UI.RawUI -and [Environment]::UserInteractive) {
            try {
                # Buffered raw-key input keeps Esc/0 cancellation responsive for multi-digit ranges.
                $buffer = ''

                while ($true) {
                    $key = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')

                    # Esc -> explicit cancel/back
                    if ($key.VirtualKeyCode -eq 27) { Write-Host ''; return $null }

                    # Enter -> submit the current buffer
                    if ($key.VirtualKeyCode -eq 13) {
                        Write-Host ''
                        if ([string]::IsNullOrWhiteSpace($buffer)) { return '' }
                        if ($buffer -eq '0') {
                            $global:ToolsMenuSkipPause = $true
                            return $null
                        }

                        $n = 0
                        if ([int]::TryParse($buffer, [ref]$n) -and $n -ge $Min -and $n -le $Max) { return $n }
                        break
                    }

                    # Backspace removes the last typed digit.
                    if ($key.VirtualKeyCode -eq 8) {
                        if ($buffer.Length -gt 0) {
                            $buffer = $buffer.Substring(0, $buffer.Length - 1)
                            Write-Host -NoNewline "`b `b"
                        }
                        continue
                    }

                    $ch = ([string]$key.Character).Trim()
                    if ([string]::IsNullOrEmpty($ch) -and $key.VirtualKeyCode -ge 96 -and $key.VirtualKeyCode -le 105) {
                        $ch = (($key.VirtualKeyCode - 96).ToString())
                    }

                    if ($ch -match '^[0-9]$') {
                        if ($ch -eq '0' -and [string]::IsNullOrEmpty($buffer)) {
                            $global:ToolsMenuSkipPause = $true
                            Write-Host ''
                            return $null
                        }
                        $buffer += $ch
                        Write-Host -NoNewline $ch
                    }
                }

                if (Use-WriteColor) { Write-Color (("  ✗ Please enter a number between {0} and {1}." -f $Min, $Max)) -Color Red }
                else { Write-Host (("  ✗ Please enter a number between {0} and {1}." -f $Min, $Max)) -ForegroundColor Red }
                continue
            }
            catch {
                # fallback to Read-Host
            }
        }

        $raw = Read-Host
        if ([string]::IsNullOrWhiteSpace($raw)) { return '' }
        $t = $raw.Trim()
        if ($t.ToLowerInvariant() -in @('esc','exit','quit','back','0')) {
            $global:ToolsMenuSkipPause = $true
            return $null
        }
        $n = 0
        if ([int]::TryParse($t, [ref]$n) -and $n -ge $Min -and $n -le $Max) { return $n }
        if (Use-WriteColor) { Write-Color ("  ✗ Please enter a number between {0} and {1}." -f $Min, $Max) -Color Red }
        else { Write-Host ("  ✗ Please enter a number between {0} and {1}." -f $Min, $Max) -ForegroundColor Red }
    }
}

function Write-MenuSectionHeader {
    param([string]$Title)
    $w = [Math]::Min([Math]::Max([Console]::WindowWidth - 4, 66), 106)
    Write-Host ""
    Write-Host ("  " + ("-" * [Math]::Min($w, 58))) -ForegroundColor DarkCyan
    Write-Host "  $Title" -ForegroundColor Cyan
    Write-Host ("  " + ("-" * [Math]::Min($w, 58))) -ForegroundColor DarkCyan
    Write-Host ""
}

function Write-MenuNumber {
    param(
        [Parameter(Mandatory)][string]$Number,
        [string]$Prefix = '  ',
        [string]$Suffix = '  '
    )

    $esc = [char]27
    $ansiNumber = '{0}[1;97m[{1}]{0}[0m' -f $esc, $Number
    try {
        Write-Host ($Prefix + $ansiNumber + $Suffix) -NoNewline
    }
    catch {
        Write-Host ($Prefix + "[$Number]" + $Suffix) -ForegroundColor White -NoNewline
    }
}

function Write-MenuItem {
    param(
        [int]   $Number,
        [string]$Emoji,
        [string]$Label,
        [string]$Desc,
        [ConsoleColor]$LabelColor = 'Yellow'
    )
    $num = ("{0,2}" -f $Number)
    Write-MenuNumber -Number $num
    Write-Host "$Emoji  " -NoNewline
    Write-Host ("{0,-28}" -f $Label) -ForegroundColor $LabelColor -NoNewline
    Write-Host "  $Desc" -ForegroundColor DarkGray
}

function Resolve-MenuConsoleWidth {
    param(
        [int]$Minimum = 80,
        [int]$Maximum = 180,
        [int]$Fallback = 110
    )

    $resolvedWidth = $Fallback
    try {
        $consoleWidth = [Console]::WindowWidth
        if ($consoleWidth -gt 0) {
            $resolvedWidth = $consoleWidth
        }
    }
    catch {
        try {
            $rawWidth = $Host.UI.RawUI.WindowSize.Width
            if ($rawWidth -gt 0) {
                $resolvedWidth = $rawWidth
            }
        }
        catch {
            $resolvedWidth = $Fallback
        }
    }

    return [Math]::Max($Minimum, [Math]::Min($resolvedWidth, $Maximum))
}

function Get-MenuTruncatedText {
    param(
        [AllowNull()][string]$Text,
        [int]$MaxLength
    )

    if ($null -eq $Text) { return '' }
    if ($MaxLength -le 0) { return '' }
    if ($Text.Length -le $MaxLength) { return $Text }
    if ($MaxLength -le 3) { return $Text.Substring(0, $MaxLength) }
    return $Text.Substring(0, $MaxLength - 3) + '...'
}

function Write-MenuCompactSectionHeader {
    param(
        [Parameter(Mandatory)][string]$Title,
        [ConsoleColor]$AccentColor = 'Cyan',
        [int]$ConsoleWidth = 110
    )

    $ruleLength = [Math]::Max(26, [Math]::Min(64, $ConsoleWidth - 8))
    $titleText = "  -- $Title "
    $ruleRemainder = [Math]::Max(1, $ruleLength - $titleText.Length)

    Write-Host ""
    Write-Host $titleText -ForegroundColor $AccentColor -NoNewline
    Write-Host ("-" * $ruleRemainder) -ForegroundColor DarkGray
}

# Number identifies the menu selection key displayed beside the item.
# Emoji contains the icon; an empty string is valid for text-only menu items.
# Label names the action shown in the compact menu.
# Desc carries details for help pages and is not displayed in compact rows.
# LabelColor selects the console color used for the action label.
# ConsoleWidth determines the available width before the label is truncated.
function Write-MenuItemCompact {
    param(
        [Parameter(Mandatory)][object]$Number,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Emoji,
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Desc,
        [ConsoleColor]$LabelColor = 'Yellow',
        [int]$ConsoleWidth = 110
    )

    # The main menu is an index, not a log. Keep the action name readable and
    # avoid putting a low-contrast paragraph beside every item.
    $labelWidth = [Math]::Min(42, [Math]::Max(24, $ConsoleWidth - 20))
    $displayLabel = Get-MenuTruncatedText -Text $Label -MaxLength $labelWidth
    $num = ("{0,2}" -f $Number)

    Write-MenuNumber -Number $num
    Write-Host "$Emoji  " -NoNewline
    Write-Host $displayLabel -ForegroundColor $LabelColor
}

function Write-MenuItemCompactPair {
    param(
        [Parameter(Mandatory)]$LeftItem,
        $RightItem,
        [ConsoleColor]$LabelColor = 'Yellow',
        [int]$ConsoleWidth = 120
    )

    if ($null -eq $RightItem) {
        Write-MenuItemCompact -Number $LeftItem.Number -Emoji $LeftItem.Emoji -Label $LeftItem.Label -Desc $LeftItem.Desc -LabelColor $LabelColor -ConsoleWidth $ConsoleWidth
        return
    }

    $colGap = 4
    $usableWidth = [Math]::Max(80, $ConsoleWidth - 4)
    $colWidth = [Math]::Max(36, [int](($usableWidth - $colGap) / 2))

    # Give each action a generous visual block. Descriptions remain in the
    # manifest for help/details, but do not compete with the action names here.
    $labelWidth = [Math]::Max(24, $colWidth - 14)

    $leftNum = ("{0,2}" -f [int]$LeftItem.Number)
    $leftLabel = Get-MenuTruncatedText -Text ([string]$LeftItem.Label) -MaxLength $labelWidth

    $rightNum = ("{0,2}" -f [int]$RightItem.Number)
    $rightLabel = Get-MenuTruncatedText -Text ([string]$RightItem.Label) -MaxLength $labelWidth

    $leftPrefix = "  [$leftNum]  $($LeftItem.Emoji)  "
    $leftPadLength = [Math]::Max(0, $colWidth - ($leftPrefix.Length + $labelWidth))

    Write-Host '  ' -NoNewline
    Write-MenuNumber -Number $leftNum -Prefix '' -Suffix '  '
    Write-Host "$($LeftItem.Emoji)  " -NoNewline
    Write-Host ("{0,-$labelWidth}" -f $leftLabel) -ForegroundColor $LabelColor -NoNewline
    if ($leftPadLength -gt 0) {
        Write-Host (" " * $leftPadLength) -NoNewline
    }

    Write-Host (" " * $colGap) -NoNewline

    Write-MenuNumber -Number $rightNum -Prefix '' -Suffix '  '
    Write-Host "$($RightItem.Emoji)  " -NoNewline
    Write-Host $rightLabel -ForegroundColor $LabelColor
}

function Write-MenuPage {
    param(
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)][object[]]$Items,
        [string]$Hint,
        [ConsoleColor]$AccentColor = 'Cyan',
        [switch]$ShowBack,
        [switch]$ShowExit,
        [string]$BackLabel = 'Back to Main Menu',
        [int]$ConsoleWidth = 0
    )

    if ($ConsoleWidth -le 0) {
        $ConsoleWidth = Resolve-MenuConsoleWidth -Minimum 80 -Maximum 180 -Fallback 110
    }

    Write-MenuCompactSectionHeader -Title $Title -AccentColor $AccentColor -ConsoleWidth $ConsoleWidth
    if (-not [string]::IsNullOrWhiteSpace($Hint)) {
        Write-Host ("  {0}" -f $Hint) -ForegroundColor DarkGray
    }

    foreach ($item in @($Items)) {
        $labelColor = [ConsoleColor]::Yellow
        if ($item.PSObject.Properties.Name -contains 'LabelColor' -and $item.LabelColor) {
            try { $labelColor = [ConsoleColor]$item.LabelColor } catch { $labelColor = [ConsoleColor]::Yellow }
        }

        $description = if ($item.PSObject.Properties.Name -contains 'Desc') { [string]$item.Desc } else { '' }
        Write-MenuItemCompact `
            -Number ([string]$item.Number) `
            -Emoji ([string]$item.Emoji) `
            -Label ([string]$item.Label) `
            -Desc $description `
            -LabelColor $labelColor `
            -ConsoleWidth $ConsoleWidth
    }

    if ($ShowBack) {
        Write-BackItem -Label $BackLabel
    }
    elseif ($ShowExit) {
        Write-Host ''
        Write-MenuExitItem
    }
}

function Write-PropertyPage {
    param(
        [Parameter(Mandatory)][string]$Title,
        [object[]]$Properties = @(),
        [object[]]$Actions = @(),
        [string]$Breadcrumb,
        [string]$Hint = 'Choose a property to edit it. Changes return to this page.',
        [string]$BackLabel = 'Back'
    )

    if ($Breadcrumb) { Write-Host ("  {0}" -f $Breadcrumb) -ForegroundColor DarkGray }
    Write-MenuCompactSectionHeader -Title $Title
    if ($Hint) { Write-Host ("  {0}" -f $Hint) -ForegroundColor Gray }
    Write-Host ''
    foreach ($property in $Properties) {
        Write-MenuNumber -Number ('{0,2}' -f $property.Number)
        Write-Host ("{0}  {1}" -f $property.Emoji,$property.Label) -ForegroundColor Yellow -NoNewline
        Write-Host (" : {0}" -f $property.Value) -ForegroundColor Cyan
        if ($property.PSObject.Properties.Name -contains 'Help' -and $property.Help) {
            Write-Host ("          {0}" -f $property.Help) -ForegroundColor Gray
        }
    }
    if ($Actions.Count) {
        Write-Host ''
        foreach ($action in $Actions) {
            $color = if ($action.PSObject.Properties.Name -contains 'LabelColor' -and $action.LabelColor) { $action.LabelColor } else { 'Yellow' }
            Write-MenuItemCompact -Number $action.Number -Emoji $action.Emoji -Label $action.Label -Desc '' -LabelColor $color
        }
    }
    Write-BackItem -Label $BackLabel
}

function Write-BackItem {
    param([string]$Label = 'Back to Main Menu')

    Write-Host ""
    Write-MenuNumber -Number ' 0'
    Write-Host ("←  {0}" -f $Label) -ForegroundColor DarkGray
}

function Write-MenuExitItem {
    $esc = [char]27
    try {
        # ANSI 1 = bold, 91 = bright red; reset afterward for the next prompt.
        Write-Host ("{0}[1;91m  [ 0]  🚪  Exit{0}[0m" -f $esc)
    }
    catch {
        Write-Host "  [ 0]  🚪  Exit" -ForegroundColor Red
    }
}

function Wait-ForEnter {
    param(
        [string]$Prompt = '  Press Enter to return to the menu...'
    )

    if (Use-WriteColor) { Write-Color $Prompt -Color DarkGray -NoNewLine }
    else { Write-Host $Prompt -ForegroundColor DarkGray -NoNewLine }
    $null = Read-Host
}

Export-ModuleMember -Function Read-MenuChoice, Read-IndexChoice, Write-MenuItem, Write-MenuNumber, Write-MenuSectionHeader, Write-BackItem, Write-MenuExitItem, Wait-ForEnter, Resolve-MenuConsoleWidth, Write-MenuCompactSectionHeader, Write-MenuItemCompact, Write-MenuItemCompactPair, Write-MenuPage, Write-PropertyPage
