function Initialize-NativeEnvironment {
    # Initialize this process only; leave machine settings and file associations alone.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$installation) { throw 'Visual Studio C++ tools and a Windows SDK are required.' }
    $env:PATH = (($env:PATH -split ';' | Select-Object -Unique) -join ';')
    if ($env:__VSCMD_PREINIT_PATH) {
        $env:__VSCMD_PREINIT_PATH = (($env:__VSCMD_PREINIT_PATH -split ';' | Select-Object -Unique) -join ';')
    }
    $ready = $env:VSINSTALLDIR -and $env:VCToolsInstallDir -and
        $env:VSINSTALLDIR.TrimEnd('\') -ieq $installation.TrimEnd('\') -and
        $env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and $env:VSCMD_ARG_HOST_ARCH -eq 'x64' -and
        (Test-Path -LiteralPath (Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'))
    $developerPaths = @($env:PATH)
    if (!$ready) {
        $developerCommand = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
        $environment = & $env:ComSpec /d /c "call `"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
        if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the compiler environment.' }
        $developerPaths = @()
        foreach ($line in $environment) {
            if ($line -match '^([^=]+)=(.*)$') {
                if ($matches[1] -ieq 'PATH') { $developerPaths += $matches[2] }
                else { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
            }
        }
    }
    $cmakeBin = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
    $ninjaBin = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
    $developerPath = (($developerPaths -join ';') -split ';' | Select-Object -Unique) -join ';'
    $env:PATH = (("$cmakeBin;$ninjaBin;$developerPath" -split ';' | Select-Object -Unique) -join ';')
}
