$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$engine = Join-Path $projectRoot 'engine'
$upstream = 'https://github.com/glKarin/com.n0n3m4.diii4a.git'
$baseline = 'ce49510f3b0c04969eab8ebe8238416e84e4f0bb'
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
RunGit @('-C', $engine, 'sparse-checkout', 'add',
    'doom3/neo/renderer', 'doom3/neo/cm', 'doom3/neo/sound', 'doom3/neo/idlib', 'doom3/neo/sys/posix',
    'doom3/neo/sys/android', 'doom3/neo/game', 'doom3/neo/tools/compilers/aas')
# Shared files such as CMakeLists.txt are changed by multiple dependent patches.
# Validate the complete chain against git-show baseline files in a separate
# repository, then update only exact baseline/previously generated source states.
& node (Join-Path $PSScriptRoot 'check-engine-patch.mjs') --prepare
if($LASTEXITCODE -ne 0) { throw 'Unable to prepare the complete Wolf2009 engine patch chain' }
Write-Output "Prepared pinned engine checkout: $engine"
Write-Output 'PK4, streams, static renderer, map/gameplay bridge and runtime bootstrap patches are present.'
Write-Output 'Feature CMake options remain opt-in. See docs/ENGINE.md.'
