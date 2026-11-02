param(
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools\android-sdk'),
    [string]$JavaHome=(Join-Path $env:USERPROFILE 'tools\jdk-21.0.13+9'),
    [string[]]$Abis=@('arm64-v8a','x86_64')
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$buildRoot=Join-Path $projectRoot 'build\android'
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$work=Join-Path $buildRoot ('work-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-ffff'))
New-Item -ItemType Directory -Path $work | Out-Null
$buildTools=Join-Path $SdkRoot 'build-tools\37.0.0'
$platformJar=Join-Path $SdkRoot 'platforms\android-37\android.jar'
$ndk=Join-Path $SdkRoot 'ndk\30.0.16248370'
$clangRoot=Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin'
foreach($required in @($platformJar,(Join-Path $buildTools 'aapt2.exe'),(Join-Path $clangRoot 'clang++.exe'),(Join-Path $JavaHome 'bin\javac.exe'))){if(!(Test-Path -LiteralPath $required)){throw "Missing Android build prerequisite: $required. Run tools/install-android-sdk.ps1."}}
function Run([string]$Command,[string[]]$Arguments){& $Command @Arguments;if($LASTEXITCODE -ne 0){throw "Build command failed ($LASTEXITCODE): $Command"}}
$oldJava=$env:JAVA_HOME
try{
    $env:JAVA_HOME=$JavaHome
    $native=@()
    foreach($abi in $Abis){
        $triple=switch($abi){'arm64-v8a'{'aarch64-linux-android26'}'x86_64'{'x86_64-linux-android26'}default{throw "Unsupported ABI: $abi"}}
        $directory=Join-Path $work "lib\$abi";New-Item -ItemType Directory -Force -Path $directory|Out-Null
        $output=Join-Path $directory 'libwolf2009-viewer.so'
        $compilerArguments=@("--target=$triple",'-std=c++11','-O2','-fPIC','-shared','-fvisibility=hidden','-static-libstdc++','-Wall','-Wextra',
            '-Wl,--build-id=sha1','-Wl,-z,max-page-size=16384','-Wl,-z,common-page-size=16384',
            '-I',(Join-Path $projectRoot 'src\wolf2009'),
            (Join-Path $projectRoot 'android\native\viewer.cpp'),(Join-Path $projectRoot 'src\wolf2009\Archive.cpp'),(Join-Path $projectRoot 'src\wolf2009\Geometry.cpp'),
            '-lGLESv3','-llog','-lz','-o',$output)
        Run (Join-Path $clangRoot 'clang++.exe') $compilerArguments
        Run (Join-Path $clangRoot 'llvm-strip.exe') @('--strip-unneeded',$output)
        $elf=& (Join-Path $clangRoot 'llvm-objdump.exe') -p $output
        if($LASTEXITCODE -ne 0){throw 'Cannot inspect native ELF'}
        $loads=@($elf|Select-String 'LOAD ')
        if(!$loads.Count -or @($loads|Where-Object{$_.Line -notmatch 'align 2\*\*14'}).Count){throw 'Native ELF is not 16 KiB aligned'}
        $native+=@{abi=$abi;path=$output;sha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()}
        Write-Output "Built $abi native GLES3/core library, 16 KiB LOAD alignment verified."
    }
    $classes=Join-Path $work 'classes';$dex=Join-Path $work 'dex';$generated=Join-Path $work 'generated'
    New-Item -ItemType Directory -Force -Path $classes,$dex,$generated|Out-Null
    $compiledResources=Join-Path $work 'resources.zip'
    Run (Join-Path $buildTools 'aapt2.exe') @('compile','--dir',(Join-Path $projectRoot 'android\res'),'-o',$compiledResources)
    $resourcesApk=Join-Path $work 'resources.apk'
    Run (Join-Path $buildTools 'aapt2.exe') @('link','-I',$platformJar,'--manifest',(Join-Path $projectRoot 'android\AndroidManifest.xml'),'--java',$generated,'--min-sdk-version','26','--target-sdk-version','37','-0','arsc','-o',$resourcesApk,$compiledResources)
    $sources=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'android\java'),$generated -Filter '*.java' -Recurse -File|Select-Object -ExpandProperty FullName)
    $bootClasspath=(Join-Path $buildTools 'core-lambda-stubs.jar')+[IO.Path]::PathSeparator+$platformJar
    Run (Join-Path $JavaHome 'bin\javac.exe') (@('-encoding','UTF-8','-source','8','-target','8','-Xlint:-options','-bootclasspath',$bootClasspath,'-d',$classes)+$sources)
    $jar=Join-Path $work 'classes.jar'
    Run (Join-Path $JavaHome 'bin\jar.exe') @('--create','--file',$jar,'-C',$classes,'.')
    Run (Join-Path $buildTools 'd8.bat') @('--min-api','26','--lib',$platformJar,'--output',$dex,$jar)
    $unsigned=Join-Path $work 'viewer-unsigned.apk';Copy-Item -LiteralPath $resourcesApk -Destination $unsigned
    Add-Type -AssemblyName System.IO.Compression
    $stream=[IO.File]::Open($unsigned,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite)
    $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Update)
    try{
        $entries=@(
            @{Name='classes.dex';Path=(Join-Path $dex 'classes.dex')},
            @{Name='assets/licenses/GPL-3.0.txt';Path=(Join-Path $projectRoot 'LICENSE')},
            @{Name='assets/licenses/Android-NDK.txt';Path=(Join-Path $ndk 'NOTICE')},
            @{Name='assets/licenses/LLVM.txt';Path=(Join-Path $clangRoot '..\NOTICE')}
        )
        foreach($library in $native){$entries+=@{Name="lib/$($library.abi)/libwolf2009-viewer.so";Path=$library.path}}
        foreach($item in $entries){
            if($zip.GetEntry($item.Name)){throw "Unexpected existing APK member: $($item.Name)"}
            $entry=$zip.CreateEntry($item.Name,[IO.Compression.CompressionLevel]::NoCompression)
            $destination=$entry.Open();$source=[IO.File]::OpenRead($item.Path)
            try{$source.CopyTo($destination)}finally{$source.Dispose();$destination.Dispose()}
        }
    }finally{$zip.Dispose();$stream.Dispose()}
    $aligned=Join-Path $work 'viewer-aligned.apk'
    Run (Join-Path $buildTools 'zipalign.exe') @('-P','16','-f','4',$unsigned,$aligned)
    Run (Join-Path $buildTools 'zipalign.exe') @('-c','-P','16','4',$aligned)
    $key=Join-Path $buildRoot 'debug.keystore'
    if(!(Test-Path -LiteralPath $key)){
        Run (Join-Path $JavaHome 'bin\keytool.exe') @('-genkeypair','-keystore',$key,'-storepass','android','-keypass','android','-alias','androiddebugkey','-dname','CN=Android Debug,O=Android,C=US','-keyalg','RSA','-keysize','2048','-validity','10000')
    }
    $apk=Join-Path $buildRoot 'wolf2009-geometry-debug.apk'
    Run (Join-Path $buildTools 'apksigner.bat') @('sign','--ks',$key,'--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',$apk,$aligned)
    Run (Join-Path $buildTools 'apksigner.bat') @('verify','--verbose','--print-certs',$apk)
    Run (Join-Path $buildTools 'zipalign.exe') @('-c','-P','16','4',$apk)
    $permissions=& (Join-Path $buildTools 'aapt2.exe') dump permissions $apk
    if($LASTEXITCODE -ne 0 -or @($permissions|Select-String 'uses-permission').Count){throw 'Unexpected storage/network permission entered APK'}
    @{builtAtUtc=[DateTime]::UtcNow.ToString('o');apk=$apk;sha256=(Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant();abis=$Abis;minApi=26;targetApi=37;ndk='30.0.16248370';native=$native;signatureVerified=$true;zip16KiBAligned=$true;gameAssetsIncluded=$false;runtimeTested=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $buildRoot 'build-report.local.json') -Encoding utf8
    Write-Output "Android diagnostic viewer APK: $apk"
    Write-Output 'Build/signature/alignment verified. Device runtime validation is a separate step.'
}finally{if($null -eq $oldJava){Remove-Item Env:JAVA_HOME -ErrorAction SilentlyContinue}else{$env:JAVA_HOME=$oldJava}}
