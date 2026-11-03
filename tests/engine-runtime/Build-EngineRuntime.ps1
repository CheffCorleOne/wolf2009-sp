# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [ValidateSet('arm64-v8a','x86_64')][string]$Abi='x86_64',
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools/android-sdk'),
    [string]$BuildDirectory,
    [switch]$WithWolfRenderer,
    [switch]$WithWolfGameplay,
    [switch]$WithRenderChecks,
    [switch]$WithOffscreenChecks,
    [string[]]$Targets=@('doom3','game'),
    [ValidateRange(1,2)][int]$Jobs=2
)
$ErrorActionPreference='Stop'
if($WithRenderChecks -or $WithOffscreenChecks){$WithWolfRenderer=$true}
if($WithOffscreenChecks){$WithRenderChecks=$true}
function CMakeOnOff([bool]$Enabled){if($Enabled){'ON'}else{'OFF'}}
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$engine=Join-Path $project 'engine'
if(!$BuildDirectory){$BuildDirectory=Join-Path $project "build/engine-runtime-android-$Abi"}
$cmake=Join-Path $SdkRoot 'cmake/3.22.1/bin/cmake.exe'
$ninja=Join-Path $SdkRoot 'cmake/3.22.1/bin/ninja.exe'
$ndk=Join-Path $SdkRoot 'ndk/30.0.16248370'
$clang=Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
foreach($tool in @($cmake,$ninja,$clang)){if(!(Test-Path -LiteralPath $tool)){throw "Missing $tool"}}
& (Join-Path $project 'tools/prepare-engine.ps1')
if($LASTEXITCODE){throw 'Unable to prepare pinned engine'}
$dirs='cm','externlibs','framework','game','idlib','MayaImport','openal','renderer','sound','sys','tools/compilers','tools/guied','ui' |
    ForEach-Object {"doom3/neo/$_"}
& git -C $engine sparse-checkout add @dirs
if($LASTEXITCODE){throw 'Unable to populate GPL engine/game sources'}
$cmakeArgs=@('-S',(Join-Path $engine 'doom3/neo'),'-B',$BuildDirectory,'-G','Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninja","-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake",
    "-DANDROID_ABI=$Abi",'-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release',
    '-DBUILD_D3=ON','-DCORE=ON','-DBASE=ON','-DBUILD_D3_MOD=OFF','-DBUILD_Q4=OFF','-DBUILD_Q4_MOD=OFF',
    '-DBUILD_PREY=OFF','-DBUILD_ETQW=OFF','-DOPENAL=ON','-DOPENSLES=ON',
    '-DALSOFT_BACKEND_OBOE=OFF','-DALSOFT_REQUIRE_OBOE=OFF','-DIMGUI=OFF','-DMP3_SUPPORT=ON',
    '-DETC2_IMAGE=OFF','-DD3BFG_NEW_FONT=OFF','-DWIDE_CHARACTER_LANGUAGE=OFF',
    '-DD3_MOD_BOTS=OFF','-DD3_MOD_FULL_BODY_AWARENESS=OFF',
    '-DWOLF2009_STREAMS=ON',"-DWOLF2009_CORE_DIR=$project/src/wolf2009",
    '-DWOLF2009_BOOTSTRAP=ON',"-DWOLF2009_BOOT_DIR=$project/src/idtech4a_boot")
$cmakeArgs+=@("-DWOLF2009_RENDERER=$(CMakeOnOff $WithWolfRenderer)",
    "-DWOLF2009_GAMEPLAY=$(CMakeOnOff $WithWolfGameplay)",
    "-DWOLF2009_RUNTIME_RENDER_QA=$(CMakeOnOff $WithRenderChecks)",
    "-DWOLF2009_RUNTIME_GL_QA=$(CMakeOnOff $WithOffscreenChecks)")
if($WithWolfRenderer){$cmakeArgs+="-DWOLF2009_RENDER_DIR=$project/src/idtech4a_render"}
if($WithWolfGameplay){$cmakeArgs+="-DWOLF2009_GAMEPLAY_DIR=$project/src/idtech4a_gameplay"}
if($WithRenderChecks){$cmakeArgs+="-DWOLF2009_RUNTIME_RENDER_QA_SOURCE=$project/tests/engine-render/EngineRenderChecks.cpp"}
if($WithOffscreenChecks){$cmakeArgs+="-DWOLF2009_RUNTIME_GL_QA_SOURCE=$project/tests/engine-render/EngineOffscreenChecks.cpp"}
& $cmake @cmakeArgs
if($LASTEXITCODE){throw 'Runtime CMake configuration failed'}
& $cmake --build $BuildDirectory --target @Targets --parallel $Jobs
if($LASTEXITCODE){throw 'Runtime core/game build failed'}
$target=if($Abi -eq 'x86_64'){'x86_64-linux-android26'}else{'aarch64-linux-android26'}
$runnerArgs=@("--target=$target",'-std=c++11','-O2','-fPIE','-pie','-static-libstdc++',
    "-I$project/src/idtech4a_boot",(Join-Path $PSScriptRoot 'RuntimeRunner.cpp'),'-ldl',
    '-o',(Join-Path $BuildDirectory 'runtime-runner'))
& $clang @runnerArgs
if($LASTEXITCODE){throw 'Runtime runner build failed'}
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $BuildDirectory 'libdoom3.so'),
    (Join-Path $BuildDirectory 'libgame.so'),(Join-Path $BuildDirectory 'runtime-runner')
