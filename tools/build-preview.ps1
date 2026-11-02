param([string]$Zig = (Join-Path $env:USERPROFILE 'tools\zig-x86_64-windows-0.15.2\zig.exe'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = Join-Path $projectRoot 'build\native'
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$binary = Join-Path $buildRoot 'mesh-preview.exe'
& $Zig c++ '-std=c++11' '-O2' '-Wall' '-Wextra' (Join-Path $PSScriptRoot 'mesh-preview.cpp') '-o' $binary
if($LASTEXITCODE -ne 0) { throw 'Native mesh-preview build failed' }
$preview = Join-Path $buildRoot 'mesh-preview-self-test.bmp'
if(Test-Path -LiteralPath $preview) { $preview = Join-Path $buildRoot ("mesh-preview-self-test-{0}.bmp" -f [Guid]::NewGuid().ToString('N')) }
& $binary '--self-test' $preview
if($LASTEXITCODE -ne 0) { throw 'Native mesh-preview self-tests failed' }
Write-Output "Built CPU geometry diagnostic: $binary"
