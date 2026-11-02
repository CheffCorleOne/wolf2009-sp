param(
    [string]$GameRoot = (Join-Path ${env:ProgramFiles(x86)} 'Activision\Wolfenstein'),
    [string]$Map = 'game/mtw/mtw_ware',
    [double]$Azimuth = 45,
    [double]$Elevation = 35
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if($Map -notmatch '^(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+$') { throw 'Use a relative map name, for example game/mtw/mtw_ware.' }
if(![double]::IsFinite($Azimuth) -or ![double]::IsFinite($Elevation)) { throw 'Camera angles must be finite.' }
$inputArchive = Join-Path $GameRoot ("SP\base\maps\" + $Map.Replace('/', '\') + '.mpk')
if(!(Test-Path -LiteralPath $inputArchive -PathType Leaf)) { throw "Map archive not found: $inputArchive" }
$sceneTool = Join-Path $projectRoot 'build\native\wolf2009-scene.exe'
$previewTool = Join-Path $projectRoot 'build\native\mesh-preview.exe'
if(!(Test-Path -LiteralPath $sceneTool) -or !(Test-Path -LiteralPath $previewTool)) {
    throw 'Build the tools first: tools/build-native.ps1 and tools/build-preview.ps1.'
}
$identifier = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$destination = Join-Path $projectRoot ("evidence\scene-previews\" + $identifier)
New-Item -ItemType Directory -Path $destination | Out-Null
$stem = $Map.Replace('/', '_')
$objPath = Join-Path $destination ($stem + '.obj')
$bmpPath = Join-Path $destination ($stem + '.bmp')
& $sceneTool $inputArchive $objPath
if($LASTEXITCODE -ne 0) { throw 'Scene export failed.' }
& $previewTool $objPath $bmpPath '--width' '1280' '--height' '720' '--azimuth' $Azimuth.ToString([Globalization.CultureInfo]::InvariantCulture) '--elevation' $Elevation.ToString([Globalization.CultureInfo]::InvariantCulture)
if($LASTEXITCODE -ne 0) { throw 'Geometry preview failed.' }
Write-Output "Static world preview: $bmpPath"
Write-Output 'Diagnostic flat shading: no textures, placed entities, animation or gameplay.'
