function Resolve-ClangTool {
    param([string]$Name, [string]$Override)

    if ($Override) {
        $command = Get-Command $Override -CommandType Application -ErrorAction SilentlyContinue
        if (-not $command) { throw "$Name was not found at '$Override'." }
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        foreach ($relative in @("VC/Tools/Llvm/x64/bin/$Name.exe", "VC/Tools/Llvm/bin/$Name.exe")) {
            $candidate = if ($installation) { Join-Path $installation $relative }
            if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
        }
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "$Name was not found. Supply an existing executable with -ClangTidy or -ClangFormat."
}

function Select-IncludeAnalysisFiles {
    param([string]$Root, [string[]]$Files, [string[]]$Scope)

    if (-not $Scope) { return $Files | Sort-Object -Unique }
    $selected = foreach ($path in $Scope) {
        $absolute = if ([IO.Path]::IsPathRooted($path)) {
            [IO.Path]::GetFullPath($path)
        } else {
            [IO.Path]::GetFullPath((Join-Path $Root $path))
        }
        $prefix = $absolute.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $matches = @($Files | Where-Object {
            $_.Equals($absolute, [StringComparison]::OrdinalIgnoreCase) -or
            $_.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
        })
        if (-not $matches.Count) { throw "Include scope '$path' contains no project C++ files." }
        $matches
    }
    return $selected | Sort-Object -Unique
}

function Invoke-IncludeAnalysis {
    param([string]$Root, [string]$Native, [string]$ClangTidy, [string[]]$Files, [switch]$Fix)

    $database = Join-Path $Native 'compile_commands.json'
    if (-not (Test-Path -LiteralPath $database)) {
        throw "Include analysis requires a configured native build with compile_commands.json: $Native"
    }
    if (-not $Files.Count) { throw 'No C++ files selected for include analysis.' }

    $configuration = Join-Path $Root '.clang-tidy'
    $checks = & $ClangTidy --list-checks "--config-file=$configuration"
    if ($LASTEXITCODE -ne 0 -or -not ($checks -match '^\s+misc-include-cleaner\s*$')) {
        throw 'The selected clang-tidy does not provide misc-include-cleaner.'
    }

    $logDirectory = Join-Path $Root 'build/lint'
    [IO.Directory]::CreateDirectory($logDirectory) | Out-Null
    # Use one command per source, preferring targets without a PCH. MSVC-only
    # analysis/path-map options are not understood by Clang's parser. Keep the
    # build database untouched and write the adjusted copy beside the log.
    $commands = [IO.File]::ReadAllText($database) | ConvertFrom-Json
    $adjusted = @(foreach ($group in ($commands | Group-Object file)) {
        $entry = $group.Group | Sort-Object @{ Expression = {
            if ($_.output -match 'console_tests') { 0 }
            elseif ($_.output -match 'native_analysis') { 2 }
            else { 1 }
        } } | Select-Object -First 1
        if ($entry.PSObject.Properties['command']) {
            $entry.command = [regex]::Replace($entry.command, '(?i)(?<!\S)(?:"/(?:analyze|pathmap)[^"]*"|/(?:analyze|pathmap)[^\s]*)(?=\s|$)', '')
        } else {
            $entry.arguments = @($entry.arguments | Where-Object { $_ -notmatch '(?i)^/(analyze|pathmap)' })
        }
        $entry
    })
    [IO.File]::WriteAllText((Join-Path $logDirectory 'compile_commands.json'), (ConvertTo-Json -InputObject $adjusted -Depth 8))
    $log = Join-Path $logDirectory 'include-analysis.log'
    $encoding = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($log, '', $encoding)
    $failures = 0
    $index = 0
    foreach ($file in $Files) {
        ++$index
        Write-Host "Include analysis $index/$($Files.Count): $file"
        # Parse headers as main files, disable MSVC PCH consumption, and use
        # Clang's constexpr offsetof and MSVC-compatible delayed templates.
        # These options affect analysis only, not the native build database.
        $arguments = @($file, '-p', $logDirectory, "--config-file=$configuration",
            '--extra-arg=/Y-', '--extra-arg=/TP', '--extra-arg=/std:c++latest',
            '--extra-arg=/D_CRT_USE_BUILTIN_OFFSETOF', '--extra-arg=-fdelayed-template-parsing',
            '--extra-arg=/WX-', '--extra-arg=/W0', '--extra-arg=-Wno-overriding-option',
            '--extra-arg=-Wno-delayed-template-parsing-in-cxx20')
        $previousPreference = $ErrorActionPreference
        try {
            # Windows PowerShell wraps native stderr in ErrorRecord objects.
            # Capture it and use the process exit code as the failure signal.
            $ErrorActionPreference = 'Continue'
            if ($Fix) {
                $fixed = @(& $ClangTidy @arguments --fix 2>&1)
                [IO.File]::AppendAllText($log, $file + ' (fix)' + [Environment]::NewLine +
                    ($fixed -join [Environment]::NewLine) + [Environment]::NewLine, $encoding)
            }
            # Recheck after fixes: applied edits must leave the file parseable and clean.
            $output = @(& $ClangTidy @arguments 2>&1)
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousPreference
        }
        $lines = @($output | ForEach-Object { $_.ToString() })
        [IO.File]::AppendAllText($log, $file + [Environment]::NewLine +
            ($lines -join [Environment]::NewLine) + [Environment]::NewLine, $encoding)
        if ($exitCode -ne 0) {
            ++$failures
            $lines | ForEach-Object { Write-Host $_ }
        }
    }
    if ($failures) { throw "Include analysis failed for $failures file(s). Full diagnostics: $log" }
    Write-Host "Include analysis passed for $($Files.Count) file(s)."
}
