param([Parameter(Mandatory)] [string] $BuildDirectory, [Parameter(Mandatory)] [string] $Executable)
$ErrorActionPreference = 'Stop'
$repoDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$statusFile = Join-Path $BuildDirectory 'ui-status.txt'
$logFile = Join-Path $BuildDirectory 'hot-reload.log'
$hostFiles = @('Main.cpp', 'ReloadableUI.cpp', 'ReloadableUI.h', 'UIAPI.h', 'Session.h', '../../src/native/ui/Menu.h', '../../src/native/ui/History.h', '../../src/native/vm/VM.h', '../../src/native/shell/Requests.h', '../../src/native/shell/Inspection.h', '../../src/native/shell/ExpressionTypes.h', '../../src/native/compiler/ScriptDeclarations.h', '../../src/native/shell/Evaluation.h', 'CMakeLists.txt', 'vcpkg.json')

function Set-PreviewStatus([string] $message) {
    [IO.File]::WriteAllText($statusFile, $message, [Text.UTF8Encoding]::new($false))
}

function Get-PreviewSourceStamp {
    $files = @(
        Get-ChildItem -LiteralPath (Join-Path $repoDirectory 'src/native/ui') -Recurse -File
        Get-ChildItem -LiteralPath (Join-Path $repoDirectory 'src/native') -Recurse -File |
            Where-Object { $_.Extension -in @('.h', '.hpp') }
        Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File |
            Where-Object { $_.Extension -in @('.cpp', '.h', '.hpp', '.cmake', '.json') -or $_.Name -eq 'CMakeLists.txt' }
    )
    $theme = Select-String -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^PREVIEW_THEME_FILE:FILEPATH=(.+)$'
    if ($theme -and (Test-Path -LiteralPath $theme.Matches[0].Groups[1].Value)) {
        $files += Get-Item -LiteralPath $theme.Matches[0].Groups[1].Value
    }
    return (($files | Sort-Object FullName -Unique | ForEach-Object {
        "$($_.FullName)|$($_.Length)|$($_.LastWriteTimeUtc.Ticks)"
    }) -join "`n")
}

function Get-HostStamp {
    return (($hostFiles | ForEach-Object {
        $item = Get-Item -LiteralPath (Join-Path $PSScriptRoot $_)
        "$($item.Length)|$($item.LastWriteTimeUtc.Ticks)"
    }) -join '|')
}

Set-PreviewStatus 'Watching UI sources'
$sourceStamp = Get-PreviewSourceStamp
$hostStamp = Get-HostStamp
$previewProcess = Start-Process -FilePath $Executable -WorkingDirectory $BuildDirectory -PassThru -WindowStyle Normal
Write-Host 'UI hot reload enabled. Save menu sources to rebuild. Close the preview to stop.'
try {
    while (-not $previewProcess.HasExited) {
        Start-Sleep -Milliseconds 400
        $nextStamp = Get-PreviewSourceStamp
        if ($nextStamp -eq $sourceStamp) { continue }
        # Wait for editors to finish their save/rename sequence.
        Start-Sleep -Milliseconds 400
        $sourceStamp = Get-PreviewSourceStamp
        if ((Get-HostStamp) -ne $hostStamp) {
            Set-PreviewStatus 'Preview host changed; close the window and rerun build.ps1.'
            Write-Host 'Preview host changed; restart is required.'
            continue
        }
        Set-PreviewStatus 'Building UI; current menu remains active...'
        try {
            & cmake --build $BuildDirectory --target eldershell-preview-ui --parallel 2 *> $logFile
            $succeeded = $LASTEXITCODE -eq 0
        } catch {
            $_ | Out-String | Add-Content -LiteralPath $logFile
            $succeeded = $false
        }
        if ($succeeded) {
            Set-PreviewStatus 'Build succeeded; reloading UI...'
            Write-Host 'UI rebuilt.'
        } else {
            Set-PreviewStatus 'UI build failed; previous menu retained. See hot-reload.log.'
            Write-Host "UI build failed. Details: $logFile"
        }
    }
} finally {
    $previewProcess.Dispose()
}
