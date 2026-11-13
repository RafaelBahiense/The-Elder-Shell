param([Parameter(Mandatory)][string]$Binary, [Parameter(Mandatory)][string]$Source)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
$identity = [IO.File]::ReadAllText((Join-Path $root 'mod.json')) | ConvertFrom-Json
$binaryZip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Binary))
try {
    $sourceZip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Source))
    try {
        foreach ($archive in @($binaryZip, $sourceZip)) {
            foreach ($entry in $archive.Entries) {
                if ($entry.FullName -match '(^/|\\|(^|/)\.\.(/|$)|:|(^|/)(\.git|\.tools|build|bin|obj)/)') {
                    throw "Unexpected archive path: $($entry.FullName)"
                }
            }
        }
        foreach ($entry in $binaryZip.Entries) {
            if ($entry.FullName -match '(?i)^(src|source)/|\.(psc|cpp|h|hpp|pdb|ini)$') {
                throw "Source, debug symbols or settings in the runtime ZIP: $($entry.FullName)"
            }
        }
        foreach ($name in @("SKSE/Plugins/$($identity.binaryName).dll", 'TheElderShell.esp',
                           'Tools/TheElderShell/Connect-ElderShell.ps1', 'Tools/TheElderShell/TerminalInput.psm1',
                           "docs/$($identity.binaryName)/LICENSE")) {
            if (!$binaryZip.GetEntry($name)) { throw "Missing runtime file: $name" }
        }
        foreach ($script in (Get-ChildItem -LiteralPath (Join-Path $root 'src/papyrus') -Filter '*.psc')) {
            if (!$binaryZip.GetEntry("Scripts/$($script.BaseName).pex")) { throw "Missing compiled script: $($script.Name)" }
            if (!$sourceZip.GetEntry("src/papyrus/$($script.Name)")) { throw "Missing script source: $($script.Name)" }
        }
        foreach ($name in @('mod.json', 'CHANGELOG.md', 'LICENSE', '.clang-tidy', '.clang-format',
                           'lint.ps1', 'build.ps1', 'package.ps1', 'tools/IncludeAnalysis.ps1',
                           'tools/NativeEnvironment.ps1', 'external/Caprica.tar.gz')) {
            if (!$sourceZip.GetEntry($name)) { throw "Missing corresponding-source file: $name" }
        }
        foreach ($entry in $sourceZip.Entries) {
            if ($entry.FullName -match '(?i)\.(dll|pex|pdb|ess|skse)$') { throw "Generated/game data in source ZIP: $($entry.FullName)" }
        }
        $entry = $sourceZip.GetEntry('mod.json')
        $reader = [IO.StreamReader]::new($entry.Open())
        try { $archivedIdentity = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if ($archivedIdentity.version -cne $identity.version) { throw 'Source archive version does not match mod.json.' }
        Write-Host "Release archive layout verified: $($binaryZip.Entries.Count) runtime files, $($sourceZip.Entries.Count) source files."
    } finally { $sourceZip.Dispose() }
} finally { $binaryZip.Dispose() }
