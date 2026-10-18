[CmdletBinding()]
param([switch] $Fix, [switch] $NativeAnalysis, [string] $ClangFormat)

$ErrorActionPreference = 'Stop'
if (-not $ClangFormat) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        foreach ($relative in @('VC/Tools/Llvm/x64/bin/clang-format.exe', 'VC/Tools/Llvm/bin/clang-format.exe')) {
            if ($vs -and (Test-Path -LiteralPath (Join-Path $vs $relative))) {
                $ClangFormat = Join-Path $vs $relative
                break
            }
        }
    }
    if (-not $ClangFormat) {
        $command = Get-Command clang-format -CommandType Application -ErrorAction SilentlyContinue
        if ($command) { $ClangFormat = $command.Source }
    }
}
if (-not $ClangFormat) { throw 'clang-format was not found. Supply -ClangFormat with an existing executable path.' }

$files = foreach ($directory in @('src', 'tests')) {
    Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $directory) -Recurse -File |
        Where-Object Extension -In @('.cpp', '.h', '.hpp') | ForEach-Object FullName
}
$flags = @(if ($Fix) { '-i' } else { '--dry-run'; '--Werror' })
& $ClangFormat '--style=file' '--fallback-style=none' @flags @files
if ($LASTEXITCODE -ne 0) { throw 'C++ formatting failed. Run ./lint.ps1 -Fix and review the changes.' }
if ($NativeAnalysis) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Target Analyze
    if ($LASTEXITCODE -ne 0) { throw 'Native analysis failed.' }
}
Write-Host 'Lint passed.'
