[CmdletBinding()]
param([string] $Tag)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
& (Join-Path $root 'build.ps1') -Target Build
if ($LASTEXITCODE -ne 0) { throw 'Build failed; no release archives were produced.' }

$cache = [IO.File]::ReadAllText((Join-Path $root 'build/CMakeCache.txt'))
if ($cache -notmatch '(?m)^CMAKE_PROJECT_VERSION:STATIC=([0-9]+\.[0-9]+\.[0-9]+)\r?$') {
    throw 'Could not read the configured project version.'
}
$version = $Matches[1]
if ($Tag -and $Tag -cne "v$version") { throw "Release tag must match CMakeLists.txt: v$version" }
$dist = Join-Path $root 'build/dist'
[IO.Directory]::CreateDirectory($dist) | Out-Null
$baseName = "PapyrusConsole-$version"

function Add-File($Files, [string] $DiskPath, [string] $ArchivePath) {
    $item = Get-Item -LiteralPath $DiskPath
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing linked archive input: $DiskPath" }
    $Files.Add($ArchivePath.Replace('\', '/'), $item.FullName)
}

function Add-Tree($Files, [string] $Directory, [string] $Prefix) {
    foreach ($item in (Get-ChildItem -LiteralPath $Directory -Recurse -Force)) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing linked archive input: $($item.FullName)" }
        if (-not $item.PSIsContainer) {
            $relative = $item.FullName.Substring($Directory.TrimEnd('\', '/').Length + 1)
            Add-File $Files $item.FullName "$Prefix/$relative"
        }
    }
}

function Write-Archive([string] $Destination, $Files) {
    $temporary = "$Destination.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        $stream = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew)
        try {
            $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
            try {
                foreach ($name in ($Files.Keys | Sort-Object -CaseSensitive)) {
                    $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
                    # Fixed metadata avoids leaking local file times and makes ZIPs repeatable.
                    $entry.LastWriteTime = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
                    $entry.ExternalAttributes = 0
                    $inputStream = [IO.File]::OpenRead($Files[$name])
                    try {
                        $outputStream = $entry.Open()
                        try { $inputStream.CopyTo($outputStream) }
                        finally { $outputStream.Dispose() }
                    } finally { $inputStream.Dispose() }
                }
            } finally { $archive.Dispose() }
        } finally { $stream.Dispose() }

        $check = [IO.Compression.ZipFile]::OpenRead($temporary)
        try {
            if ($check.Entries.Count -ne $Files.Count) { throw 'Archive entry count mismatch.' }
            foreach ($entry in $check.Entries) {
                if (-not $Files.ContainsKey($entry.FullName) -or $entry.Length -ne (Get-Item -LiteralPath $Files[$entry.FullName]).Length) {
                    throw "Archive validation failed: $($entry.FullName)"
                }
                $entryStream = $entry.Open()
                $hasher = [Security.Cryptography.SHA256]::Create()
                try {
                    $actual = [BitConverter]::ToString($hasher.ComputeHash($entryStream)).Replace('-', '')
                    if ($actual -cne (Get-FileHash -LiteralPath $Files[$entry.FullName] -Algorithm SHA256).Hash) {
                        throw "Archive content mismatch: $($entry.FullName)"
                    }
                } finally { $hasher.Dispose(); $entryStream.Dispose() }
            }
        } finally { $check.Dispose() }
        Move-Item -LiteralPath $temporary -Destination $Destination -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$binary = @{}
Add-File $binary (Join-Path $root 'build/PapyrusConsole.dll') 'SKSE/Plugins/PapyrusConsole.dll'
Add-File $binary (Join-Path $root 'LICENSE') 'docs/PapyrusConsole/LICENSE'
Add-Tree $binary (Join-Path $root 'licenses') 'docs/PapyrusConsole/licenses'

$source = @{}
foreach ($directory in @('src', 'tests', 'cmake', 'docs', 'licenses', '.github')) {
    if (Test-Path -LiteralPath (Join-Path $root $directory)) {
        Add-Tree $source (Join-Path $root $directory) $directory
    }
}
foreach ($file in (Get-ChildItem -LiteralPath $root -File -Force)) {
    if ($file.Extension -eq '.ps1' -or $file.Name -in @('CMakeLists.txt', 'vcpkg.json', 'README.md', 'LICENSE', '.clang-format', '.editorconfig', '.gitignore', 'AGENTS.md')) {
        Add-File $source $file.FullName $file.Name
    }
}

# Ship the compiler's upstream source alongside the tracked port patches.
$port = [IO.File]::ReadAllText((Join-Path $root 'cmake/ports/caprica/portfile.cmake'))
$revision = [regex]::Match($port, '\bREF\s+([a-f0-9]{40})\b').Groups[1].Value
$checksum = [regex]::Match($port, '\bSHA512\s+([a-f0-9]{128})\b').Groups[1].Value
if (-not $revision -or -not $checksum) { throw 'Caprica source identity is missing from its port.' }
$options = [IO.File]::ReadAllText((Join-Path $root '.tools/local.json')) | ConvertFrom-Json
$filename = "Orvid-Caprica-$revision.tar.gz"
$candidates = @((Join-Path $root ".tools/downloads/$filename"), (Join-Path $options.Vcpkg "downloads/$filename"))
$upstream = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $upstream) {
    $upstream = Join-Path $root "build/$filename"
    Invoke-WebRequest "https://github.com/Orvid/Caprica/archive/$revision.tar.gz" -OutFile $upstream
}
if ((Get-FileHash -LiteralPath $upstream -Algorithm SHA512).Hash -ine $checksum) { throw 'Caprica source checksum mismatch.' }
Add-File $source $upstream 'external/Caprica.tar.gz'

# Every ZIP is complete before replacing its prior published filename.
Write-Archive (Join-Path $dist "$baseName-source.zip") $source
Write-Archive (Join-Path $dist "$baseName.zip") $binary
$checksums = foreach ($name in @("$baseName.zip", "$baseName-source.zip")) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
[IO.File]::WriteAllLines((Join-Path $dist 'SHA256SUMS.txt'), $checksums)
Write-Host "Release archives: $dist"
