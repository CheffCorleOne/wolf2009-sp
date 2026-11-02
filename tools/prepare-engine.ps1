$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$engine = Join-Path $projectRoot 'engine'
$upstream = 'https://github.com/glKarin/com.n0n3m4.diii4a.git'
$baseline = 'ce49510f3b0c04969eab8ebe8238416e84e4f0bb'
$patches = @('idtech4a-wolf2009-pk4.patch','idtech4a-wolf2009-streams.patch') |
    ForEach-Object { Join-Path $projectRoot "patches\$_" }
function RunGit([string[]]$Arguments) {
    & git @Arguments
    if($LASTEXITCODE -ne 0) { throw "git failed: $($Arguments -join ' ')" }
}
if(!(Test-Path -LiteralPath $engine)) {
    RunGit @('init', $engine)
    RunGit @('-C', $engine, 'remote', 'add', 'origin', $upstream)
    RunGit @('-C', $engine, '-c', 'http.version=HTTP/1.1', 'fetch', '--depth=1', '--filter=blob:none', 'origin', $baseline)
    RunGit @('-C', $engine, 'sparse-checkout', 'init', '--cone')
    RunGit @('-C', $engine, 'sparse-checkout', 'set', 'doom3/neo/framework', 'doom3/neo/externlibs/miniz', 'doom3/neo/externlibs/minizip')
    RunGit @('-C', $engine, 'checkout', '--detach', $baseline)
}
$actual = & git -C $engine rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $actual -ne $baseline) { throw 'Engine HEAD differs from the pinned baseline. Preserve your changes and use a separate checkout.' }
$pending = @()
foreach($patch in $patches) {
    if(!(Test-Path -LiteralPath $patch)) { throw "Missing project patch: $patch" }
    & git -C $engine apply --check $patch 2>$null
    if($LASTEXITCODE -eq 0) { $pending += $patch; continue }
    & git -C $engine apply --reverse --check $patch 2>$null
    if($LASTEXITCODE -ne 0) { throw "Patch conflicts with existing changes: $patch" }
}
if($pending.Count) { RunGit (@('-C', $engine, 'apply') + $pending) }
Write-Output 'Wolf2009 PK4 support and the opt-in SPK/MPK read bridge are present.'
Write-Output "Prepared archive-testing checkout: $engine"
Write-Output 'This sparse checkout is not a complete Android build. See docs/ROADMAP.md.'
