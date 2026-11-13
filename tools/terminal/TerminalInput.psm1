function New-TerminalInput {
    [pscustomobject]@{
        History = [Collections.Generic.List[string]]::new()
        Buffer = ''; Cursor = 0; Index = 0
        SavedDraft = ''; SavedCursor = 0
        Browsing = $false; Continuation = $false
        Completion = $null
    }
}

function Add-TerminalHistory($State, [string]$Source) {
    $source = $Source.TrimEnd([char[]]"`r`n")
    if (!$source.Trim()) { return }
    if ($State.History.Count -and $State.History[$State.History.Count - 1] -ceq $source) { return }
    $State.History.Add($source)
    if ($State.History.Count -gt 256) { $State.History.RemoveAt(0) }
}

function Get-PreviousBoundary([string]$Text, [int]$Cursor) {
    $previous = 0
    foreach ($index in [Globalization.StringInfo]::ParseCombiningCharacters($Text)) {
        if ($index -ge $Cursor) { break }
        $previous = $index
    }
    return $previous
}

function Get-NextBoundary([string]$Text, [int]$Cursor) {
    foreach ($index in [Globalization.StringInfo]::ParseCombiningCharacters($Text)) {
        if ($index -gt $Cursor) { return $index }
    }
    return $Text.Length
}

function Move-TerminalCompletion($State, [bool]$Previous) {
    $completion = $State.Completion
    if (!$completion -or !$completion.Items.Count) { return }
    $count = $completion.Items.Count
    $completion.Selected = if ($Previous) { ($completion.Selected - 1 + $count) % $count } else { ($completion.Selected + 1) % $count }
    $item = $completion.Items[$completion.Selected]
    $source = $completion.Source.Substring(0, $completion.Begin) + $item.text + $completion.Source.Substring($completion.End)
    $encoding = [Text.UTF8Encoding]::new($false, $true)
    $bytes = $encoding.GetBytes($source)
    $cursorBytes = $completion.BeginBytes + [int]$item.cursor
    if ($cursorBytes -lt 0 -or $cursorBytes -gt $bytes.Length) { throw 'Invalid completion cursor.' }
    $State.Buffer = $source.Substring($completion.Prefix.Length)
    $State.Cursor = $encoding.GetString($bytes, 0, $cursorBytes).Length - $completion.Prefix.Length
}

function Set-TerminalCompletions($State, $Result, [string]$Prefix = '', [bool]$Previous = $false) {
    $State.Completion = $null
    $items = @($Result.items)
    if (!$items.Count) { return $false }
    $source = $Prefix + $State.Buffer
    $encoding = [Text.UTF8Encoding]::new($false, $true)
    $bytes = $encoding.GetBytes($source)
    $beginBytes = [int]$Result.begin; $endBytes = [int]$Result.end
    if ($beginBytes -lt 0 -or $endBytes -lt $beginBytes -or $endBytes -gt $bytes.Length) { throw 'Invalid completion range.' }
    $begin = $encoding.GetString($bytes, 0, $beginBytes).Length
    $end = $encoding.GetString($bytes, 0, $endBytes).Length
    if ($begin -lt $Prefix.Length) { throw 'Completion would replace a previous input line.' }
    $State.Completion = [pscustomobject]@{
        Source = $source; Prefix = $Prefix; Begin = $begin; End = $end; BeginBytes = $beginBytes
        Items = $items; Selected = if ($Previous) { 0 } else { -1 }
    }
    Move-TerminalCompletion $State $Previous
    return $true
}

# Value-only editing rules; the console reader supplies keys and renders the result.
function Edit-TerminalInput($State, [ConsoleKeyInfo]$Key) {
    $control = ($Key.Modifiers -band [ConsoleModifiers]::Control) -ne 0
    $shift = ($Key.Modifiers -band [ConsoleModifiers]::Shift) -ne 0
    if ($control -and $Key.Key -eq [ConsoleKey]::C) { return 'Cancel' }
    if ($control -and $Key.Key -eq [ConsoleKey]::L) { return 'ClearScreen' }
    if ($control -and $Key.Key -eq [ConsoleKey]::D -and !$State.Buffer) { return 'Exit' }
    if ($control -and $Key.Key -eq [ConsoleKey]::Spacebar) { $State.Completion = $null; return 'Complete' }
    if ($State.Completion) {
        if ($Key.Key -in @([ConsoleKey]::Tab, [ConsoleKey]::UpArrow, [ConsoleKey]::DownArrow)) {
            Move-TerminalCompletion $State ($shift -or $Key.Key -eq [ConsoleKey]::UpArrow)
            return 'Edit'
        }
        $State.Completion = $null
        if ($Key.Key -eq [ConsoleKey]::Escape) { return 'Edit' }
    }
    switch ($Key.Key) {
        Enter {
            if (!$shift) { return 'Submit' }
            $State.Buffer = $State.Buffer.Insert($State.Cursor, "`n")
            $State.Cursor++
        }
        UpArrow {
            if ($State.Continuation -or !$State.History.Count) { break }
            if (!$State.Browsing) {
                $State.SavedDraft = $State.Buffer
                $State.SavedCursor = $State.Cursor
                $State.Index = $State.History.Count
                $State.Browsing = $true
            }
            if ($State.Index -gt 0) { $State.Index-- }
            $State.Buffer = $State.History[$State.Index]
            $State.Cursor = $State.Buffer.Length
        }
        DownArrow {
            if ($State.Continuation -or !$State.Browsing) { break }
            if ($State.Index -lt $State.History.Count) { $State.Index++ }
            if ($State.Index -eq $State.History.Count) {
                $State.Buffer = $State.SavedDraft
                $State.Cursor = $State.SavedCursor
                $State.Browsing = $false
            } else {
                $State.Buffer = $State.History[$State.Index]
                $State.Cursor = $State.Buffer.Length
            }
        }
        LeftArrow { $State.Cursor = Get-PreviousBoundary $State.Buffer $State.Cursor }
        RightArrow { $State.Cursor = Get-NextBoundary $State.Buffer $State.Cursor }
        Home { $State.Cursor = 0 }
        End { $State.Cursor = $State.Buffer.Length }
        Backspace {
            if ($State.Cursor) {
                $previous = Get-PreviousBoundary $State.Buffer $State.Cursor
                $State.Buffer = $State.Buffer.Remove($previous, $State.Cursor - $previous)
                $State.Cursor = $previous
            }
        }
        Delete {
            if ($State.Cursor -lt $State.Buffer.Length) {
                $next = Get-NextBoundary $State.Buffer $State.Cursor
                $State.Buffer = $State.Buffer.Remove($State.Cursor, $next - $State.Cursor)
            }
        }
        Escape { $State.Buffer = ''; $State.Cursor = 0 }
        Tab {
            if (!$State.Buffer.Trim()) { $State.Buffer = $State.Buffer.Insert($State.Cursor, '    '); $State.Cursor += 4 }
            elseif ($shift) { return 'CompletePrevious' }
            else { return 'Complete' }
        }
        default {
            if (!$control -and ![char]::IsControl($Key.KeyChar)) {
                $State.Buffer = $State.Buffer.Insert($State.Cursor, [string]$Key.KeyChar)
                $State.Cursor++
            }
        }
    }
    return 'Edit'
}

function Get-TerminalInputView($State, [int]$Width) {
    $convert = { param([string]$Text) $Text.Replace("`r", '').Replace("`n", (' ' + [char]0x21B5 + ' ')).Replace("`t", '    ') }
    $text = & $convert $State.Buffer
    $cursor = (& $convert $State.Buffer.Substring(0, $State.Cursor)).Length
    $capacity = [Math]::Max(1, $Width - 8)
    $indices = [Globalization.StringInfo]::ParseCombiningCharacters($text)
    $widths = [int[]]::new($indices.Length)
    $position = 0
    for ($i = 0; $i -lt $indices.Length; $i++) {
        if ($indices[$i] -lt $cursor) { $position++ }
        $offset = $indices[$i]
        $next = if ($i + 1 -lt $indices.Length) { $indices[$i + 1] } else { $text.Length }
        $glyph = $text.Substring($offset, $next - $offset)
        $code = [int]$glyph[0]
        if ($glyph.Length -gt 1 -and [char]::IsSurrogatePair($glyph, 0)) { $code = [char]::ConvertToUtf32($glyph, 0) }
        $wide = ($code -ge 0x1100 -and $code -le 0x115F) -or
                ($code -ge 0x2E80 -and $code -le 0xA4CF) -or
                ($code -ge 0xAC00 -and $code -le 0xD7A3) -or
                ($code -ge 0xF900 -and $code -le 0xFAFF) -or
                ($code -ge 0xFE10 -and $code -le 0xFE6F) -or
                ($code -ge 0xFF00 -and $code -le 0xFF60) -or
                ($code -ge 0xFFE0 -and $code -le 0xFFE6) -or
                ($code -ge 0x1F1E6 -and $code -le 0x1FAFF) -or
                ($code -ge 0x20000 -and $code -le 0x3FFFD) -or $glyph.Contains([char]0xFE0F)
        $widths[$i] = if ($wide) { 2 } else { 1 }
    }
    $first = $position; $cells = 0
    while ($first -gt 0 -and $cells + $widths[$first - 1] -lt $capacity) {
        $first--; $cells += $widths[$first]
    }
    $last = $position
    while ($last -lt $indices.Length -and $cells + $widths[$last] -le $capacity) {
        $cells += $widths[$last]; $last++
    }
    $start = if ($first -lt $indices.Length) { $indices[$first] } else { $text.Length }
    $end = if ($last -lt $indices.Length) { $indices[$last] } else { $text.Length }
    $left = if ($start) { '<' } else { ' ' }
    $right = if ($end -lt $text.Length) { '>' } else { ' ' }
    [pscustomobject]@{
        Before = $left + $text.Substring($start, $cursor - $start)
        After = $text.Substring($cursor, $end - $cursor) + $right
    }
}

function Read-TerminalInput($State, [bool]$Continuation, [scriptblock]$Complete = $null, [string]$Prefix = '') {
    $State.Buffer = ''; $State.Cursor = 0; $State.Browsing = $false
    $State.Completion = $null
    $State.Continuation = $Continuation
    $prompt = if ($Continuation) { '... ' } else { '> ' }
    $controlC = [Console]::TreatControlCAsInput
    try {
        [Console]::TreatControlCAsInput = $true
        while ($true) {
            $width = [Console]::WindowWidth
            $view = Get-TerminalInputView $State $width
            $row = [Console]::CursorTop
            [Console]::SetCursorPosition(0, $row)
            [Console]::Write($prompt + $view.Before)
            $column = [Console]::CursorLeft
            [Console]::Write($view.After)
            $padding = [Math]::Max(0, $width - 1 - [Console]::CursorLeft)
            [Console]::Write(' ' * $padding)
            [Console]::SetCursorPosition($column, $row)
            $action = Edit-TerminalInput $State ([Console]::ReadKey($true))
            if ($action -eq 'Edit') { continue }
            if ($action -eq 'ClearScreen') { [Console]::Clear(); continue }
            if ($action -in @('Complete', 'CompletePrevious')) {
                if (!$Complete) { continue }
                $response = & $Complete ($Prefix + $State.Buffer) ($Prefix.Length + $State.Cursor)
                [Console]::SetCursorPosition(0, $row)
                [Console]::Write(' ' * [Math]::Max(0, $width - 1))
                [Console]::SetCursorPosition(0, $row)
                [Console]::WriteLine($prompt + $State.Buffer.Replace("`n", "`n... "))
                if ($response.PSObject.Properties['Error'] -and $response.Error) {
                    [Console]::WriteLine('Completion unavailable: ' + $response.Error)
                    continue
                }
                $items = @($response.items)
                if (!$items.Count) { [Console]::WriteLine('No suggestions') }
                else {
                    $matchesLabel = if ($items.Count -lt $response.total) { "$($items.Count) of $($response.total) matches" } else { "$($response.total) matches" }
                    [Console]::WriteLine($matchesLabel + '; Tab/Shift+Tab cycles, Esc dismisses')
                    foreach ($item in ($items | Select-Object -First 8)) {
                        [Console]::WriteLine('  ' + $item.name + ' : ' + $item.description)
                    }
                }
                if ($response.signature) { [Console]::WriteLine($response.signature) }
                if ($response.parameter) { [Console]::WriteLine($response.parameter) }
                [void](Set-TerminalCompletions $State $response $Prefix ($action -eq 'CompletePrevious'))
                continue
            }
            [Console]::SetCursorPosition(0, $row)
            [Console]::Write(' ' * [Math]::Max(0, $width - 1))
            [Console]::SetCursorPosition(0, $row)
            if ($action -eq 'Submit') {
                [Console]::WriteLine($prompt + $State.Buffer.Replace("`n", "`n... "))
            } elseif ($action -eq 'Cancel') {
                [Console]::WriteLine($prompt + '^C')
            } else {
                [Console]::WriteLine()
            }
            return [pscustomobject]@{ Action = $action; Text = $State.Buffer }
        }
    } finally {
        [Console]::TreatControlCAsInput = $controlC
    }
}

Export-ModuleMember -Function New-TerminalInput, Add-TerminalHistory, Edit-TerminalInput, Get-TerminalInputView, Read-TerminalInput, Set-TerminalCompletions
