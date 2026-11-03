# SPDX-License-Identifier: GPL-3.0-or-later
# Package the actual GPL engine/game backend. Never invokes the legacy viewer build.
param(
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools/android-sdk'),
    [string]$JavaHome=(Join-Path $env:USERPROFILE 'tools/jdk-21.0.13+9'),
    [string]$Arm64BuildDirectory,
    [string]$X86BuildDirectory,
    [string]$KeyStore,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*\.apk$')]
    [string]$ApkName='wolf2009-sp-0.9.0-engine-dev.apk'
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$Arm64BuildDirectory){$Arm64BuildDirectory=Join-Path $project 'build/engine-runtime-android-arm64-v8a'}
if(!$X86BuildDirectory){$X86BuildDirectory=Join-Path $project 'build/engine-runtime-android-x86_64'}
if(!$KeyStore){$KeyStore=Join-Path $project 'build/android/debug.keystore'}
$outputRoot=Join-Path $project 'build/android-engine'
$work=Join-Path $outputRoot ('work-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
$buildTools=Join-Path $SdkRoot 'build-tools/37.0.0'
$platformJar=Join-Path $SdkRoot 'platforms/android-37/android.jar'
$ndk=Join-Path $SdkRoot 'ndk/30.0.16248370'
$llvm=Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin'
$cmake=Join-Path $SdkRoot 'cmake/3.22.1/bin/cmake.exe'
$ninja=Join-Path $SdkRoot 'cmake/3.22.1/bin/ninja.exe'
$verifier=Join-Path $project 'tools/lib/engine-apk.mjs'
$manifest=Join-Path $project 'android/engine/AndroidManifest.xml'
foreach($required in @($KeyStore,$platformJar,$cmake,$ninja,(Join-Path $llvm 'llvm-strip.exe'),
    (Join-Path $buildTools 'aapt2.exe'),(Join-Path $buildTools 'd8.bat'),(Join-Path $buildTools 'apksigner.bat'),
    (Join-Path $buildTools 'zipalign.exe'),(Join-Path $JavaHome 'bin/javac.exe'),$manifest)){
    if(!(Test-Path -LiteralPath $required -PathType Leaf)){throw "Missing build prerequisite: $required. The existing debug signing key is required; this builder never creates one."}
}
function Run([string]$Command,[string[]]$Arguments){
    & $Command @Arguments
    if($LASTEXITCODE -ne 0){throw "Build command failed ($LASTEXITCODE): $Command"}
}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function AssertUpToDate([string]$Directory){
    $dryRun=@(& $ninja -C $Directory -n doom3 game 2>&1)
    if($LASTEXITCODE -ne 0 -or !($dryRun -match '^ninja: no work to do\.$')){
        throw "Production core/game are stale in $Directory. Build with tests/engine-runtime/Build-EngineRuntime.ps1 -WithWolfRenderer -WithWolfGameplay and QA switches omitted. Ninja: $($dryRun -join [Environment]::NewLine)"
    }
}
# Validate the independent engine manifest before doing work or signing an artifact.
[xml]$xml=Get-Content -LiteralPath $manifest -Raw
$ns='http://schemas.android.com/apk/res/android'
if($xml.manifest.package -ne 'org.wolf2009.viewer' -or
    $xml.manifest.GetAttribute('versionCode',$ns) -ne '10' -or
    $xml.manifest.GetAttribute('versionName',$ns) -ne '0.9.0-engine-dev' -or
    $xml.manifest.application.GetAttribute('extractNativeLibs',$ns) -ne 'true' -or
    $xml.manifest.application.activity.GetAttribute('name',$ns) -ne 'org.wolf2009.engine.EngineActivity' -or
    $xml.manifest.application.activity.GetAttribute('process',$ns) -ne ':engine' -or
    $xml.SelectNodes('//*[starts-with(local-name(),"uses-permission")]').Count){
    throw 'Engine manifest package/version/activity/process/extraction/permission contract differs'
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
New-Item -ItemType Directory -Path $work | Out-Null
$oldJava=$env:JAVA_HOME
try{
    $env:JAVA_HOME=$JavaHome
    Run node @((Join-Path $project 'tools/check-engine-patch.mjs'))
    AssertUpToDate $Arm64BuildDirectory
    AssertUpToDate $X86BuildDirectory
    $inputsFile=Join-Path $work 'production-inputs.local.json'
    Run node @($verifier,'inputs',$project,$Arm64BuildDirectory,$X86BuildDirectory,$inputsFile)
    $inputs=Get-Content -LiteralPath $inputsFile -Raw | ConvertFrom-Json
    Run (Join-Path $project 'tools/prepare-q3e-host.ps1') @()
    $q3e=Join-Path $project 'build/q3e-host-src'
    $originals=@()
    foreach($build in $inputs.builds){
        $abi=$build.configuration.ANDROID_ABI
        $directory=Join-Path $work "lib/$abi"
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        foreach($library in $build.native){
            $destination=Join-Path $directory $library.library
            Copy-Item -LiteralPath $library.path -Destination $destination
            Run (Join-Path $llvm 'llvm-strip.exe') @('--strip-unneeded',$destination)
            $originals+=@{abi=$abi;library=$library.library;path=$library.path;sha256=$library.sha256}
        }
        $hostBuild=Join-Path $work "host-$abi"
        Run $cmake @('-S',(Join-Path $project 'android/engine/native'),'-B',$hostBuild,'-G','Ninja',
            "-DCMAKE_MAKE_PROGRAM=$ninja","-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake",
            "-DANDROID_ABI=$abi",'-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release',
            "-DWOLF2009_ENGINE_ROOT=$project/engine","-DWOLF2009_Q3E_HOST_SOURCE=$q3e")
        Run $cmake @('--build',$hostBuild,'--target','wolf2009-enginehost','--parallel','1')
        $hostLibrary=Join-Path $hostBuild 'libwolf2009-enginehost.so'
        $originals+=@{abi=$abi;library='libwolf2009-enginehost.so';path=$hostLibrary;sha256=(Hash $hostLibrary)}
        $copy=Join-Path $directory 'libwolf2009-enginehost.so'
        Copy-Item -LiteralPath $hostLibrary -Destination $copy
        Run (Join-Path $llvm 'llvm-strip.exe') @('--strip-unneeded',$copy)
    }
    # The generator requires a fresh base directory and supplies no original assets or maps.
    Run node @((Join-Path $project 'tools/prepare-engine-support.mjs'),(Join-Path $work 'assets/engine-support/base'))
    $licenses=Join-Path $work 'assets/licenses'
    New-Item -ItemType Directory -Force -Path $licenses | Out-Null
    $licenseFiles=@{
        'GPL-3.0.txt'=(Join-Path $project 'LICENSE')
        'idTech4A-GPL-3.0.txt'=(Join-Path $project 'engine/doom3/COPYING.txt')
        'OpenAL-LGPL-2.0.txt'=(Join-Path $project 'engine/doom3/neo/externlibs/openal-soft/COPYING')
        'Ogg.txt'=(Join-Path $project 'engine/doom3/neo/externlibs/libogg/COPYING')
        'Vorbis.txt'=(Join-Path $project 'engine/doom3/neo/externlibs/libvorbis/COPYING')
        'miniz.txt'=(Join-Path $project 'engine/doom3/neo/externlibs/miniz/LICENSE.txt')
        'JPEG.txt'=(Join-Path $project 'engine/doom3/neo/externlibs/jpeg-8d/README')
        'irrXML.txt'=(Join-Path $project 'engine/doom3/neo/externlibs/irrxml/readme.txt')
        'Android-NDK.txt'=(Join-Path $ndk 'NOTICE')
        'LLVM.txt'=(Join-Path $llvm '../NOTICE')
    }
    foreach($name in $licenseFiles.Keys){Copy-Item -LiteralPath $licenseFiles[$name] -Destination (Join-Path $licenses $name)}
    Copy-Item -LiteralPath (Join-Path $q3e 'source-manifest.local.json') -Destination (Join-Path $licenses 'Q3E-source-manifest.txt')
    $classes=Join-Path $work 'classes';$dex=Join-Path $work 'dex';$generated=Join-Path $work 'generated'
    New-Item -ItemType Directory -Force -Path $classes,$dex,$generated | Out-Null
    $compiledResources=Join-Path $work 'resources.zip'
    Run (Join-Path $buildTools 'aapt2.exe') @('compile','--dir',(Join-Path $project 'android/res'),'-o',$compiledResources)
    $resourcesApk=Join-Path $work 'resources.apk'
    Run (Join-Path $buildTools 'aapt2.exe') @('link','-I',$platformJar,'--manifest',$manifest,'--java',$generated,
        '--min-sdk-version','26','--target-sdk-version','37','-0','arsc','-o',$resourcesApk,$compiledResources)
    # Deliberately excludes android/java and android/native, the legacy viewer target.
    $sources=@(Get-ChildItem -LiteralPath (Join-Path $project 'android/engine/java'),$generated -Filter '*.java' -Recurse -File |
        Select-Object -ExpandProperty FullName)
    $bootClasspath=(Join-Path $buildTools 'core-lambda-stubs.jar')+[IO.Path]::PathSeparator+$platformJar
    Run (Join-Path $JavaHome 'bin/javac.exe') (@('-encoding','UTF-8','-source','8','-target','8','-Xlint:-options',
        '-bootclasspath',$bootClasspath,'-d',$classes)+$sources)
    $jar=Join-Path $work 'classes.jar'
    Run (Join-Path $JavaHome 'bin/jar.exe') @('--create','--file',$jar,'-C',$classes,'.')
    Run (Join-Path $buildTools 'd8.bat') @('--min-api','26','--lib',$platformJar,'--output',$dex,$jar)
    $unsigned=Join-Path $work 'engine-unsigned.apk'
    Copy-Item -LiteralPath $resourcesApk -Destination $unsigned
    Add-Type -AssemblyName System.IO.Compression
    $stream=[IO.File]::Open($unsigned,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite)
    $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Update)
    try{
        $entries=@(@{Name='classes.dex';Path=(Join-Path $dex 'classes.dex')})
        foreach($root in @('lib','assets')){
            foreach($file in Get-ChildItem -LiteralPath (Join-Path $work $root) -Recurse -File){
                $entries+=@{Name=$file.FullName.Substring($work.Length+1).Replace('\','/');Path=$file.FullName}
            }
        }
        foreach($item in $entries){
            if($zip.GetEntry($item.Name)){throw "Unexpected existing APK member: $($item.Name)"}
            $entry=$zip.CreateEntry($item.Name,[IO.Compression.CompressionLevel]::NoCompression)
            $destination=$entry.Open();$source=[IO.File]::OpenRead($item.Path)
            try{$source.CopyTo($destination)}finally{$source.Dispose();$destination.Dispose()}
        }
    }finally{$zip.Dispose();$stream.Dispose()}
    $aligned=Join-Path $work 'engine-aligned.apk'
    Run (Join-Path $buildTools 'zipalign.exe') @('-P','16','-f','4',$unsigned,$aligned)
    $signed=Join-Path $work $ApkName
    Run (Join-Path $buildTools 'apksigner.bat') @('sign','--ks',$KeyStore,'--ks-key-alias','androiddebugkey',
        '--ks-pass','pass:android','--key-pass','pass:android','--out',$signed,$aligned)
    $signature=@(& (Join-Path $buildTools 'apksigner.bat') verify --verbose --print-certs $signed)
    if($LASTEXITCODE -ne 0){throw 'APK signature verification failed'}
    $signerCertificates=@($signature | Select-String '^(?:Signer #1 |V[0-9.]+ Signer: )certificate SHA-256 digest: ([0-9a-fA-F]{64})$' |
        ForEach-Object {$_.Matches[0].Groups[1].Value.ToLowerInvariant()} | Select-Object -Unique)
    if($signerCertificates.Count -ne 1){throw 'Cannot identify a single verified signing certificate'}
    $signature | Set-Content -LiteralPath (Join-Path $work 'signature.txt') -Encoding utf8
    $signature | Write-Output
    Run (Join-Path $buildTools 'zipalign.exe') @('-c','-P','16','4',$signed)
    $permissions=@(& (Join-Path $buildTools 'aapt2.exe') dump permissions $signed)
    if($LASTEXITCODE -ne 0 -or ($permissions -match 'uses-permission')){throw 'Unexpected permission entered engine APK'}
    $badging=@(& (Join-Path $buildTools 'aapt2.exe') dump badging $signed)
    if($LASTEXITCODE -ne 0 -or !($badging -match "^package: name='org.wolf2009.viewer' versionCode='10' versionName='0.9.0-engine-dev'") -or
        !($badging -match "^launchable-activity: name='org.wolf2009.engine.EngineActivity'")){throw 'Compiled engine manifest differs'}
    $badging | Set-Content -LiteralPath (Join-Path $work 'badging.txt') -Encoding utf8
    $apkReport=Join-Path $work 'apk-verification.local.json'
    Run node @($verifier,'apk',$signed,$work,$apkReport)
    foreach($input in $originals){if((Hash $input.path) -ne $input.sha256){throw "Input library changed: $($input.path)"}}
    AssertUpToDate $Arm64BuildDirectory
    AssertUpToDate $X86BuildDirectory
    $sourceFile=Join-Path $work 'source.local.json'
    Run node @($verifier,'snapshot',$project,$sourceFile)
    $source=Get-Content -LiteralPath $sourceFile -Raw | ConvertFrom-Json
    if($source.sourceSha256 -ne $inputs.source.sourceSha256){throw 'Build source changed while packaging. Rebuild from a stable source snapshot.'}
    $apk=Join-Path $outputRoot $ApkName
    Copy-Item -LiteralPath $signed -Destination $apk
    $report=[ordered]@{
        builtAtUtc=[DateTime]::UtcNow.ToString('o');apk=$apk;workDirectory=$work
        verification=(Get-Content -LiteralPath $apkReport -Raw | ConvertFrom-Json)
        source=$source;productionInputs=$inputs.builds;unstrippedLibraries=$originals
        q3e=(Get-Content -LiteralPath (Join-Path $q3e 'source-manifest.local.json') -Raw | ConvertFrom-Json)
        minApi=26;targetApi=37;ndk='30.0.16248370';hostBuildJobs=1
        signatureVerified=$true;signerCertificateSha256=$signerCertificates[0]
        permissions=@();deviceRuntimeTested=$false;arm64RuntimeTested=$false
    }
    $report | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath ($apk+'.build-report.local.json') -Encoding utf8
    Write-Output "Engine APK: $apk"
    Write-Output "SHA256: $(Hash $apk)"
    Write-Output 'Verified both ABIs, ELF/ZIP 16 KiB alignment, signature and no game assets/permissions. Not installed or runtime-tested by this script.'
}finally{
    if($null -eq $oldJava){Remove-Item Env:JAVA_HOME -ErrorAction SilentlyContinue}else{$env:JAVA_HOME=$oldJava}
}
