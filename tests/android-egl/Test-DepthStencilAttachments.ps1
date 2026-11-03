# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9._:-]+$')][string]$Serial,
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools/android-sdk')
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$adb=Join-Path $SdkRoot 'platform-tools/adb.exe'
$clang=Join-Path $SdkRoot 'ndk/30.0.16248370/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
$abi=(& $adb -s $Serial shell getprop ro.product.cpu.abi).Trim()
if($LASTEXITCODE){throw 'Cannot inspect explicitly selected Android target'}
$target=switch($abi){'x86_64'{'x86_64-linux-android26'}'arm64-v8a'{'aarch64-linux-android26'}default{throw "Unsupported ABI: $abi"}}
$nonce=[Guid]::NewGuid().ToString('N').Substring(0,12)
$build=Join-Path $project "build/android-egl/$nonce"
$out=Join-Path $project "evidence/android-egl/$nonce"
New-Item -ItemType Directory -Force -Path $build,$out | Out-Null
$binary=Join-Path $build 'depth-stencil-attachments'
& $clang "--target=$target" '-std=c++11' '-O1' '-fPIE' '-pie' '-static-libstdc++' `
    (Join-Path $PSScriptRoot 'DepthStencilAttachments.cpp') '-lEGL' '-lGLESv3' '-o' $binary
if($LASTEXITCODE){throw 'EGL probe compilation failed'}
$remote="/data/local/tmp/wolf2009-egl-$nonce"
if($remote -notmatch '^/data/local/tmp/wolf2009-egl-[a-f0-9]{12}$'){throw 'Unsafe probe path'}
try {
    & $adb -s $Serial push $binary $remote
    if($LASTEXITCODE){throw 'EGL probe push failed'}
    & $adb -s $Serial shell chmod 700 $remote
    if($LASTEXITCODE){throw 'EGL probe chmod failed'}
    $lines=& $adb -s $Serial shell $remote 2>&1
    $exit=$LASTEXITCODE
    [IO.File]::WriteAllText((Join-Path $out 'run.log'),($lines -join "`n"),[Text.UTF8Encoding]::new($false))
    $lines
    $passed=$exit -eq 0 -and [bool]($lines -match 'WOLF2009_EGL_ATTACHMENTS_QA_OK')
    @{abi=$abi;serial=$Serial;passed=$passed;sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'report.local.json') -Encoding utf8
    if(!$passed){throw "EGL attachment check failed; evidence: $out"}
    Write-Output "Asset-free EGL check passed. Evidence: $out"
} finally {
    & $adb -s $Serial shell rm -f -- $remote | Out-Null
}
