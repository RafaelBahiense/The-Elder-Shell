param(
    [ValidateSet('Build','Test','Analyze')][string]$Target = 'Build',
    [string]$CommonLib,
    [string]$Vcpkg,
    [string]$InstalledDependencies
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$identity = [IO.File]::ReadAllText((Join-Path $root 'mod.json')) | ConvertFrom-Json
$configPath = Join-Path $root '.tools/local.json'
$config = if (Test-Path -LiteralPath $configPath) { [IO.File]::ReadAllText($configPath) | ConvertFrom-Json } else { $null }
if (!$CommonLib) { $CommonLib = if ($config) { $config.CommonLib } else { Join-Path $root '.tools/CommonLibSSE-NG' } }
if (!$Vcpkg) { $Vcpkg = if ($config) { $config.Vcpkg } else { Join-Path $root '.tools/vcpkg' } }
if (!$InstalledDependencies -and $config) { $InstalledDependencies = $config.InstalledDependencies }
if (!$InstalledDependencies) { $InstalledDependencies = Join-Path $root '.tools/vcpkg_installed' }

function Run([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed (exit $LASTEXITCODE)" }
}

# Initialize only this process. No registry, system PATH or file associations.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio C++ tools and a Windows SDK.' }
$developerCommand = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$developerEnvironment = & $env:ComSpec /d /c "call `"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the compiler environment.' }
$developerPaths = @()
foreach ($line in $developerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        if ($matches[1] -ieq 'PATH') { $developerPaths += $matches[2] }
        else { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
$cmakeBin = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$ninjaBin = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$developerPath = (($developerPaths -join ';' ) -split ';' | Select-Object -Unique) -join ';'
$env:PATH = "$cmakeBin;$ninjaBin;$developerPath"

if (!(Test-Path -LiteralPath (Join-Path $CommonLib 'CMakeLists.txt')) -or
    !(Test-Path -LiteralPath (Join-Path $Vcpkg 'scripts/buildsystems/vcpkg.cmake'))) {
    throw 'Run setup.ps1 or supply -CommonLib and -Vcpkg paths.'
}
$CommonLib = [IO.Path]::GetFullPath($CommonLib)
$Vcpkg = [IO.Path]::GetFullPath($Vcpkg)
[IO.Directory]::CreateDirectory((Join-Path $root '.tools')) | Out-Null
@{ CommonLib=$CommonLib; Vcpkg=$Vcpkg; InstalledDependencies=$InstalledDependencies } |
    ConvertTo-Json | ForEach-Object { [IO.File]::WriteAllText($configPath, $_) }

$build = Join-Path $root 'build'
if (!(Test-Path -LiteralPath (Join-Path $InstalledDependencies 'x64-windows-static/share/Caprica/CapricaConfig.cmake'))) {
    throw 'Caprica library is not installed. Run ./setup.ps1 with your dependency paths first.'
}
$arguments = @('-S',$root,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_CXX_COMPILER=cl.exe', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg/scripts/buildsystems/vcpkg.cmake",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static', "-DVCPKG_OVERLAY_PORTS=$root/cmake/ports",
    "-DCOMMONLIB_SSE_FOLDER=$CommonLib")
$arguments += @("-DVCPKG_INSTALLED_DIR=$InstalledDependencies", '-DVCPKG_MANIFEST_INSTALL=OFF')
Run cmake $arguments
switch ($Target) {
    'Build' {
        Run cmake @('--build',$build,'--parallel','3')
        $package = [IO.Path]::GetFullPath((Join-Path $build 'package'))
        if ([IO.Path]::GetDirectoryName($package) -ne [IO.Path]::GetFullPath($build)) { throw 'Invalid package staging path.' }
        if (Test-Path -LiteralPath $package) {
            $items = @((Get-Item -LiteralPath $package)) + @(Get-ChildItem -LiteralPath $package -Recurse -Force)
            if ($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Package staging contains a link; refusing cleanup.' }
            Remove-Item -LiteralPath $package -Recurse -Force
        }
        $plugins = Join-Path $package 'SKSE/Plugins'
        [IO.Directory]::CreateDirectory($plugins) | Out-Null
        Copy-Item -LiteralPath (Join-Path $build ($identity.binaryName + '.dll')) -Destination $plugins
        $notices = Join-Path $package ('docs/' + $identity.binaryName)
        [IO.Directory]::CreateDirectory((Join-Path $notices 'licenses')) | Out-Null
        Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $notices
        foreach ($notice in (Get-ChildItem -LiteralPath (Join-Path $root 'licenses') -Filter '*.txt')) {
            Copy-Item -LiteralPath $notice.FullName -Destination (Join-Path $notices 'licenses')
        }
        Write-Output "Installable files: $build/package"
    }
    'Test' {
        Run cmake @('--build',$build,'--target','console_tests','compiler_tests','--parallel','3')
        Run ctest @('--test-dir',$build,'--output-on-failure')
    }
    'Analyze' { Run cmake @('--build',$build,'--target','native_analysis','--parallel','3') }
}
