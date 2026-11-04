# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7
param(
    [Parameter(Mandatory)][string]$GameRoot,
    [Parameter(Mandatory)][string]$SupportBase,
    [ValidateSet('emulator-5556')][string]$Serial='emulator-5556',
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools/android-sdk'),
    [switch]$WithTrainyard
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
function Send-Binary([string]$File,[string]$Phase){
    $info=[Diagnostics.ProcessStartInfo]::new($adb)
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardInput=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    # On Windows `adb shell -T` still treats host stdin as text: the first
    # binary 0x1A truncates input. exec-in selects the binary upload service.
    foreach($arg in @('-s',$Serial,'exec-in','run-as',$package,'tar','-xf','-')){$info.ArgumentList.Add($arg)}
    $process=[Diagnostics.Process]::Start($info)
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    $sourceStream=[IO.File]::OpenRead($File);$copyError=$null
    try{
        $copy=$sourceStream.CopyToAsync($process.StandardInput.BaseStream)
        if(!$copy.Wait(30000)){
            $process.Kill();throw 'Binary upload timed out after 30 seconds'
        }
        [void]$copy.GetAwaiter().GetResult()
    }catch{$copyError=$_.Exception.Message}
    finally{$sourceStream.Dispose();try{$process.StandardInput.Close()}catch{if(!$copyError){$copyError=$_.Exception.Message}}}
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit();if(!$copyError){$copyError='ADB upload completion timed out'}}
    # Even an EPIPE must not bypass child stderr/exit collection.
    $output=$stdout.GetAwaiter().GetResult();$errorText=$stderr.GetAwaiter().GetResult()
    @{phase=$Phase;exitCode=$process.ExitCode;copyError=$copyError;stdout=$output;stderr=$errorText;transport='adb exec-in'}|
        ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence "$Phase-transport.local.json") -Encoding utf8
    if($process.ExitCode -or $copyError){throw "Private $Phase upload failed (exit $($process.ExitCode)): $copyError $errorText $output"}
    $process.Dispose()
}
function Remote-Hash([string]$Relative){
    if($Relative -notmatch '^[A-Za-z0-9_./-]+$'){throw 'Unexpected private QA path'}
    $line=(Adb -Arguments @('shell','run-as',$package,'sha256sum',$Relative)|Out-String).Trim()
    if($line -notmatch '^([0-9a-fA-F]{64})\s+'){throw "Invalid remote checksum for $Relative"}
    return $Matches[1].ToLowerInvariant()
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
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'shared_prefs')|Out-Null
$sourceInputs=@('assets.pk4','maps/game/empty/empty.mpk')
if($WithTrainyard){
    $prepared=& node (Join-Path $project 'tools/prepare-source-session.mjs') --game-root $selected[0] --support-base $support --output $root
    if($LASTEXITCODE){throw 'Private trainyard source preparation failed'}
    [IO.File]::WriteAllText((Join-Path $evidence 'trainyard-preparation.local.json'),($prepared -join "`n"),[Text.UTF8Encoding]::new($false))
    $sourceInputs+=@('maps/game/trainyard/trainyard.mpk','maps/game/global/global.spk','maps/game/trainyard/trainyard_vo_english.spk','streampacks/mp40.spk')
}else{
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'base'),(Join-Path $root 'save')|Out-Null
    # Copy only the explicit children of the root-authored support directory.
    foreach($item in Get-ChildItem -LiteralPath $support){Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $root 'base') -Recurse}
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'base/fixtures'),(Join-Path $root 'base/maps/game/empty')|Out-Null
    Copy-Item -LiteralPath (Join-Path $selected[0] 'assets.pk4') -Destination (Join-Path $root 'base/fixtures/wolf-materials.bin')
    Copy-Item -LiteralPath (Join-Path $selected[0] 'maps/game/empty/empty.mpk') -Destination (Join-Path $root 'base/maps/game/empty/empty.mpk')
    [IO.File]::WriteAllText((Join-Path $root 'owner.ready'),"original-assets-and-empty`n",[Text.UTF8Encoding]::new($false))
}
$deviceRoot=(Adb -Arguments @('shell','run-as',$package,'pwd')|Out-String).Trim()
if($deviceRoot -notmatch '^/data/(user/[0-9]+|data)/org\.wolf2009\.viewer$'){throw 'Unexpected app-private run-as root'}
$sourcePath="$deviceRoot/$relative"
$prefs='<?xml version="1.0" encoding="utf-8" standalone="yes" ?><map><string name="sourceRoot">'+$sourcePath+'</string></map>'
$prefStage="shared_prefs/engine.xml.stage-$nonce"
[IO.File]::WriteAllText((Join-Path $stage $prefStage),$prefs,[Text.UTF8Encoding]::new($false))
$archive=Join-Path $evidence 'private-data.tar'
& tar -cf $archive -C $stage files
if($LASTEXITCODE){throw 'Unable to create own private QA data archive'}
# Stop only the explicitly named test APK before touching its preferences.
Adb -Arguments @('shell','am','force-stop',$package)|Out-Null
Send-Binary -File $archive -Phase 'data'
# exec-in intentionally discards remote stdout and may not surface a remote tar
# error. Verify every support/input/marker byte before changing the selection.
$verified=0
foreach($file in Get-ChildItem -LiteralPath (Join-Path $stage 'files') -Recurse -File){
    $relativeFile=[IO.Path]::GetRelativePath($stage,$file.FullName).Replace('\','/')
    $localHash=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()
    if((Remote-Hash -Relative $relativeFile) -ne $localHash){throw "Private payload checksum mismatch: $relativeFile"}
    $verified++
}
# Publish preferences in a second, independently verified transaction. A failed
# data upload leaves the prior ready sourceRoot and its files untouched.
$prefArchive=Join-Path $evidence 'preferences.tar'
& tar -cf $prefArchive -C $stage shared_prefs
if($LASTEXITCODE){throw 'Unable to create staged QA preference archive'}
Send-Binary -File $prefArchive -Phase 'preferences'
$prefHash=(Get-FileHash -LiteralPath (Join-Path $stage $prefStage)).Hash.ToLowerInvariant()
if((Remote-Hash -Relative $prefStage) -ne $prefHash){throw 'Staged preference checksum mismatch'}
Adb -Arguments @('shell','run-as',$package,'mv','-f',$prefStage,'shared_prefs/engine.xml')|Out-Null
if((Remote-Hash -Relative 'shared_prefs/engine.xml') -ne $prefHash){throw 'Published preference checksum mismatch'}
$report=@{serial=$Serial;package=$package;dataRoot=$sourcePath;trainyard=[bool]$WithTrainyard;sourceFiles=@(
    foreach($sourceInput in $sourceInputs){@{name=[IO.Path]::GetFileName($sourceInput);relativePath=$sourceInput;sha256=(Get-FileHash -LiteralPath (Join-Path $selected[0] $sourceInput)).Hash.ToLowerInvariant()}});verifiedPayloadFiles=$verified;transport='adb exec-in';selectionPublishedAfterVerification=$true;apkInstalledByThisScript=$false;appLaunched=$false}
$report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $evidence 'stage-report.local.json') -Encoding utf8
Write-Output "ENGINE_ANDROID_ORIGINAL_DATA_STAGED=$sourcePath"
Write-Output "EVIDENCE=$evidence"
