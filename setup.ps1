param([string]$CommonLib, [string]$Vcpkg, [string]$InstalledDependencies)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$localTools = Join-Path $root '.tools'
[IO.Directory]::CreateDirectory($localTools) | Out-Null

function Run([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed (exit $LASTEXITCODE)" }
}

if (!$CommonLib) {
    $CommonLib = Join-Path $localTools 'CommonLibSSE-NG'
    $revision = 'b93280e832f263dbef44e44cbe2936622a02f91a'
    $marker = Join-Path $CommonLib '.source-revision'
    if (Test-Path -LiteralPath $CommonLib) {
        if (!(Test-Path -LiteralPath $marker) -or [IO.File]::ReadAllText($marker).Trim() -ne $revision) {
            throw 'Unrecognized CommonLib directory. Supply -CommonLib explicitly.'
        }
    } else {
        $archive = Join-Path $localTools 'commonlib.tar.gz'
        Invoke-WebRequest "https://codeload.github.com/CharmedBaryon/CommonLibSSE-NG/tar.gz/$revision" -OutFile $archive
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine 'e5377bf64e7c4fc2cf5cca642cd24212a2f940ea705ea0a75075273aa85a67f5') {
            throw 'CommonLib archive hash mismatch.'
        }
        Run tar @('-xf',$archive,'-C',$localTools)
        $extracted = [IO.Path]::GetFullPath((Join-Path $localTools "CommonLibSSE-NG-$revision"))
        $destination = [IO.Path]::GetFullPath($CommonLib)
        $boundary = [IO.Path]::GetFullPath($localTools) + [IO.Path]::DirectorySeparatorChar
        if (!$extracted.StartsWith($boundary) -or !$destination.StartsWith($boundary)) { throw 'Invalid dependency paths.' }
        Move-Item -LiteralPath $extracted -Destination $destination
        [IO.File]::WriteAllText($marker,$revision)
    }
}
if (!$Vcpkg) {
    $Vcpkg = Join-Path $localTools 'vcpkg'
    if (!(Test-Path -LiteralPath $Vcpkg)) {
        Run git @('clone','https://github.com/microsoft/vcpkg.git',$Vcpkg)
        Run git @('-C',$Vcpkg,'checkout','--detach','da2be01c400dd3ed102c1198752ef44c76aabe37')
    }
    if (!(Test-Path -LiteralPath (Join-Path $Vcpkg 'vcpkg.exe'))) {
        Run (Join-Path $Vcpkg 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }
}
if (!(Test-Path -LiteralPath (Join-Path $CommonLib 'CMakeLists.txt')) -or
    !(Test-Path -LiteralPath (Join-Path $Vcpkg 'vcpkg.exe'))) { throw 'Dependency paths are incomplete.' }
if (!$InstalledDependencies) { $InstalledDependencies = Join-Path $localTools 'vcpkg_installed' }
$InstalledDependencies = [IO.Path]::GetFullPath($InstalledDependencies)
$binaryCache = Join-Path $localTools 'vcpkg-cache'
$downloads = Join-Path $localTools 'downloads'
Run (Join-Path $Vcpkg 'vcpkg.exe') @('install',"--x-manifest-root=$root", "--downloads-root=$downloads",
    "--x-install-root=$InstalledDependencies",'--triplet=x64-windows-static','--host-triplet=x64-windows',
    "--overlay-ports=$root/cmake/ports",'--binarysource=clear',"--binarysource=files,$binaryCache,readwrite")
@{ CommonLib=[IO.Path]::GetFullPath($CommonLib); Vcpkg=[IO.Path]::GetFullPath($Vcpkg); InstalledDependencies=$InstalledDependencies } |
    ConvertTo-Json | ForEach-Object { [IO.File]::WriteAllText((Join-Path $localTools 'local.json'), $_) }
Write-Output 'Project dependencies configured. Run ./build.ps1.'
