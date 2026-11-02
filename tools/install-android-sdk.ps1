param(
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools\android-sdk'),
    [string]$Node=(Join-Path $env:USERPROFILE 'tools\node-v24.22.2-win-x64\node.exe'),
    [switch]$Engine
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path $SdkRoot | Out-Null
$SdkRoot=(Resolve-Path -LiteralPath $SdkRoot).Path
$downloads=Join-Path $SdkRoot 'downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null
$metadataPath=Join-Path $downloads 'repository2-3.xml'
Invoke-WebRequest 'https://dl.google.com/android/repository/repository2-3.xml' -OutFile $metadataPath
[xml]$metadata=Get-Content -LiteralPath $metadataPath -Raw
$packages=$metadata.SelectNodes("//*[local-name()='remotePackage']")
$requests=@(
    @{Package='ndk;30.0.16248370';Destination='ndk\30.0.16248370';ArchiveRoot='android-ndk-r30'},
    @{Package='platforms;android-37.0';Destination='platforms\android-37';ArchiveRoot='android-37'},
    @{Package='build-tools;37.0.0';Destination='build-tools\37.0.0';ArchiveRoot='android-17'},
    @{Package='platform-tools';Destination='platform-tools';ArchiveRoot='platform-tools'}
)
if($Engine){$requests+=@{Package='cmake;3.22.1';Destination='cmake\3.22.1';FlatArchive=$true}}
$receipts=@()
foreach($request in $requests){
    $destination=Join-Path $SdkRoot $request.Destination
    if(Test-Path -LiteralPath (Join-Path $destination 'source.properties')){Write-Output "Installed: $($request.Package)";continue}
    $package=$packages|Where-Object path -EQ $request.Package|Select-Object -First 1
    if(!$package){throw "Pinned official SDK package is no longer listed: $($request.Package)"}
    $archive=$package.SelectNodes("*[local-name()='archives']/*[local-name()='archive']")|Where-Object{!$_.SelectSingleNode("*[local-name()='host-os']") -or $_.SelectSingleNode("*[local-name()='host-os']").InnerText -eq 'windows'}|Select-Object -First 1
    $complete=$archive.SelectSingleNode("*[local-name()='complete']")
    $filename=$complete.SelectSingleNode("*[local-name()='url']").InnerText
    if($filename -match '[/\\]' -or !$filename.EndsWith('.zip')){throw 'Unexpected SDK archive name'}
    $size=$complete.SelectSingleNode("*[local-name()='size']").InnerText
    $checksum=$complete.SelectSingleNode("*[local-name()='checksum']")
    $url='https://dl.google.com/android/repository/'+$filename
    $archivePath=Join-Path $downloads $filename
    & $Node (Join-Path $projectRoot 'android\download-sdk.mjs') $url $size $checksum.type $checksum.InnerText $archivePath
    if($LASTEXITCODE -ne 0){throw "SDK acquisition failed: $($request.Package)"}
    $staging=Join-Path $downloads ($request.Package.Replace(';','-')+'-extract')
    if(Test-Path -LiteralPath $staging){throw "Existing extraction directory retained: $staging"}
    New-Item -ItemType Directory -Path $staging | Out-Null
    & tar.exe -xf $archivePath -C $staging
    if($LASTEXITCODE -ne 0){throw 'SDK archive extraction failed'}
    if($request.FlatArchive){
        if(!(Test-Path -LiteralPath (Join-Path $staging 'bin\cmake.exe')) -or
           !(Test-Path -LiteralPath (Join-Path $staging 'bin\ninja.exe'))){throw 'Unexpected CMake package layout'}
        $source=(Resolve-Path -LiteralPath $staging).Path
    }else{
        $children=@(Get-ChildItem -LiteralPath $staging -Directory)
        if($children.Count -ne 1){throw 'Unexpected SDK archive layout'}
        $source=$children[0].FullName
    }
    if(!$source.StartsWith($downloads+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or !$destination.StartsWith($SdkRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'SDK install path escaped selected tools root'}
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    if(Test-Path -LiteralPath $destination){throw 'Refusing to replace an existing SDK directory'}
    Move-Item -LiteralPath $source -Destination $destination
    $receipts+=@{package=$request.Package;url=$url;publishedDigestType=$checksum.type;publishedDigest=$checksum.InnerText;sha256=(Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()}
    Write-Output "Installed verified package: $($request.Package)"
}
if($receipts.Count){
    $receiptPath=Join-Path $downloads 'installation-receipt.local.json'
    $prior=if(Test-Path -LiteralPath $receiptPath){Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json}
    $installed=@($receipts.package)
    $all=@($prior|Where-Object {$_ -and $_.package -notin $installed})+@($receipts)
    ConvertTo-Json -InputObject $all -Depth 4|Set-Content -LiteralPath $receiptPath -Encoding utf8
}
Write-Output "Android SDK/NDK ready at $SdkRoot"
