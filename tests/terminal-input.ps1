param([Parameter(Mandatory)][string]$ModulePath)
$ErrorActionPreference = 'Stop'
Import-Module $ModulePath -Force

function Assert-Equal($Actual, $Expected, [string]$Reason) {
    if ($Actual -cne $Expected) { throw "$Reason. Expected [$Expected], got [$Actual]." }
}
function Key($State, [ConsoleKey]$Code, [char]$Character = [char]0, [bool]$Shift = $false, [bool]$Control = $false) {
    Edit-TerminalInput $State ([ConsoleKeyInfo]::new($Character, $Code, $Shift, $false, $Control))
}

$state = New-TerminalInput
Assert-Equal (Key $state UpArrow) 'Edit' 'Empty history navigation'
Assert-Equal $state.Buffer '' 'Empty history preserves input'
Add-TerminalHistory $state 'first'
Add-TerminalHistory $state "If True`n    second`nEndIf`n"
Add-TerminalHistory $state "If True`n    second`nEndIf`n"
Assert-Equal $state.History.Count 2 'Consecutive duplicate commands are skipped'
$state.Buffer = 'draft'; $state.Cursor = 2
[void](Key $state UpArrow)
Assert-Equal $state.Buffer "If True`n    second`nEndIf" 'Multiline history is recalled as one command'
[void](Key $state UpArrow)
[void](Key $state UpArrow)
Assert-Equal $state.Buffer 'first' 'Oldest history boundary'
[void](Key $state End)
[void](Key $state X 'x')
Assert-Equal $state.Buffer 'firstx' 'Recalled commands are editable'
Assert-Equal $state.History[0] 'first' 'Editing leaves stored history intact'
[void](Key $state DownArrow)
[void](Key $state DownArrow)
Assert-Equal $state.Buffer 'draft' 'Down past newest restores draft'
Assert-Equal $state.Cursor 2 'Draft cursor is restored'
[void](Key $state DownArrow)
Assert-Equal $state.Buffer 'draft' 'Further down leaves draft intact'
$state.Continuation = $true
[void](Key $state UpArrow)
Assert-Equal $state.Buffer 'draft' 'History cannot replace a continuation line'
$state.Continuation = $false

[void](Key $state Home)
[void](Key $state Delete)
Assert-Equal $state.Buffer 'raft' 'Forward deletion'
[void](Key $state End)
[void](Key $state Backspace)
Assert-Equal $state.Buffer 'raf' 'Backspace'
[void](Key $state Enter ([char]0) $true)
Assert-Equal $state.Buffer "raf`n" 'Shift Enter inserts a newline without submission'
Assert-Equal (Key $state Enter) 'Submit' 'Enter submits'
Assert-Equal (Key $state C ([char]3) $false $true) 'Cancel' 'Control C cancels'
Assert-Equal (Key $state L ([char]12) $false $true) 'ClearScreen' 'Control L clears the display locally'
Assert-Equal $state.Buffer "raf`n" 'Screen clearing preserves unfinished input'
[void](Key $state Escape)
Assert-Equal (Key $state D ([char]4) $false $true) 'Exit' 'Control D exits an empty prompt'
[void](Key $state Tab)
Assert-Equal $state.Buffer '    ' 'Tab indentation'

$accent = 'e' + [char]0x0301
$emoji = [char]::ConvertFromUtf32(0x1F600)
$state.Buffer = $accent + $emoji; $state.Cursor = $state.Buffer.Length
[void](Key $state LeftArrow)
Assert-Equal $state.Cursor 2 'Cursor skips a surrogate pair'
[void](Key $state Backspace)
Assert-Equal $state.Buffer $emoji 'Backspace removes a combining sequence'
Assert-Equal $state.Cursor 0 'Unicode backspace cursor'
[void](Key $state Delete)
Assert-Equal $state.Buffer '' 'Delete removes a complete surrogate pair'

$state.Buffer = "If True`n    Game.GetPlayer().GetLevel()`nEndIf"; $state.Cursor = $state.Buffer.Length
$view = Get-TerminalInputView $state 120
if (!($view.Before + $view.After).Contains([char]0x21B5)) { throw 'View lost multiline markers.' }
if (($view.Before + $view.After).Contains("`n")) { throw 'Editor view must stay on its input row.' }
Assert-Equal $state.Buffer "If True`n    Game.GetPlayer().GetLevel()`nEndIf" 'Display leaves source unchanged'
$state.Buffer = 'a' * 500; $state.Cursor = 250
$view = Get-TerminalInputView $state 80
if (!$view.Before.StartsWith('<') -or !$view.After.EndsWith('>')) { throw 'Long input overflow indicators missing.' }
if (($view.Before + $view.After).Length -gt 74) { throw 'View exceeds its row budget.' }
$state.Buffer = $emoji; $state.Cursor = $emoji.Length
[void](Get-TerminalInputView $state 8)
Add-TerminalHistory $state ' '
Assert-Equal $state.History.Count 2 'Blank entries are skipped'
for ($i = 0; $i -lt 300; $i++) { Add-TerminalHistory $state "command $i" }
Assert-Equal $state.History.Count 256 'History has a bounded session capacity'
Assert-Equal $state.History[0] 'command 44' 'Oldest excess history is retired'
$state = New-TerminalInput
$state.Buffer = 'Game.GetP'; $state.Cursor = $state.Buffer.Length
Assert-Equal (Key $state Tab) 'Complete' 'Tab requests metadata'
Assert-Equal (Key $state Tab ([char]0) $true) 'CompletePrevious' 'Shift Tab requests reverse completion'
Assert-Equal (Key $state Spacebar ([char]0) $false $true) 'Complete' 'Control Space explicitly requests completion'
$result = [pscustomobject]@{ begin = 5; end = 9; items = @(
    [pscustomobject]@{ text = 'GetPlayer()'; cursor = 11 },
    [pscustomobject]@{ text = 'GetForm()'; cursor = 8 }
) }
Assert-Equal (Set-TerminalCompletions $state $result) $true 'Completion applied'
Assert-Equal $state.Buffer 'Game.GetPlayer()' 'Function insertion'
Assert-Equal $state.Cursor 16 'No argument cursor follows parentheses'
[void](Key $state Tab)
Assert-Equal $state.Buffer 'Game.GetForm()' 'Cycling replaces the original token'
Assert-Equal $state.Cursor 13 'Argument cursor inside parentheses'
[void](Key $state Tab ([char]0) $true)
Assert-Equal $state.Buffer 'Game.GetPlayer()' 'Reverse cycling'
[void](Key $state Escape)
Assert-Equal $state.Buffer 'Game.GetPlayer()' 'Escape dismisses without erasing input'
Assert-Equal $state.Completion $null 'Escape ends cycling'
$state.Buffer = 'Game.GetP()'; $state.Cursor = 9
$result.end = 9
Assert-Equal (Set-TerminalCompletions $state ([pscustomobject]@{ begin = 5; end = 9; items = @(
    [pscustomobject]@{ text = 'GetPlayer'; cursor = 11 }
) })) $true 'Existing parentheses completion'
Assert-Equal $state.Buffer 'Game.GetPlayer()' 'Existing parentheses retained'
Assert-Equal $state.Cursor 16 'Cursor can cross existing parentheses'
[void](Key $state X 'x')
Assert-Equal $state.Completion $null 'Typing ends completion cycling'

$prefix = 'var label = "' + $emoji + '"' + "`n"
$state.Buffer = 'Game.GetP'; $state.Cursor = 9
$prefixBytes = [Text.Encoding]::UTF8.GetByteCount($prefix)
$result.begin = $prefixBytes + 5; $result.end = $prefixBytes + 9
Assert-Equal (Set-TerminalCompletions $state $result $prefix $true) $true 'Multiline Unicode completion'
Assert-Equal $state.Buffer 'Game.GetForm()' 'Previous physical lines stay outside editing buffer'
Assert-Equal $state.Cursor 13 'UTF8 offsets become local UTF16 cursor'
Assert-Equal (Set-TerminalCompletions $state ([pscustomobject]@{ items = @() })) $false 'Empty results do not activate cycling'
Write-Output 'Terminal history, editor and completion tests passed.'
