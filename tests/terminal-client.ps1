param([Parameter(Mandatory)][string]$Client)
$ErrorActionPreference = 'Stop'
$encoding = [Text.UTF8Encoding]::new($false, $true)

function Read-TestFrame($Pipe) {
    $header = [byte[]]::new(4)
    Read-TestBytes $Pipe $header
    $length = [BitConverter]::ToUInt32($header, 0)
    if ($length -gt 65568) { throw 'Unexpected test request length.' }
    $bytes = [byte[]]::new($length)
    Read-TestBytes $Pipe $bytes
    return $encoding.GetString($bytes)
}
function Read-TestBytes($Pipe, [byte[]]$Bytes) {
    $offset = 0
    while ($offset -lt $Bytes.Length) {
        $task = $Pipe.ReadAsync($Bytes, $offset, $Bytes.Length - $offset)
        if (!$task.Wait(10000)) { throw 'Client did not send its request.' }
        $count = $task.GetAwaiter().GetResult()
        if (!$count) { throw 'Client disconnected before sending its request.' }
        $offset += $count
    }
}
function Write-TestFrame($Pipe, [string]$Text) {
    $bytes = $encoding.GetBytes($Text)
    $header = [BitConverter]::GetBytes([uint32]$bytes.Length)
    $Pipe.Write($header, 0, 4)
    $Pipe.Write($bytes, 0, $bytes.Length)
    $Pipe.Flush()
}

function Check-Client([string]$Status, [string]$Result, [bool]$Json, [bool]$FromFile = $false, [bool]$SingleLine = $false) {
    $name = 'TheElderShell-test-' + [Guid]::NewGuid().ToString('N')
    $server = [IO.Pipes.NamedPipeServerStream]::new($name, [IO.Pipes.PipeDirection]::InOut, 1,
        [IO.Pipes.PipeTransmissionMode]::Byte, [IO.Pipes.PipeOptions]::Asynchronous)
    $process = [Diagnostics.Process]::new()
    $started = $false
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ($name + '.psc')
    try {
        $source = "var greeting = `"Ol" + [char]0xE1 + "`"`ngreeting"
        if ($SingleLine) { $source = "`r`n  Game.GetPlayer().GetLevel()`r`n" }
        [IO.File]::WriteAllText($temporary, $source, $encoding)
        $process.StartInfo.FileName = (Get-Process -Id $PID).Path
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.CreateNoWindow = $true
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        $arguments = @('-NoProfile', '-File', $Client, '-PipeName', $name, '-TimeoutSeconds', '5')
        $arguments += if ($FromFile) { @('-FilePath', $temporary) } else { @('-Command', $source) }
        if ($Json) { $arguments += '-AsJson' }
        foreach ($argument in $arguments) { $process.StartInfo.ArgumentList.Add($argument) }
        $connection = $server.WaitForConnectionAsync()
        if (!$process.Start()) { throw 'Could not start the PowerShell client.' }
        $started = $true
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$connection.Wait(10000)) { throw 'Client did not connect.' }
        $connection.GetAwaiter().GetResult()
        Write-TestFrame $server "hello`n1`n$name"
        $request = Read-TestFrame $server
        if ($request -cne ("execute`n" + $source.Trim())) { throw 'Client altered the source or UTF-8 payload.' }
        Write-TestFrame $server "pending`n42`n7`nCompiling`nWorking"
        Write-TestFrame $server ("result`n42`n7`n" + $Status + "`n" + $Result)
        if (!$process.WaitForExit(10000)) { throw 'Client did not finish.' }
        $output = $stdout.GetAwaiter().GetResult()
        $errorText = $stderr.GetAwaiter().GetResult()
        if ($Json) {
            $decoded = $output | ConvertFrom-Json
            if ($process.ExitCode -ne 0 -or $decoded.Ticket -ne '42' -or $decoded.Generation -ne '7' -or
                $decoded.Status -cne $Status -or $decoded.Result -cne $Result) {
                throw "JSON result mismatch: $output $errorText"
            }
        } elseif ($Status -eq 'Completed') {
            if ($process.ExitCode -ne 0 -or $output.TrimEnd("`r", "`n") -cne $Result) { throw "Plain result mismatch: $output $errorText" }
        } elseif ($process.ExitCode -eq 0 -or !$errorText.Contains($Result)) {
            throw "Failed request did not raise its actual error: $output $errorText"
        }
    } finally {
        $server.Dispose()
        if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}

Check-Client 'Completed' "line 1`nline 2" $true
Check-Client 'Completed' '95' $false
Check-Client 'Abandoned after load' 'Session changed; no replay' $true
Check-Client 'Rejected' 'Synthetic rejected command' $false
Check-Client 'Completed' 'file result' $true $true
Check-Client 'Completed' '97' $true $true $true

function Check-Completion([bool]$Unsupported) {
    $name = 'TheElderShell-test-' + [Guid]::NewGuid().ToString('N')
    $server = [IO.Pipes.NamedPipeServerStream]::new($name, [IO.Pipes.PipeDirection]::InOut, 1,
        [IO.Pipes.PipeTransmissionMode]::Byte, [IO.Pipes.PipeOptions]::Asynchronous)
    $process = [Diagnostics.Process]::new()
    $started = $false
    try {
        $source = 'var label = "' + [char]0xE9 + '"' + "`nGame.GetP trailing"
        $cursor = $source.IndexOf(' trailing')
        $process.StartInfo.FileName = (Get-Process -Id $PID).Path
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.CreateNoWindow = $true
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        foreach ($argument in @('-NoProfile', '-File', $Client, '-PipeName', $name, '-TimeoutSeconds', '5',
                                '-Complete', $source, '-Cursor', "$cursor", '-AsJson')) {
            $process.StartInfo.ArgumentList.Add($argument)
        }
        $connection = $server.WaitForConnectionAsync()
        if (!$process.Start()) { throw 'Could not start completion client.' }
        $started = $true
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$connection.Wait(10000)) { throw 'Completion client did not connect.' }
        $connection.GetAwaiter().GetResult()
        Write-TestFrame $server "hello`n1`n$name"
        $request = Read-TestFrame $server
        $byteCursor = $encoding.GetByteCount($source.Substring(0, $cursor))
        if ($request -cne ("complete`n" + $byteCursor + "`n" + $source)) { throw 'Completion source or byte cursor altered.' }
        if ($Unsupported) {
            Write-TestFrame $server "error`nCompletion unavailable"
        } else {
            Write-TestFrame $server "pending`n0`n7`nQueued`nReading metadata"
            Write-TestFrame $server ('result' + "`n0`n7`nCompleted`n" +
                '{"begin":20,"end":24,"total":1,"signature":"Actor Game.GetPlayer()","parameter":"","items":[{"name":"GetPlayer","description":"Actor","text":"GetPlayer()","cursor":11}]}')
        }
        if (!$process.WaitForExit(10000)) { throw 'Completion client did not finish.' }
        $output = $stdout.GetAwaiter().GetResult()
        $errorText = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Completion client failed: $errorText" }
        $decoded = $output | ConvertFrom-Json
        if ($Unsupported) {
            if ($decoded.Error -cne 'Completion unavailable' -or $decoded.items.Count -ne 0) { throw 'Unsupported completion error lost.' }
        } elseif ($decoded.total -ne 1 -or $decoded.items[0].text -cne 'GetPlayer()' -or
                  $decoded.signature -cne 'Actor Game.GetPlayer()') {
            throw "Completion metadata mismatch: $output"
        }
    } finally {
        $server.Dispose()
        if ($started -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
Check-Completion $false
Check-Completion $true
Write-Output 'PowerShell terminal client tests passed.'
