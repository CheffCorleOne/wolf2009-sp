param(
    [string]$SdkRoot = (Join-Path $env:USERPROFILE 'tools\android-sdk'),
    [ValidateSet('arm64-v8a','x86_64')][string]$Abi = 'arm64-v8a',
    [ValidateRange(1,16)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$engine = Join-Path $project 'engine'
$source = Join-Path $engine 'doom3/neo'
$build = Join-Path $project "build/engine-android-$Abi"
$cmake = Join-Path $SdkRoot 'cmake/3.22.1/bin/cmake.exe'
$ninja = Join-Path $SdkRoot 'cmake/3.22.1/bin/ninja.exe'
$toolchain = Join-Path $SdkRoot 'ndk/30.0.16248370/build/cmake/android.toolchain.cmake'
foreach($file in $cmake,$ninja,$toolchain){
    if(!(Test-Path -LiteralPath $file)){throw "Missing $file. Run tools/install-android-sdk.ps1 -Engine."}
}
& (Join-Path $PSScriptRoot 'prepare-engine.ps1')
$directories = 'cm','externlibs','framework','game','idlib','openal','renderer','sound','sys',
               'tools/compilers','tools/guied','ui' | ForEach-Object { "doom3/neo/$_" }
& git -C $engine sparse-checkout add @directories
if($LASTEXITCODE){throw 'Unable to populate the idTech4A++ engine sources'}
$core = (Resolve-Path (Join-Path $project 'src/wolf2009')).Path
$arguments = @('-S',$source,'-B',$build,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain","-DANDROID_ABI=$Abi",'-DANDROID_PLATFORM=android-26',
    '-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release','-DBUILD_D3=ON','-DCORE=ON',
    '-DBASE=OFF','-DBUILD_D3_MOD=OFF','-DBUILD_Q4=OFF','-DBUILD_Q4_MOD=OFF',
    '-DBUILD_PREY=OFF','-DBUILD_ETQW=OFF','-DOPENAL=ON','-DOPENSLES=ON',
    '-DALSOFT_BACKEND_OBOE=OFF','-DALSOFT_REQUIRE_OBOE=OFF','-DIMGUI=OFF','-DMP3_SUPPORT=ON',
    '-DETC2_IMAGE=OFF','-DD3BFG_NEW_FONT=OFF','-DWIDE_CHARACTER_LANGUAGE=OFF',
    '-DWOLF2009_STREAMS=ON',"-DWOLF2009_CORE_DIR=$core")
& $cmake @arguments
if($LASTEXITCODE){throw 'Doom3 core configuration failed'}
& $cmake --build $build --target doom3 --parallel $Jobs
if($LASTEXITCODE){throw 'Doom3 core compilation failed'}
Get-FileHash -LiteralPath (Join-Path $build 'libdoom3.so') -Algorithm SHA256
