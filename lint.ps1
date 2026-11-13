[CmdletBinding()]
param(
    [ValidateSet('All', 'CSharp', 'Cpp')][string] $Language = 'All',
    [switch] $Fix,
    [switch] $NativeAnalysis,
    [switch] $IncludeAnalysis,
    [string[]] $IncludeScope,
    [string] $ClangFormat,
    [string] $ClangTidy
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'tools/IncludeAnalysis.ps1')
. (Join-Path $PSScriptRoot 'tools/NativeEnvironment.ps1')
if (($NativeAnalysis -or $IncludeAnalysis) -and $Language -eq 'CSharp') { throw 'Native/include analysis requires Cpp or All.' }
if (($IncludeScope -or $ClangTidy) -and !$IncludeAnalysis) { throw '-IncludeScope and -ClangTidy require -IncludeAnalysis.' }

Push-Location $PSScriptRoot
try {
    if ($Language -in @('All', 'Cpp')) {
        $ClangFormat = Resolve-ClangTool 'clang-format' $ClangFormat
        $files = @(foreach ($directory in @('src/native', 'tests/native', 'tools')) {
            Get-ChildItem -LiteralPath $directory -Recurse -File |
                Where-Object Extension -In @('.cpp', '.h', '.hpp') | ForEach-Object FullName
        })
        if ($IncludeAnalysis) {
            Initialize-NativeEnvironment
            $selected = @(Select-IncludeAnalysisFiles -Root $PSScriptRoot -Files $files -Scope $IncludeScope)
            $ClangTidy = Resolve-ClangTool 'clang-tidy' $ClangTidy
            Invoke-IncludeAnalysis -Root $PSScriptRoot -Native (Join-Path $PSScriptRoot 'build') -ClangTidy $ClangTidy -Files $selected -Fix:$Fix
        }
        $flags = @(if ($Fix) { '-i' } else { '--dry-run'; '--Werror' })
        for ($offset = 0; $offset -lt $files.Count; $offset += 64) {
            $batch = @($files | Select-Object -Skip $offset -First 64)
            & $ClangFormat '--style=file' '--fallback-style=none' @flags @batch
            if ($LASTEXITCODE -ne 0) { throw 'C++ formatting failed. Run ./lint.ps1 -Fix and review the changes.' }
        }
    }
    if ($Language -in @('All', 'CSharp')) {
        foreach ($project in @('src/plugin/TheElderShell.Plugin.csproj', 'tests/plugin/TheElderShell.Plugin.Tests.csproj')) {
            $arguments = @('format', $project, '--no-restore', '--severity', 'info')
            if (!$Fix) { $arguments += '--verify-no-changes' }
            & dotnet @arguments
            if ($LASTEXITCODE -ne 0) { throw 'C# formatting failed. Run ./lint.ps1 -Fix and review the changes.' }
        }
    }
    if ($NativeAnalysis) {
        & (Join-Path $PSScriptRoot 'build.ps1') -Target Analyze
        if ($LASTEXITCODE -ne 0) { throw 'Native analysis failed.' }
    }
    Write-Host 'Lint passed.'
} finally { Pop-Location }
