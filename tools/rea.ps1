param([Parameter(ValueFromRemainingArguments=$true)][string[]]$ReaArgs)
$toolRoot = Join-Path $env:USERPROFILE 'tools'
$node = Join-Path $toolRoot 'node-v24.22.2-win-x64\node.exe'
$entry = Join-Path $toolRoot 'rea\node_modules\rea-agents\scripts\rea.mjs'
$env:GHIDRA_INSTALL_DIR = Join-Path $toolRoot 'ghidra_12.1.4_PUBLIC'
$env:JAVA_HOME = Join-Path $toolRoot 'jdk-21.0.13+9'
$env:REA_ANALYSIS_PROVIDER = 'ghidra'
$env:REA_GHIDRA_STARTUP_TIMEOUT_MS = '900000'
& $node $entry @ReaArgs
exit $LASTEXITCODE
