# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7
param(
    [Parameter(Mandatory)][string]$GameRoot,
    [Parameter(Mandatory)][string]$SupportBase,
    [ValidateSet('emulator-5556')][string]$Serial='emulator-5556',
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools/android-sdk')
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$adb=Join-Path $SdkRoot 'platform-tools/adb.exe'
$package='org.wolf2009.viewer'
function Adb([string[]]$Arguments){
    $result=& $adb -s $Serial @Arguments
    if($LASTEXITCODE){throw "adb failed: $($Arguments -join ' ')"}
    return $result
}
$game=[IO.Path]::GetFullPath($GameRoot)
$candidates=@($game,(Join-Path $game 'base'),(Join-Path $game 'SP/base'))
$selected=@($candidates|Where-Object{(Test-Path -LiteralPath (Join-Path $_ 'assets.pk4') -PathType Leaf)-and(Test-Path -LiteralPath (Join-Path $_ 'maps/game/empty/empty.mpk') -PathType Leaf)})
if($selected.Count -ne 1){throw 'Select the original SP/base containing assets.pk4 and maps/game/empty/empty.mpk'}
$support=[IO.Path]::GetFullPath($SupportBase)
if(!(Test-Path -LiteralPath (Join-Path $support 'def/wolf2009_runtime.def') -PathType Leaf)){throw 'Missing independently authored engine support'}
if(Test-Path -LiteralPath (Join-Path $support 'maps')){throw 'Engine APK support must not contain maps'}
$nonce=[Guid]::NewGuid().ToString()
$evidence=Join-Path $project "evidence/engine-android/$nonce"
$stage=Join-Path $evidence 'stage'
$sourceName="source-$nonce"
$relative="files/engine-runtime/$sourceName"
$root=Join-Path $stage $relative
New-Item -ItemType Directory -Force -Path (Join-Path $root 'base'),(Join-Path $root 'save'),(Join-Path $stage 'shared_prefs')|Out-Null
# Copy only the explicit children of the root-authored support directory.
foreach($item in Get-ChildItem -LiteralPath $support){Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $root 'base') -Recurse}
New-Item -ItemType Directory -Force -Path (Join-Path $root 'base/fixtures'),(Join-Path $root 'base/maps/game/empty')|Out-Null
Copy-Item -LiteralPath (Join-Path $selected[0] 'assets.pk4') -Destination (Join-Path $root 'base/fixtures/wolf-materials.bin')
Copy-Item -LiteralPath (Join-Path $selected[0] 'maps/game/empty/empty.mpk') -Destination (Join-Path $root 'base/maps/game/empty/empty.mpk')
[IO.File]::WriteAllText((Join-Path $root 'owner.ready'),"original-assets-and-empty`n",[Text.UTF8Encoding]::new($false))
$deviceRoot=(Adb -Arguments @('shell','run-as',$package,'pwd')|Out-String).Trim()
if($deviceRoot -notmatch '^/data/(user/[0-9]+|data)/org\.wolf2009\.viewer$'){throw 'Unexpected app-private run-as root'}
$sourcePath="$deviceRoot/$relative"
$prefs='<?xml version="1.0" encoding="utf-8" standalone="yes" ?><map><string name="sourceRoot">'+$sourcePath+'</string></map>'
[IO.File]::WriteAllText((Join-Path $stage 'shared_prefs/engine.xml'),$prefs,[Text.UTF8Encoding]::new($false))
$archive=Join-Path $evidence 'private-data.tar'
& tar -cf $archive -C $stage files shared_prefs
if($LASTEXITCODE){throw 'Unable to create own private QA data archive'}
# Stop only the explicitly named test APK before touching its preferences.
Adb -Arguments @('shell','am','force-stop',$package)|Out-Null
$info=[Diagnostics.ProcessStartInfo]::new($adb)
$info.UseShellExecute=$false;$info.CreateNoWindow=$true
$info.RedirectStandardInput=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
foreach($arg in @('-s',$Serial,'shell','-T','run-as',$package,'tar','-xf','-')){$info.ArgumentList.Add($arg)}
$process=[Diagnostics.Process]::Start($info)
$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
$input=[IO.File]::OpenRead($archive)
try{$input.CopyTo($process.StandardInput.BaseStream)}finally{$input.Dispose();$process.StandardInput.Close()}
$process.WaitForExit()
$output=$stdout.GetAwaiter().GetResult();$errorText=$stderr.GetAwaiter().GetResult()
if($process.ExitCode){throw "Private data staging failed: $errorText $output"}
$report=@{serial=$Serial;package=$package;dataRoot=$sourcePath;sourceFiles=@(
    @{name='assets.pk4';sha256=(Get-FileHash -LiteralPath (Join-Path $selected[0] 'assets.pk4')).Hash.ToLowerInvariant()},
    @{name='empty.mpk';sha256=(Get-FileHash -LiteralPath (Join-Path $selected[0] 'maps/game/empty/empty.mpk')).Hash.ToLowerInvariant()});apkInstalledByThisScript=$false;appLaunched=$false}
$report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $evidence 'stage-report.local.json') -Encoding utf8
Write-Output "ENGINE_ANDROID_ORIGINAL_DATA_STAGED=$sourcePath"
Write-Output "EVIDENCE=$evidence"
