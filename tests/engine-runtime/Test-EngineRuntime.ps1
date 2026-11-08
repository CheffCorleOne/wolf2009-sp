# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9._:-]+$')][string]$Serial,
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools/android-sdk'),
    [string]$BuildDirectory,
    [string]$GameplayAssets,
    [string]$RendererGameRoot,
    [string]$RealEmptyGameRoot,
    [switch]$WithOffscreenChecks,
    [switch]$WithBrainChecks,
    [switch]$WithSkinnedChecks,
    [switch]$WithAnimationChecks,
    [switch]$WithMeshV2Checks,
    [switch]$WithAnimationV2Checks,
    [switch]$WithSourceSessionChecks,
    [switch]$WithTrainyardChecks,
    [switch]$WithTrainyardArrivalChecks,
    [switch]$WithTrainyardActorChecks,
    [switch]$WithTrainyardDoorChecks,
    [switch]$WithAudioBankChecks,
    [string]$AudioBankGameRoot,
    [ValidatePattern('^[A-Za-z0-9_/-]*$')][string]$Map='',
    [switch]$KeepRemote
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if($WithTrainyardDoorChecks){
    if($WithTrainyardChecks -or $WithTrainyardArrivalChecks -or $WithTrainyardActorChecks){throw 'Door lifecycle checks use a separate original empty source session; run trainyard checks separately'}
    $WithSourceSessionChecks=$true
}
if($WithTrainyardActorChecks){$WithTrainyardArrivalChecks=$true}
if($WithTrainyardArrivalChecks){$WithTrainyardChecks=$true}
if($WithTrainyardChecks){
    $WithSourceSessionChecks=$true
    if(!$RendererGameRoot){$RendererGameRoot=$RealEmptyGameRoot}
    if(!$RendererGameRoot){throw 'Trainyard checks require explicit owner-supplied RendererGameRoot'}
    if($Map -and $Map -ne 'wolf2009/main/PROC/maps/game/trainyard/trainyard'){throw 'Trainyard checks select the original trainyard map'}
    $Map='wolf2009/main/PROC/maps/game/trainyard/trainyard'
}
if($WithSourceSessionChecks -and !$GameplayAssets){$GameplayAssets=Join-Path $project 'src/idtech4a_gameplay/assets/base'}
if($WithSourceSessionChecks -and $WithAnimationV2Checks){
    throw 'Run source-session and legacy animation-save QA separately: the legacy save record intentionally refuses the source player wide trace cache'
}
if($WithOffscreenChecks -and !$RendererGameRoot){throw 'Offscreen checks require explicit owner-supplied RendererGameRoot'}
if($WithSkinnedChecks -and !$RendererGameRoot){throw 'Skinned checks require explicit owner-supplied RendererGameRoot'}
if($WithAnimationChecks -and !$RendererGameRoot){throw 'Animation checks require explicit owner-supplied RendererGameRoot'}
if($WithMeshV2Checks -and !$RendererGameRoot){throw 'Mesh v2 checks require explicit owner-supplied RendererGameRoot'}
if($WithAnimationV2Checks -and !$RendererGameRoot){throw 'Animation v2 checks require explicit owner-supplied RendererGameRoot'}
if($WithSourceSessionChecks -and !$RendererGameRoot -and !$RealEmptyGameRoot){throw 'Source session checks require explicit owner-supplied RealEmptyGameRoot or RendererGameRoot'}
if($WithBrainChecks -or $WithAnimationChecks -or $WithAnimationV2Checks){
    if(!$GameplayAssets){$GameplayAssets=Join-Path $project 'src/idtech4a_gameplay/assets/base'}
    if(!$Map -and !$RealEmptyGameRoot -and !$WithSourceSessionChecks){$Map='wolf2009/runtime'}
}
if($RealEmptyGameRoot -and !$WithTrainyardChecks){
    if($Map -and $Map -ne 'wolf2009/main/PROC/maps/game/empty/empty'){throw 'RealEmptyGameRoot selects the source-qualified original empty map'}
    $Map='wolf2009/main/PROC/maps/game/empty/empty'
    if(!$GameplayAssets){$GameplayAssets=Join-Path $project 'src/idtech4a_gameplay/assets/base'}
}
if($WithSourceSessionChecks -and !$WithTrainyardChecks){
    if($Map -and $Map -ne 'wolf2009/main/PROC/maps/game/empty/empty'){throw 'Source session checks require the source-qualified original empty map'}
    $Map='wolf2009/main/PROC/maps/game/empty/empty'
    if(!$GameplayAssets){$GameplayAssets=Join-Path $project 'src/idtech4a_gameplay/assets/base'}
}
if($WithAudioBankChecks){
    if(!$AudioBankGameRoot){throw 'Audio bank checks require explicit owner-supplied AudioBankGameRoot'}
    if(!$GameplayAssets){$GameplayAssets=Join-Path $project 'src/idtech4a_gameplay/assets/base'}
    if(!$Map){$Map='wolf2009/runtime'}
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
    realEmptyChecks=[bool]($RealEmptyGameRoot -and !$WithTrainyardChecks);
    offscreenChecks=[bool]$WithOffscreenChecks;
    brainChecks=[bool]$WithBrainChecks; skinnedChecks=[bool]$WithSkinnedChecks;
    animationChecks=[bool]$WithAnimationChecks;
    meshV2Checks=[bool]$WithMeshV2Checks;
    animationV2Checks=[bool]$WithAnimationV2Checks;
    sourceSessionChecks=[bool]$WithSourceSessionChecks; trainyardChecks=[bool]$WithTrainyardChecks;
    trainyardArrivalChecks=[bool]$WithTrainyardArrivalChecks;
    trainyardActorChecks=[bool]$WithTrainyardActorChecks;
    trainyardDoorChecks=[bool]$WithTrainyardDoorChecks;
    audioBankChecks=[bool]$WithAudioBankChecks;
    skinnedGpuChecks=[bool]($WithSkinnedChecks -and $WithOffscreenChecks); target=$Serial; sha256=$artifacts
}
[IO.File]::WriteAllText((Join-Path $out 'artifacts.json'),($manifest | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
$makeArgs=@((Join-Path $PSScriptRoot 'MakeFixture.mjs'),$data)
if($GameplayAssets){$makeArgs+=$GameplayAssets}
& node @makeArgs
if($LASTEXITCODE){throw 'Fixture generation failed'}
if($WithBrainChecks){
    $brainFixtureManifest=& node (Join-Path $project 'tests/engine-brain/prepare-fixture.mjs') --output (Join-Path $data 'base')
    if($LASTEXITCODE){throw 'Brain fixture generation failed'}
    [IO.File]::WriteAllText((Join-Path $out 'brain-fixtures.json'),($brainFixtureManifest -join "`n"),[Text.UTF8Encoding]::new($false))
    New-Item -ItemType Directory -Path (Join-Path $data 'base/fixtures') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $data 'base/fixtures/brain-checks.enabled'),'Explicit own native BrainTask QA request.')
}
if($WithAnimationChecks){
    New-Item -ItemType Directory -Path (Join-Path $data 'base/fixtures') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $data 'base/fixtures/animation-checks.enabled'),'Explicit source-backed native animator QA request.')
}
if($WithAnimationV2Checks){
    New-Item -ItemType Directory -Path (Join-Path $data 'base/fixtures') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $data 'base/fixtures/animation-v2-checks.enabled'),'Explicit four-source mesh-v2/native animator QA request.')
}
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
    if($WithSkinnedChecks -or $WithAnimationChecks -or $WithMeshV2Checks -or $WithAnimationV2Checks){
        $inputs+=@('maps/game/global/global.spk','streampacks/allies_kreisau_agt02.spk')
    }
    if($WithAnimationV2Checks){$inputs+='maps/game/trainyard/trainyard_vo_english.spk'}
    foreach($input in $inputs){
        $destination=if($input -eq 'assets.pk4'){Join-Path $data 'base/fixtures/wolf-materials.bin'}else{Join-Path $data "base/$input"}
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $renderBase $input) -Destination $destination
    }
    [IO.File]::WriteAllText((Join-Path $data 'base/fixtures/render-checks.enabled'),'Explicit native renderer QA request.')
    if($WithOffscreenChecks){[IO.File]::WriteAllText((Join-Path $data 'base/fixtures/offscreen-checks.enabled'),'Explicit isolated EGL pbuffer QA request.')}
    if($WithSkinnedChecks){[IO.File]::WriteAllText((Join-Path $data 'base/fixtures/skinned-checks.enabled'),'Explicit source-backed native MD5 QA request.')}
    if($WithMeshV2Checks){[IO.File]::WriteAllText((Join-Path $data 'base/fixtures/mesh-v2-checks.enabled'),'Explicit geometry-only named SKEL/MD5 QA request.')}
}
if($WithSourceSessionChecks){
    & node (Join-Path $project 'tests/source-mover/prepare-fixture.mjs') --output (Join-Path $data 'base')
    if($LASTEXITCODE){throw 'Native source mover fixture generation failed'}
    $sessionBase=$null
    $candidates=@()
    if($RendererGameRoot){$candidates+=@((Join-Path $RendererGameRoot 'SP/base'),(Join-Path $RendererGameRoot 'base'),$RendererGameRoot)}
    if($RealEmptyGameRoot){$candidates+=@((Join-Path $RealEmptyGameRoot 'SP/base'),(Join-Path $RealEmptyGameRoot 'base'),$RealEmptyGameRoot)}
    foreach($candidate in $candidates){
        if(Test-Path -LiteralPath (Join-Path $candidate 'maps/game/empty/empty.mpk')){$sessionBase=[IO.Path]::GetFullPath($candidate);break}
    }
    if(!$sessionBase){throw 'Source session checks require the owner-supplied SP/base directory'}
    $sessionInputs=@(
        'maps/game/empty/empty.mpk',
        'maps/game/global/global.spk',
        'maps/game/trainyard/trainyard.mpk',
        'maps/game/trainyard/trainyard_vo_english.spk',
        'streampacks/mp40.spk',
        'assets.pk4'
    )
    foreach($input in $sessionInputs){
        $destination=if($input -eq 'assets.pk4'){Join-Path $data 'base/fixtures/wolf-materials.bin'}else{Join-Path $data "base/$input"}
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $sessionBase $input) -Destination $destination
    }
    New-Item -ItemType Directory -Path (Join-Path $data 'base/fixtures') -Force | Out-Null
    $sessionFlag=if($WithTrainyardChecks){'trainyard-session.enabled'}else{'source-session.enabled'}
    [IO.File]::WriteAllText((Join-Path $data "base/fixtures/$sessionFlag"),'Explicit source session player/map QA request.')
    if($WithTrainyardArrivalChecks){[IO.File]::WriteAllText((Join-Path $data 'base/fixtures/trainyard-arrival.enabled'),'Explicit source native arrival/director QA request.')}
    if($WithTrainyardActorChecks -or $WithTrainyardDoorChecks){
        $character='streampacks/allies_kreisau_agt02.spk'
        $destination=Join-Path $data "base/$character"
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $sessionBase $character) -Destination $destination
        $actorFlag=if($WithTrainyardDoorChecks){'source-animated-door.enabled'}else{'trainyard-actor.enabled'}
        [IO.File]::WriteAllText((Join-Path $data "base/fixtures/$actorFlag"),'Explicit isolated native actor/door lifecycle QA; not a campaign VO readiness acknowledgement.')
    }
}
if($WithAudioBankChecks){
    $bankBase=$null
    foreach($candidate in @((Join-Path $AudioBankGameRoot 'SP/base'),(Join-Path $AudioBankGameRoot 'base'),$AudioBankGameRoot)){
        if(Test-Path -LiteralPath (Join-Path $candidate 'streampacks/scripted_voice_kc1_trn_vo_english.spk')){$bankBase=[IO.Path]::GetFullPath($candidate);break}
    }
    if(!$bankBase){throw 'AudioBankGameRoot must contain the owner-supplied SP/base directory'}
    $bankInput=Join-Path $bankBase 'streampacks/scripted_voice_kc1_trn_vo_english.spk'
    $destination=Join-Path $data 'base/streampacks/scripted_voice_kc1_trn_vo_english.spk'
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $bankInput -Destination $destination
    $bankFixtures=& node (Join-Path $project 'tests/source-audio-bank/MakeNativeFixtures.mjs') $destination (Join-Path $data 'base')
    if($LASTEXITCODE){throw 'Audio bank fixture generation failed'}
    [IO.File]::WriteAllText((Join-Path $out 'audio-bank-fixtures.json'),($bankFixtures -join "`n"),[Text.UTF8Encoding]::new($false))
    New-Item -ItemType Directory -Path (Join-Path $data 'base/fixtures') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $data 'base/fixtures/audio-bank-checks.enabled'),'Explicit native whole-bank SNDS and type19 readiness QA; no playback claim.')
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
    # Windows PowerShell 5.1 turns redirected native stderr into terminating
    # errors under Stop; collect the runner output as plain text instead.
    $previousPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
    $lines=& $adb -s $Serial @run 2>&1 | ForEach-Object { "$_" }
    $exit=$LASTEXITCODE
    $ErrorActionPreference=$previousPreference
    [IO.File]::WriteAllText((Join-Path $out 'run.log'),($lines -join "`n"),[Text.UTF8Encoding]::new($false))
    if($WithOffscreenChecks){
        $frames=Join-Path $out 'frames'
        New-Item -ItemType Directory -Path $frames -Force | Out-Null
        foreach($name in @('offscreen-empty.bmp','offscreen-empty-flat.bmp','offscreen-warehouse.bmp','offscreen-warehouse-flat.bmp','offscreen-warehouse-unmodulated.bmp',
            'offscreen-cull-control.bmp','offscreen-gui-control.bmp','offscreen-depth-less-control.bmp','offscreen-alpha-control.bmp',
            'offscreen-skinned-start.bmp','offscreen-skinned-flat.bmp','offscreen-skinned-fractional.bmp',
            'offscreen-source-mp40-idle.bmp','offscreen-source-mp40-flat.bmp','offscreen-source-mp40-restored.bmp',
            'offscreen-source-mp40-fire.bmp','offscreen-source-mp40-reload.bmp',
            'offscreen-trainyard-player.bmp','offscreen-trainyard-without-wagon.bmp','offscreen-trainyard-restored.bmp')){
            & $adb -s $Serial shell test -f "$remote/data/base/fixtures/$name"
            if(!$LASTEXITCODE){Adb @('pull',"$remote/data/base/fixtures/$name",(Join-Path $frames $name))}
        }
    }
    $lines | Select-String 'WOLF2009_NATIVE_|WOLF2009_ACTUAL_|WOLF2009_AUDIO_BANK_|WOLF2009_SCRIPTED_VO_|WOLF2009_BRAIN_|WOLF2009_ANIMATION_|WOLF2009_MESH_V2_|WOLF2009_SOURCE_(SUPPORT_|ENTITY_|AF_|SESSION_)|WOLF2009_PLAYER_HULL_|WOLF2009_ROUNDED_(MODEL |PLAYER |ROTATION |EXPECTED_FAILURES |COLLISION_LIFETIME)|WOLF2009_OFFSCREEN_(SCOPE|GL|MAP|FAIL|FBO_ROUNDTRIP)|RUNTIME_SCOPE|RUNTIME_MAP_MOUNT|RUNTIME_STATE|RUNTIME_GAME_TIME|RUNTIME_SHUTDOWN|RUNNER_RESULT|Sys_Error|ERROR'
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
    if($WithBrainChecks -and
       (!($lines -match 'WOLF2009_BRAIN_QA passed=1 live_tasks=0 live_globals=0') -or
        !($lines -match 'WOLF2009_BRAIN_HOST_QA completed=1 same_game_module=1'))){
        throw "Native BrainTask game-module checks were not completed; evidence: $out"
    }
    if($WithSkinnedChecks -and !($lines -match 'WOLF2009_ACTUAL_ENGINE_SKINNED_QA_OK')){
        throw "Native skinned model checks were not completed; evidence: $out"
    }
    if($WithAnimationChecks -and
       (!($lines -match 'WOLF2009_ANIMATION_HOST_QA completed=1 same_game_module=1') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_ANIMATOR_QA_OK') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_VISUAL_QA_OK') -or
        !($lines -match 'WOLF2009_ACTUAL_ENGINE_ANIMATION_QA_OK') -or
        !($lines -match 'WOLF2009_ANIMATION_LIFETIME descriptors=0'))){
        throw "Native source animator checks were not completed; evidence: $out"
    }
    if($WithMeshV2Checks -and !($lines -match 'WOLF2009_NATIVE_SKINNED_MESH_V2_QA_OK')){
        throw "Native geometry-only mesh v2 checks were not completed; evidence: $out"
    }
    if($WithAnimationV2Checks -and
       (!($lines -match 'WOLF2009_ANIMATION_V2_HOST_QA completed=1 same_game_module=1') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_ANIMATOR_V2_QA_OK') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_CONTROLLED_QA_OK') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_DECLARATIONS_QA_OK') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_DECLARATION_SET_QA_OK .*retail_schedule_claim=0 ') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_DECLARATION_STARTUP_QA_OK .*observed_schedule=1 ') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_SOUND_QA_OK .*nondefault_samples=2 ') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_ACTOR_BINDING_QA_OK .*native_animator_cycles=1 ') -or
        !($lines -match 'WOLF2009_SOURCE_ENTITY_NATIVE_QA_OK .*native_item=1 ') -or
        !($lines -match 'WOLF2009_SOURCE_AF_NATIVE_QA_OK .*world_hinge=1 ') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_ACTOR_RUNTIME_QA_OK .*actual_source_class=rvAnimatedContainer ') -or
        !($lines -match 'WOLF2009_ACTUAL_ENGINE_ANIMATION_V2_QA_OK .*set_probes=2 ') -or
        !($lines -match 'WOLF2009_ACTUAL_ENGINE_ANIMATION_V2_QA_OK .*declaration_probes=1 ') -or
        !($lines -match 'WOLF2009_ACTUAL_ENGINE_ANIMATION_V2_QA_OK') -or
        !($lines -match 'WOLF2009_ANIMATION_LIFETIME descriptors=0'))){
        throw "Native source animator v2 checks were not completed; evidence: $out"
    }
    if($WithSkinnedChecks -and $WithOffscreenChecks -and !($lines -match 'WOLF2009_ACTUAL_ENGINE_SKINNED_GPU_QA_OK')){
        throw "Native skinned GPU checks were not completed; evidence: $out"
    }
    if($RealEmptyGameRoot -and !$WithTrainyardChecks -and
       (!($lines -match 'WOLF2009_SOURCE_SUPPORT_READY handle=0 native_vertices=0 native_polygons=0 native_brushes=0') -or
        !($lines -match 'WOLF2009_ACTUAL_SOURCE_SUPPORT_QA_OK') -or
        !($lines -match 'WOLF2009_SOURCE_SUPPORT_LIFETIME SourceLifetime=1 .*valid=1'))){
        throw "Source support collision/cache/lease/lifetime checks were not completed; evidence: $out"
    }
    if($WithSourceSessionChecks -and !$WithTrainyardChecks -and !$WithTrainyardDoorChecks -and
       (!($lines -match 'WOLF2009_SOURCE_SESSION_FIXTURE_READY') -or
        !($lines -match 'WOLF2009_PLAYER_HULL_NATIVE PASS') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_PLAYER_QA_OK') -or
        !($lines -match 'WOLF2009_NATIVE_SOURCE_WEAPON_QA_OK') -or
        !($lines -match 'WOLF2009_SOURCE_SESSION_GAME_QA_OK') -or
        !($lines -match 'WOLF2009_SOURCE_SESSION_TEARDOWN_QA_OK'))){
        throw "Native source session player/weapon/hull/teardown checks were not completed; evidence: $out"
    }
    $trainyardMarker=if($WithTrainyardArrivalChecks){'WOLF2009_TRAINYARD_ARRIVAL_QA_OK'}else{'WOLF2009_TRAINYARD_SESSION_QA_OK'}
    if($WithTrainyardChecks -and (!($lines -match $trainyardMarker) -or !($lines -match 'WOLF2009_SOURCE_SESSION_TEARDOWN_QA_OK'))){
        throw "Original trainyard/player/wagon/teardown checks were not completed; evidence: $out"
    }
    if($WithTrainyardActorChecks -and (!($lines -match 'WOLF2009_TRAINYARD_SCRIPTED_ACTOR_FIXTURE_QA_OK') -or !($lines -match 'WOLF2009_SOURCE_SCRIPTED_ACTOR_QA_OK'))){
        throw "Actual source scripted-actor lifecycle checks were not completed; evidence: $out"
    }
    if($WithTrainyardDoorChecks -and (!($lines -match 'WOLF2009_TRAINYARD_ANIMATED_DOOR_FIXTURE_QA_OK') -or !($lines -match 'WOLF2009_SOURCE_ANIMATED_DOOR_QA PASS') -or
        !($lines -match 'WOLF2009_SOURCE_ANIMATED_DOOR_STALE_QA PASS') -or !($lines -match 'WOLF2009_SOURCE_ANIMATED_DOOR_REPLACEMENT_QA PASS') -or
        !($lines -match 'WOLF2009_SOURCE_SESSION_TEARDOWN_QA_OK'))){
        throw "Actual source animated-door lifecycle checks were not completed; evidence: $out"
    }
    if($WithTrainyardChecks -and $WithOffscreenChecks -and !($lines -match 'WOLF2009_TRAINYARD_VIEW_GPU_QA_OK')){
        throw "Actual trainyard game-camera check was not completed; evidence: $out"
    }
    if($WithSourceSessionChecks -and !$WithTrainyardChecks -and $WithOffscreenChecks -and
       !($lines -match 'WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_GPU_QA_OK')){
        throw "Original first-person MP40 GPU check was not completed; evidence: $out"
    }
    if($WithAudioBankChecks -and
       (!($lines -match 'WOLF2009_AUDIO_BANK_ORIGINAL_OK .*members=19 receipts=19 ') -or
        !($lines -match 'WOLF2009_AUDIO_BANK_NATIVE_OK .*live=0 ') -or
        !($lines -match 'WOLF2009_SCRIPTED_VO_READINESS_OK ') -or
        !($lines -match 'WOLF2009_SCRIPTED_VO_HOST_QA completed=1 same_game_module=1 live_banks_after=0') -or
        !($lines -match 'WOLF2009_AUDIO_BANK_LIFETIME live_banks=0'))){
        throw "Native whole-bank SNDS / type19 readiness checks were not completed; evidence: $out"
    }
    Write-Output "Actual common/game lifecycle passed. Evidence: $out"
} finally {
    if(!$KeepRemote -and $remote -match '^/data/local/tmp/wolf2009-runtime-[a-f0-9]{12}$'){
        & $adb -s $Serial shell rm -rf -- $remote | Out-Null
    }
}
exit 0
