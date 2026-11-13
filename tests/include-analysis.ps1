$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'tools/IncludeAnalysis.ps1')
. (Join-Path $root 'tools/NativeEnvironment.ps1')
Initialize-NativeEnvironment
$tidy = Resolve-ClangTool 'clang-tidy'
$fixture = Join-Path $root ('build/testing/includes-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
Copy-Item -LiteralPath (Join-Path $root '.clang-tidy') -Destination (Join-Path $fixture '.clang-tidy')
$source = Join-Path $fixture 'sample.cpp'
$original = "#include `"bridge.h`"`n#include <vector>`nstd::string Label() { return `"example`"; }`n"
[IO.File]::WriteAllText((Join-Path $fixture 'bridge.h'), "#include <string>`n")
[IO.File]::WriteAllText($source, $original)
$database = @(@{ directory = $fixture; file = $source; arguments = @('clang-cl.exe', '/c', '/std:c++latest', '/EHsc', $source) })
[IO.File]::WriteAllText((Join-Path $fixture 'compile_commands.json'), (ConvertTo-Json -InputObject $database -Depth 4))
$parameters = @{ Root = $fixture; Native = $fixture; ClangTidy = $tidy; Files = @($source) }
$rejected = $false
try { Invoke-IncludeAnalysis @parameters } catch { $rejected = $true }
if (!$rejected) { throw 'Include checks accepted unused/transitive includes.' }
if ([IO.File]::ReadAllText($source) -cne $original) { throw 'Check-only analysis rewrote the fixture.' }
Invoke-IncludeAnalysis @parameters -Fix
$corrected = [IO.File]::ReadAllText($source)
if ($corrected -match '#include <vector>' -or $corrected -notmatch '#include <string>') {
    throw 'Include fixes failed to remove the unused include and add the direct include.'
}
$selected = @(Select-IncludeAnalysisFiles -Root $fixture -Files @($source) -Scope @('sample.cpp'))
if ($selected.Count -ne 1 -or $selected[0] -cne $source) { throw 'File scope selection failed.' }
$rejected = $false
try { Select-IncludeAnalysisFiles -Root $fixture -Files @($source) -Scope @('missing') } catch { $rejected = $true }
if (!$rejected) { throw 'Unmatched scope was accepted.' }
[IO.File]::WriteAllText($source, 'int Broken( {')
$rejected = $false
try { Invoke-IncludeAnalysis @parameters -Fix } catch { $rejected = $true }
if (!$rejected) { throw 'Fix mode accepted a parse failure.' }
Write-Output 'Include analysis checks, fixes, scopes and parse-error rejection passed.'
exit 0
