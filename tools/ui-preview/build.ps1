[CmdletBinding()]
param(
    [switch] $BuildOnly,
    [switch] $SmokeTest,
    [string] $FrameworkDirectory,
    [string] $FrameworkIni,
    [string] $Font
)
$ErrorActionPreference = 'Stop'
$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function Invoke-Tool([string] $program, [string[]] $arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed (exit $LASTEXITCODE)" }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools and a Windows SDK are required.' }
function Get-UniquePath([string] $value) {
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    ($value -split ';' | Where-Object { $_ -and $seen.Add($_.TrimEnd('\')) }) -join ';'
}
$env:PATH = Get-UniquePath $env:PATH
if ($env:__VSCMD_PREINIT_PATH) { $env:__VSCMD_PREINIT_PATH = Get-UniquePath $env:__VSCMD_PREINIT_PATH }
$compilerReady = $env:VSINSTALLDIR -and $env:VCToolsInstallDir -and
    $env:VSINSTALLDIR.TrimEnd('\') -ieq $vs.TrimEnd('\') -and
    $env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and $env:VSCMD_ARG_HOST_ARCH -eq 'x64' -and
    (Test-Path -LiteralPath (Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'))
if (-not $compilerReady) {
    $developerCommand = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
    $developerEnvironment = & $env:ComSpec /d /c "call `"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the compiler environment.' }
    foreach ($line in $developerEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
$cmakeBin = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$ninjaBin = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$env:PATH = Get-UniquePath "$cmakeBin;$ninjaBin;$env:PATH"
$configPath = Join-Path $RepoRoot '.tools/local.json'
$config = if (Test-Path -LiteralPath $configPath) { [IO.File]::ReadAllText($configPath) | ConvertFrom-Json } else { $null }
$previewVcpkg = if ($config -and $config.Vcpkg) { $config.Vcpkg } else { Join-Path $RepoRoot '.tools/vcpkg' }
$previewBuild = Join-Path $RepoRoot 'build/ui-preview'
$localConfig = Join-Path $RepoRoot '.tools/ui-preview.json'
if (Test-Path -LiteralPath $localConfig) {
    $local = [IO.File]::ReadAllText($localConfig) | ConvertFrom-Json
    $useLocalFramework = -not $FrameworkDirectory
    if (-not $FrameworkDirectory) { $FrameworkDirectory = $local.FrameworkDirectory }
    if (-not $FrameworkIni -and $useLocalFramework) { $FrameworkIni = $local.FrameworkIni }
    if (-not $Font -and $useLocalFramework) { $Font = $local.Font }
}
function Read-IniValue([string] $path, [string] $section, [string] $key, [string] $fallback) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return $fallback }
    $currentSection = ''
    foreach ($line in [IO.File]::ReadLines($path)) {
        if ($line -match '^\s*\[([^]]+)\]') { $currentSection = $matches[1] }
        elseif ($currentSection -ieq $section -and $line -match '^\s*([^;#=]+?)\s*=\s*([^;]*?)\s*$' -and $matches[1] -ieq $key) {
            return $matches[2]
        }
    }
    return $fallback
}
$themeFile = Join-Path $PSScriptRoot 'skyrimDefault.json'
$fontSize = '32'
if ($FrameworkDirectory) {
    if (-not $FrameworkIni) { $FrameworkIni = Join-Path $FrameworkDirectory 'SKSEMenuFramework.ini' }
    $themeName = Read-IniValue $FrameworkIni 'General' 'MenuStyle' 'skyrimDefault'
    $themeFile = Join-Path $FrameworkDirectory "SKSEMenuFrameworkThemes/$themeName.json"
    if (-not (Test-Path -LiteralPath $themeFile)) { throw "Framework theme not found: $themeFile" }
    $fontSize = Read-IniValue $FrameworkIni 'Fonts' 'FontSizeMedium' '32'
    if (-not $Font) {
        $fontName = Read-IniValue $FrameworkIni 'Fonts' 'PrimaryFont' 'MainFont.ttf'
        $Font = Join-Path $FrameworkDirectory "fonts/$fontName"
        if (-not (Test-Path -LiteralPath $Font)) { $Font = Join-Path $FrameworkDirectory 'fonts/SkyrimMenuFont.ttf' }
    }
}
if ($Font -and -not (Test-Path -LiteralPath $Font)) { throw "Preview font not found: $Font" }
Write-Host "Preview theme: $themeFile | Font size: $fontSize"
if ($Font) { Write-Host "Preview font: $Font" }
$vcpkgToolchain = Join-Path $previewVcpkg 'scripts/buildsystems/vcpkg.cmake'
Invoke-Tool cmake @('-S', $PSScriptRoot, '-B', $previewBuild, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain", '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md',
    "-DPREVIEW_THEME_FILE=$themeFile", "-DPREVIEW_FONT_FILE=$Font", "-DPREVIEW_FONT_SIZE=$fontSize")
Invoke-Tool cmake @('--build', $previewBuild, '--parallel', '2')
$previewExe = Join-Path $previewBuild 'eldershell-ui-preview-host.exe'
if ($SmokeTest) {
    $smokeProcess = Start-Process -FilePath $previewExe -ArgumentList '--smoke-test' -WindowStyle Hidden -Wait -PassThru
    if ($smokeProcess.ExitCode -ne 0) { throw "UI smoke test failed (exit $($smokeProcess.ExitCode))" }
    Write-Host 'UI smoke test passed: rendering, module reload, state retention and rejected replacement.'
}
elseif (-not $BuildOnly) { & "$PSScriptRoot/Watch.ps1" -BuildDirectory $previewBuild -Executable $previewExe }
