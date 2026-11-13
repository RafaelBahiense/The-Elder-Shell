[CmdletBinding(DefaultParameterSetName = 'Interactive')]
param(
    [Parameter(ParameterSetName = 'Command', Mandatory)][string]$Command,
    [Parameter(ParameterSetName = 'File', Mandatory)][string]$FilePath,
    [Parameter(ParameterSetName = 'Completion', Mandatory)][AllowEmptyString()][string]$Complete,
    [Parameter(ParameterSetName = 'Completion')][ValidateRange(-1, 65536)][int]$Cursor = -1,
    [int]$GameProcessId,
    [string]$PipeName,
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 60,
    [ValidateRange(1, 60)][int]$ConnectTimeoutSeconds = 5,
    [switch]$AsJson
)

$ErrorActionPreference = 'Stop'
if (!$PipeName) {
    if (!$GameProcessId) {
        $games = @(Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue)
        if ($games.Count -ne 1) { throw 'Specify -GameProcessId when Skyrim is not running or more than one instance exists.' }
        $GameProcessId = $games[0].Id
    }
    $PipeName = "TheElderShell-$GameProcessId"
}
if ($PipeName -match '[\\/]' -or !$PipeName.StartsWith('TheElderShell-')) {
    throw 'Use a local TheElderShell-<process ID> pipe name.'
}
$interactive = $PSCmdlet.ParameterSetName -eq 'Interactive'
$encoding = [Text.UTF8Encoding]::new($false, $true)
$pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $PipeName, [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
$maximumFrame = 8 * 1024 * 1024

function Read-Bytes([int]$Count, [DateTime]$Deadline) {
    $bytes = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $remaining = [int][Math]::Min([int]::MaxValue, ($Deadline - [DateTime]::UtcNow).TotalMilliseconds)
        if ($remaining -le 0) { throw 'Timed out. A submitted command may still execute; it has not been cancelled and must not be replayed automatically.' }
        $task = $pipe.ReadAsync($bytes, $offset, $Count - $offset)
        if (!$task.Wait($remaining)) {
            throw 'Timed out. A submitted command may still execute; it has not been cancelled and must not be replayed automatically.'
        }
        $received = $task.GetAwaiter().GetResult()
        if (!$received) { throw 'Elder Shell disconnected. Submitted commands are not cancelled or replayed.' }
        $offset += $received
    }
    return ,$bytes
}

function Read-Frame([DateTime]$Deadline) {
    $length = [BitConverter]::ToUInt32((Read-Bytes 4 $Deadline), 0)
    if (!$length -or $length -gt $maximumFrame) { throw 'Invalid Elder Shell response length.' }
    return $encoding.GetString((Read-Bytes ([int]$length) $Deadline))
}

function Write-Frame([string]$Text) {
    $bytes = $encoding.GetBytes($Text)
    if ($bytes.Length -gt 65568) { throw 'Command exceeds the terminal input limit.' }
    $header = [BitConverter]::GetBytes([uint32]$bytes.Length)
    $pipe.Write($header, 0, 4)
    $pipe.Write($bytes, 0, $bytes.Length)
    $pipe.Flush()
}

function Send-Source([string]$Source, [bool]$AllowContinuation) {
    if ($encoding.GetByteCount($Source) -gt 65536) { throw 'Command exceeds 65536 UTF-8 bytes.' }
    $Source = $Source.Trim()
    $operation = if ($AllowContinuation) { 'input' } else { 'execute' }
    Write-Frame ($operation + "`n" + $Source)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ($true) {
        $frame = Read-Frame $deadline
        if ($frame -eq 'continue') { return $null }
        if ($frame.StartsWith("error`n")) { throw $frame.Substring(6) }
        $parts = $frame -split "`n", 5
        if ($parts.Count -ne 5 -or $parts[0] -notin @('pending', 'result')) { throw 'Invalid Elder Shell response.' }
        if ($parts[0] -eq 'pending') {
            Write-Verbose "Request $($parts[1]): $($parts[3])"
            continue
        }
        return [pscustomobject]@{ Ticket = $parts[1]; Generation = $parts[2]; Status = $parts[3]; Result = $parts[4] }
    }
}

function Request-Completion([string]$Source, [int]$Position) {
    if ($Position -lt 0 -or $Position -gt $Source.Length -or $encoding.GetByteCount($Source) -gt 65536) {
        throw 'Invalid completion source or cursor.'
    }
    $byteCursor = $encoding.GetByteCount($Source.Substring(0, $Position))
    Write-Frame ("complete`n" + $byteCursor + "`n" + $Source)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ($true) {
        $frame = Read-Frame $deadline
        if ($frame.StartsWith("error`n")) { return [pscustomobject]@{ Error = $frame.Substring(6); items = @() } }
        $parts = $frame -split "`n", 5
        if ($parts.Count -ne 5 -or $parts[0] -notin @('pending', 'result')) { throw 'Invalid completion response.' }
        if ($parts[0] -eq 'pending') { continue }
        if ($parts[3] -ne 'Completed') { return [pscustomobject]@{ Error = $parts[4]; items = @() } }
        return $parts[4] | ConvertFrom-Json
    }
}

try {
    try { $pipe.Connect($ConnectTimeoutSeconds * 1000) }
    catch { throw "Cannot connect to $PipeName. Enable External terminal in Elder Shell Settings and check that another client is not connected. $($_.Exception.Message)" }
    $hello = (Read-Frame ([DateTime]::UtcNow.AddSeconds($ConnectTimeoutSeconds))) -split "`n", 3
    if ($hello.Count -ne 3 -or $hello[0] -ne 'hello' -or $hello[1] -ne '1') { throw 'Unsupported Elder Shell terminal protocol.' }
    if (!$interactive) {
        if ($PSCmdlet.ParameterSetName -eq 'Completion') {
            $position = if ($Cursor -lt 0) { $Complete.Length } else { $Cursor }
            $result = Request-Completion $Complete $position
            if ($AsJson) { $result | ConvertTo-Json -Depth 8 -Compress }
            elseif ($result.PSObject.Properties['Error']) { throw $result.Error }
            else { $result.items | Select-Object name, description }
            return
        }
        $source = if ($PSCmdlet.ParameterSetName -eq 'File') { [IO.File]::ReadAllText((Resolve-Path -LiteralPath $FilePath).Path) } else { $Command }
        $result = Send-Source $source $false
        if ($AsJson) { $result | ConvertTo-Json -Compress }
        elseif ($result.Status -eq 'Completed') { $result.Result }
        else { throw "$($result.Status): $($result.Result)" }
        return
    }
    if ([Console]::IsInputRedirected) { throw 'Interactive input requires a terminal. Use -Command or -FilePath for automation.' }
    Import-Module (Join-Path $PSScriptRoot 'TerminalInput.psm1') -Force
    $inputState = New-TerminalInput
    [Console]::Clear()
    Write-Host "Connected to $($hello[2]). Type exit to disconnect."
    $draft = ''
    while ($pipe.IsConnected) {
        $inputLine = Read-TerminalInput $inputState ([bool]$draft) { param($source, $position) Request-Completion $source $position } $draft
        if ($inputLine.Action -eq 'Exit') { break }
        if ($inputLine.Action -eq 'Cancel') { $draft = ''; continue }
        $line = $inputLine.Text
        if (!$draft -and $line.Trim() -eq 'exit') { break }
        if (!$draft -and $line.Trim() -eq 'clear') {
            Add-TerminalHistory $inputState $line
            [Console]::Clear()
            continue
        }
        if (!$draft -and !$line.Trim()) { continue }
        $draft += $line + "`n"
        $result = Send-Source $draft $true
        if ($null -eq $result) { continue }
        Add-TerminalHistory $inputState $draft
        if ($AsJson) { $result | ConvertTo-Json -Compress }
        else {
            if ($result.Status -ne 'Completed') { Write-Host "$($result.Status):" }
            Write-Host $result.Result
        }
        $draft = ''
    }
} finally {
    $pipe.Dispose()
}
