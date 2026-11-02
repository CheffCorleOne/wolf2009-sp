param(
    [string]$Zig = (Join-Path $env:USERPROFILE 'tools\zig-x86_64-windows-0.15.2\zig.exe')
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$neo = Join-Path $projectRoot 'engine\doom3\neo'
$buildRoot = Join-Path $projectRoot 'build\native'
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
foreach($part in 'anchor','idlib','sys') { New-Item -ItemType Directory -Force -Path (Join-Path $buildRoot "stubs\$part") | Out-Null }
# Allocator/platform shims let us test the actual archive translation unit
# without linking a renderer or creating fake headers in the engine checkout.
@'
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cerrno>
#include <ctime>
typedef unsigned char byte;
// This probe script targets Windows x64 (little endian), matching these shims.
inline short LittleShort(short x) { return x; }
inline int LittleLong(int x) { return x; }
inline void* Mem_Alloc(size_t n) { return malloc(n); }
inline void* Mem_ClearedAlloc(size_t n) { return calloc(1, n); }
inline void Mem_Free(void* p) { free(p); }
'@ | Set-Content -LiteralPath (Join-Path $buildRoot 'stubs\idlib\precompiled.h') -Encoding utf8
foreach($stub in 'sys\platform.h','idlib\Heap.h','idlib\Lib.h') {
    '#pragma once' | Set-Content -LiteralPath (Join-Path $buildRoot "stubs\$stub") -Encoding utf8
}
$common = @('-std=c++11','-O1','-I', $neo, '-I', (Join-Path $neo 'externlibs'), '-I', (Join-Path $buildRoot 'stubs'), '-I', (Join-Path $buildRoot 'stubs\anchor'))
foreach($backend in 'miniz','legacy') {
    $args = @('c++') + $common
    if($backend -eq 'miniz') { $args += '-D_MINIZ' }
    $args += @((Join-Path $PSScriptRoot 'native_zip_probe.cpp'), (Join-Path $neo 'framework\Unzip.cpp'), '-o', (Join-Path $buildRoot "zip-$backend.exe"))
    & $Zig @args
    if($LASTEXITCODE -ne 0) { throw "ZIP $backend build failed" }
}
$miniz = Join-Path $neo 'externlibs\miniz'
& $Zig cc '-O1' '-c' (Join-Path $miniz 'miniz.c') '-o' (Join-Path $buildRoot 'miniz.o')
if($LASTEXITCODE -ne 0) { throw 'miniz build failed' }
$sources = @('Archive.cpp','SelfTest.cpp','Inspect.cpp') | ForEach-Object { Join-Path $projectRoot "src\wolf2009\$_" }
& $Zig c++ '-std=c++11' '-O1' '-DWOLF2009_USE_MINIZ' '-I' $miniz @sources (Join-Path $buildRoot 'miniz.o') '-o' (Join-Path $buildRoot 'wolf2009-inspect.exe')
if($LASTEXITCODE -ne 0) { throw 'SPK/MPK loader build failed' }
& (Join-Path $buildRoot 'wolf2009-inspect.exe') '--self-test'
if($LASTEXITCODE -ne 0) { throw 'SPK/MPK self-tests failed' }
$mapSources = @('Archive.cpp','Entities.cpp','PlayerStart.cpp','StaticCollision.cpp','ConvexContact.cpp','MapPhysics.cpp')
$sceneSources = ($mapSources + @('Geometry.cpp','GeometryTest.cpp','PlayerStartTest.cpp','ModelPlacement.cpp','ModelPlacementTest.cpp','SceneExport.cpp')) | ForEach-Object { Join-Path $projectRoot "src\wolf2009\$_" }
& $Zig c++ '-std=c++11' '-O1' '-DWOLF2009_USE_MINIZ' '-I' $miniz @sceneSources (Join-Path $buildRoot 'miniz.o') '-o' (Join-Path $buildRoot 'wolf2009-scene.exe')
if($LASTEXITCODE -ne 0) { throw 'Static geometry decoder build failed' }
& (Join-Path $buildRoot 'wolf2009-scene.exe') '--self-test'
if($LASTEXITCODE -ne 0) { throw 'Static geometry self-tests failed' }
$walkSources = ($mapSources + @(
    'PlayerMove.cpp','StaticCollisionTest.cpp','PlayerMoveTest.cpp','LinearMover.cpp','LinearMoverTest.cpp',
    'MovingSupport.cpp','MovingSupportTest.cpp','ModelCollisionTest.cpp','ConvexContactTest.cpp','ModelConvexCollisionTest.cpp','ModelPrimitiveCollisionTest.cpp','BrainStartup.cpp','BrainStartupTest.cpp',
    'Geometry.cpp','ModelPlacement.cpp','SkinCatalog.cpp','WagonScene.cpp','WagonSceneTest.cpp',
    'TrainArrival.cpp','TrainArrivalTest.cpp','TrainDoorSignal.cpp','TrainDoorSignalTest.cpp','DoorAnimation.cpp','DoorModel.cpp','DoorModelTest.cpp',
    'ZipArchive.cpp','StreamMounts.cpp','DeclarationText.cpp','ScriptedVoice.cpp','GreeterAnimation.cpp','GreeterAnimationTest.cpp','TrainStopScene.cpp','Walk.cpp'
)) | ForEach-Object { Join-Path $projectRoot "src\wolf2009\$_" }
& $Zig c++ '-std=c++11' '-O1' '-DWOLF2009_USE_MINIZ' '-I' $miniz @walkSources (Join-Path $buildRoot 'miniz.o') '-o' (Join-Path $buildRoot 'wolf2009-walk.exe')
if($LASTEXITCODE -ne 0) { throw 'Native player movement build failed' }
& (Join-Path $buildRoot 'wolf2009-walk.exe') '--self-test'
if($LASTEXITCODE -ne 0) { throw 'Player movement or collision self-tests failed' }
$contentSources = @('Archive.cpp','StreamMounts.cpp','StreamMountsTest.cpp','Dds.cpp','Entities.cpp','Material.cpp','SkinCatalog.cpp','SkinCatalogTest.cpp','ZipArchive.cpp','DdsTest.cpp','EntitiesTest.cpp','MaterialTest.cpp','ZipTest.cpp','DeclarationText.cpp','ScriptedVoice.cpp','ScriptedVoiceTest.cpp','ContentTest.cpp') | ForEach-Object { Join-Path $projectRoot "src\wolf2009\$_" }
& $Zig c++ '-std=c++11' '-O1' '-DWOLF2009_USE_MINIZ' '-I' $miniz @contentSources (Join-Path $buildRoot 'miniz.o') '-o' (Join-Path $buildRoot 'wolf2009-content.exe')
if($LASTEXITCODE -ne 0) { throw 'Content decoder build failed' }
& (Join-Path $buildRoot 'wolf2009-content.exe') '--self-test'
if($LASTEXITCODE -ne 0) { throw 'Content decoder self-tests failed' }
$packSources = @('Archive.cpp','Geometry.cpp','ZipArchive.cpp','Dds.cpp','Material.cpp') | ForEach-Object { Join-Path $projectRoot "src\wolf2009\$_" }
& $Zig c++ '-std=c++11' '-O1' '-DWOLF2009_USE_MINIZ' '-I' $miniz '-I' (Join-Path $projectRoot 'src\wolf2009') @packSources (Join-Path $projectRoot 'tools\scene-pack.cpp') (Join-Path $buildRoot 'miniz.o') '-o' (Join-Path $buildRoot 'wolf2009-pack.exe')
if($LASTEXITCODE -ne 0) { throw 'Diagnostic scene pack build failed' }
Write-Output "Built native probes in $buildRoot"
