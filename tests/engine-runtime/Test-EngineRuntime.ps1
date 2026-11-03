# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9._:-]+$')][string]$Serial,
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools/android-sdk'),
    [string]$BuildDirectory,
    [string]$GameplayAssets,
    [string]$RendererGameRoot,
    [string]$RealEmptyGameRoot,
    [switch]$WithOffscreenChecks,
    [ValidatePattern('^[A-Za-z0-9_/-]*$')][string]$Map='',
    [switch]$KeepRemote
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if($WithOffscreenChecks -and !$RendererGameRoot){throw 'Offscreen checks require explicit owner-supplied RendererGameRoot'}
if($RealEmptyGameRoot){
    if($Map -and $Map -ne 'wolf2009/main/PROC/maps/game/empty/empty'){throw 'RealEmptyGameRoot selects the source-qualified original empty map'}
    $Map='wolf2009/main/PROC/maps/game/empty/empty'
    if(!$GameplayAssets){$GameplayAssets=Join-Path $project 'src/idtech4a_gameplay/assets/base'}
}
if(!$BuildDirectory){$BuildDirectory=Join-Path $project 'build/engine-runtime-android-x86_64'}
$adb=Join-Path $SdkRoot 'platform-tools/adb.exe'
foreach($name in @('libdoom3.so','libgame.so','runtime-runner')){
    if(!(Test-Path -LiteralPath (Join-Path $BuildDirectory $name))){throw "Missing $name; run Build-EngineRuntime.ps1 first"}
}
$abi=(& $adb -s $Serial shell getprop ro.product.cpu.abi).Trim()
if($LASTEXITCODE -or $abi -ne 'x86_64'){throw 'This headless check requires the explicitly selected Android x86_64 target'}
$nonce=[Guid]::NewGuid().ToString('N').Substring(0,12)
$out=Join-Path $project "evidence/engine-runtime/$nonce"
$data=Join-Path $out 'data'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$artifacts=@{}
foreach($name in @('libdoom3.so','libgame.so','runtime-runner')){
    $artifacts[$name]=(Get-FileHash -LiteralPath (Join-Path $BuildDirectory $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$manifest=@{
    scope='actual common Init/Frame/Shutdown with the GPL Doom3 game module; headless Android';
    gameApiVersion=8; map=$Map; rendererChecks=[bool]$RendererGameRoot;
    realEmptyChecks=[bool]$RealEmptyGameRoot;
    offscreenChecks=[bool]$WithOffscreenChecks;
    target=$Serial; sha256=$artifacts
}
[IO.File]::WriteAllText((Join-Path $out 'artifacts.json'),($manifest | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
$makeArgs=@((Join-Path $PSScriptRoot 'MakeFixture.mjs'),$data)
if($GameplayAssets){$makeArgs+=$GameplayAssets}
& node @makeArgs
if($LASTEXITCODE){throw 'Fixture generation failed'}
if($RealEmptyGameRoot){
    $emptyBase=$null
    foreach($candidate in @((Join-Path $RealEmptyGameRoot 'SP/base'),(Join-Path $RealEmptyGameRoot 'base'),$RealEmptyGameRoot)){
        if(Test-Path -LiteralPath (Join-Path $candidate 'maps/game/empty/empty.mpk')){$emptyBase=[IO.Path]::GetFullPath($candidate);break}
    }
    if(!$emptyBase){throw 'RealEmptyGameRoot must contain the owner-supplied SP/base directory'}
    $destination=Join-Path $data 'base/maps/game/empty/empty.mpk'
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $emptyBase 'maps/game/empty/empty.mpk') -Destination $destination
}
if($RendererGameRoot){
    $renderBase=$null
    foreach($candidate in @((Join-Path $RendererGameRoot 'SP/base'),(Join-Path $RendererGameRoot 'base'),$RendererGameRoot)){
        if(Test-Path -LiteralPath (Join-Path $candidate 'maps/game/empty/empty.mpk')){$renderBase=[IO.Path]::GetFullPath($candidate);break}
    }
    if(!$renderBase){throw 'RendererGameRoot must contain the owner-supplied SP/base directory'}
    $inputs=@('maps/game/empty/empty.mpk','maps/game/trainyard/trainyard.mpk','maps/game/mtw/mtw_ware.mpk','assets.pk4')
    foreach($input in $inputs){
        $destination=if($input -eq 'assets.pk4'){Join-Path $data 'base/fixtures/wolf-materials.bin'}else{Join-Path $data "base/$input"}
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $renderBase $input) -Destination $destination
    }
    [IO.File]::WriteAllText((Join-Path $data 'base/fixtures/render-checks.enabled'),'Explicit native renderer QA request.')
    if($WithOffscreenChecks){[IO.File]::WriteAllText((Join-Path $data 'base/fixtures/offscreen-checks.enabled'),'Explicit isolated EGL pbuffer QA request.')}
}
$remote="/data/local/tmp/wolf2009-runtime-$nonce"
if($remote -notmatch '^/data/local/tmp/wolf2009-runtime-[a-f0-9]{12}$'){throw 'Unsafe remote path'}
function Adb([string[]]$Arguments){
    & $adb -s $Serial @Arguments
    if($LASTEXITCODE){throw "adb step failed: $($Arguments[0])"}
}
try {
    Adb @('shell','mkdir','-p',$remote)
    Adb @('push',$data,"$remote/")
    foreach($name in @('libdoom3.so','libgame.so','runtime-runner')){Adb @('push',(Join-Path $BuildDirectory $name),"$remote/")}
    Adb @('shell','chmod','700',"$remote/runtime-runner")
    $run=@('shell',"$remote/runtime-runner","$remote/libdoom3.so","$remote/data","$remote/libgame.so")
    if($Map){$run+=$Map}
    $lines=& $adb -s $Serial @run 2>&1
    $exit=$LASTEXITCODE
    [IO.File]::WriteAllText((Join-Path $out 'run.log'),($lines -join "`n"),[Text.UTF8Encoding]::new($false))
    if($WithOffscreenChecks){
        $frames=Join-Path $out 'frames'
        New-Item -ItemType Directory -Path $frames -Force | Out-Null
        foreach($name in @('offscreen-empty.bmp','offscreen-empty-flat.bmp','offscreen-warehouse.bmp','offscreen-warehouse-flat.bmp','offscreen-warehouse-unmodulated.bmp',
            'offscreen-cull-control.bmp','offscreen-gui-control.bmp','offscreen-depth-less-control.bmp','offscreen-alpha-control.bmp')){
            & $adb -s $Serial shell test -f "$remote/data/base/fixtures/$name"
            if(!$LASTEXITCODE){Adb @('pull',"$remote/data/base/fixtures/$name",(Join-Path $frames $name))}
        }
    }
    $lines | Select-String 'WOLF2009_NATIVE_|WOLF2009_ACTUAL_|WOLF2009_ROUNDED_(MODEL |PLAYER |ROTATION |EXPECTED_FAILURES |COLLISION_LIFETIME)|WOLF2009_OFFSCREEN_(SCOPE|GL|MAP|FAIL|FBO_ROUNDTRIP)|RUNTIME_SCOPE|RUNTIME_MAP_MOUNT|RUNTIME_STATE|RUNTIME_GAME_TIME|RUNTIME_SHUTDOWN|RUNNER_RESULT|Sys_Error|ERROR'
    if($exit -or !($lines -match 'WOLF2009_ACTUAL_ENGINE_RUNTIME_QA_OK')){
        $lines | Select-Object -Last 35
        throw "Actual common/game lifecycle check failed (exit $exit); evidence: $out"
    }
    if($Map -eq 'wolf2009/runtime' -and
        (!($lines -match 'WOLF2009_ACTUAL_NATIVE_COLLISION_MESH_QA_OK') -or
         !($lines -match 'WOLF2009_NATIVE_COLLISION_PLAYER .*seam_crossed=1') -or
         !($lines -match 'WOLF2009_NATIVE_COLLISION_LIFETIME released=1'))){
        throw "Native triangle collision/player/lifetime checks were not completed; evidence: $out"
    }
    if($Map -eq 'wolf2009/runtime' -and
        (!($lines -match 'WOLF2009_NATIVE_ROUNDED_QUERIES_QA_OK') -or
         !($lines -match 'WOLF2009_ACTUAL_NATIVE_ROUNDED_COLLISION_QA_OK') -or
         !($lines -match 'WOLF2009_ROUNDED_COLLISION_LIFETIME released=1 native_invalid=1 sidecar_absent=1'))){
        throw "Native rounded collision/query/player/lifetime checks were not completed; evidence: $out"
    }
    Write-Output "Actual common/game lifecycle passed. Evidence: $out"
} finally {
    if(!$KeepRemote -and $remote -match '^/data/local/tmp/wolf2009-runtime-[a-f0-9]{12}$'){
        & $adb -s $Serial shell rm -rf -- $remote | Out-Null
    }
}
exit 0
