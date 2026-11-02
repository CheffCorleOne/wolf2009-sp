# SPDX-License-Identifier: GPL-3.0-or-later
# Windows/PowerShell7 smoke test. No renderer/game bootstrap or app installation.
param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9._:-]+$')][string]$Serial,
    [string]$ProjectRoot,
    [string]$BuildDirectory,
    [string]$SdkRoot=(Join-Path $env:USERPROFILE 'tools/android-sdk'),
    [switch]$KeepRemote
)
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=$PSScriptRoot;while(!(Test-Path -LiteralPath (Join-Path $ProjectRoot 'src/wolf2009/Archive.cpp'))){$parent=Split-Path $ProjectRoot -Parent;if(!$parent -or $parent -eq $ProjectRoot){throw 'Pass -ProjectRoot for the engine repository'};$ProjectRoot=$parent}}
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
if(!$BuildDirectory){$BuildDirectory=Join-Path $ProjectRoot 'build/engine-android-x86_64'}
$BuildDirectory=[IO.Path]::GetFullPath($BuildDirectory)
$base=$null
foreach($candidate in @((Join-Path $GameRoot 'SP/base'),(Join-Path $GameRoot 'base'),$GameRoot)){
    if(Test-Path -LiteralPath (Join-Path $candidate 'maps/game/trainyard/trainyard.mpk')){$base=[IO.Path]::GetFullPath($candidate);break}
}
if(!$base){throw 'GameRoot must contain the installed game, SP, or SP/base directory'}
$userFiles=@('maps/game/trainyard/trainyard.mpk','maps/game/trainyard/trainyard_pak01.spk','streampacks/train_engine.spk')
foreach($file in $userFiles){if(!(Test-Path -LiteralPath (Join-Path $base $file))){throw "Missing user-owned input: $file"}}
$ndk=Join-Path $SdkRoot 'ndk/30.0.16248370/toolchains/llvm/prebuilt/windows-x86_64/bin'
$ninja=Join-Path $SdkRoot 'cmake/3.22.1/bin/ninja.exe'
$adb=Join-Path $SdkRoot 'platform-tools/adb.exe'
foreach($tool in @($ninja,$adb,(Join-Path $ndk 'clang++.exe'))){if(!(Test-Path -LiteralPath $tool)){throw "Missing tool: $tool"}}
if(!(Test-Path -LiteralPath (Join-Path $BuildDirectory 'libdoom3.so'))){throw 'First run tools/build-engine-android.ps1 -Abi x86_64'}
$nonce=[Guid]::NewGuid().ToString('N').Substring(0,12)
$output=Join-Path $ProjectRoot ('evidence/engine-entry-probe/'+$nonce)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
function Capture([string]$Binary,[string[]]$ArgList,[string]$Log){$lines=& $Binary @ArgList 2>&1;$result=$LASTEXITCODE;[IO.File]::WriteAllText($Log,($lines -join "`n"),$utf8);if($result -ne 0){Get-Content -LiteralPath $Log -Tail 20;throw "Probe step failed: $Binary"}}
Capture $ninja @('-C',$BuildDirectory,'-t','compdb','-x','CXX_COMPILER__doom3_Release') (Join-Path $output 'compile.json')
Capture $ninja @('-C',$BuildDirectory,'-t','compdb','-x','CXX_SHARED_LIBRARY_LINKER__doom3_Release') (Join-Path $output 'link.json')
$builder=@'
import {mkdirSync, readFileSync, writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {deflateSync} from 'node:zlib';
const [outArg, sourceArg, buildArg, ndkArg] = process.argv.slice(2),
                                    out = resolve(outArg).replaceAll('\\', '/'),
                                    source = resolve(sourceArg).replaceAll('\\', '/'),
                                    build = resolve(buildArg), ndk = resolve(ndkArg),
                                    digest = p => createHash('sha256').update(readFileSync(p)).digest('hex'),
                                    original = resolve(build, 'libdoom3.so'), before = digest(original);
function argv(s) {
    const a = [];
    let i = 0;
    while (i < s.length) {
        while (i < s.length && /\s/.test(s[i]))
            i++;
        if (i === s.length)
            break;
        let v = '', q = false;
        while (i < s.length && (q || !/\s/.test(s[i]))) {
            let n = 0;
            while (s[i] === '\\') {
                n++;
                i++;
            }
            if (s[i] === '"') {
                v += '\\'.repeat(Math.floor(n / 2));
                if (n % 2)
                    v += '"';
                else if (q && s[i + 1] === '"') {
                    v += '"';
                    i++;
                } else
                    q = !q;
                i++;
            } else {
                v += '\\'.repeat(n);
                if (i < s.length && (q || !/\s/.test(s[i])))
                    v += s[i++];
            }
        }
        if (q)
            throw Error('unmatched build-command quotes');
        a.push(v);
    }
    return a;
}
function run(args, cwd, log) {
    const binary = args.shift(),
          r = spawnSync(binary, args, {cwd, encoding : 'utf8', maxBuffer : 32 * 1024 * 1024});
    writeFileSync(out + '/' + log, (r.stdout ?? '') + (r.stderr ?? ''));
    if (r.status !== 0)
        throw Error('compile/link failed: ' + log);
}
const compile = JSON.parse(readFileSync(out + '/compile.json', 'utf8'))
                    .find(e => /framework[/\\]FileSystem\.cpp$/.test(e.file));
if (!compile || !compile.command.includes('-DWOLF2009_STREAMS') ||
    !compile.command.includes('--target=x86_64'))
    throw Error('build must be x86_64 with the actual patched Doom3 stream entry enabled');
let a = argv(compile.command);
const b = [];
for (let i = 0; i < a.length; i++) {
    if (a[i] === '-MD')
        continue;
    if (a[i] === '-MT' || a[i] === '-MF') {
        i++;
        continue;
    }
    b.push(a[i]);
}
b[b.indexOf('-o') + 1] = out + '/probe.o';
b[b.indexOf('-c') + 1] = source;
run(b, compile.directory, 'compile.log');
const link = JSON.parse(readFileSync(out + '/link.json', 'utf8')).find(e => e.output === 'libdoom3.so'),
      body = /^cmd\.exe \/C "cd \. && (.*) && cd \."$/s.exec(link?.command ?? '');
if (!body)
    throw Error('unsupported Ninja link rule');
a = argv(body[1]);
let o = a.indexOf('-o');
if (o < 0 || a[o + 1] !== 'libdoom3.so')
    throw Error('unexpected engine output');
a[o + 1] = out + '/libdoom3-probe.so';
a.splice(o + 2, 0, out + '/probe.o');
a = a.map(s => s === '-Wl,-soname,libdoom3.so' ? '-Wl,-soname,libdoom3-probe.so' : s);
run(a, link.directory, 'link.log');
run(
    [
        resolve(ndk, 'clang++.exe'), '--target=x86_64-linux-android26', '-std=c++11', '-O2', '-fPIE', '-pie',
        '-static-libstdc++', '-DWOLF2009_PROBE_RUNNER', source, '-ldl', '-o', out + '/probe-runner'
    ],
    out, 'runner-build.log');
if (digest(original) !== before)
    throw Error('production artifact changed');
function crc(data) {
    let c = 0xffffffff;
    for (const byte of data) {
        c ^= byte;
        for (let i = 0; i < 8; i++)
            c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
    }
    return (c ^ 0xffffffff) >>> 0;
}
const u =
          n => {
              let b = Buffer.alloc(4);
              b.writeUInt32LE(n);
              return b
          },
      pad = b => Buffer.concat([ b, Buffer.alloc((-b.length) & 3) ]),
      value = Buffer.from('nested SPK through actual idFile_InZip\n'),
      name = Buffer.from('synthetic/value\0'),
      plain =
          Buffer.concat([ pad(Buffer.concat([ u(1), name, u(value.length), u(value.length) ])), pad(value) ]),
      packed = deflateSync(plain),
      spk = Buffer.concat([ u(300), Buffer.from('TSET'), u(plain.length), u(packed.length), packed ]);
function zip(files, wolf) {
    const local = [], central = [];
    let offset = 0;
    for (const [n, d] of files) {
        const name = Buffer.from(n), h = Buffer.alloc(30), c = Buffer.alloc(46);
        h.writeUInt32LE(0x04034b50);
        h.writeUInt16LE(20, 4);
        h.writeUInt32LE(crc(d), 14);
        h.writeUInt32LE(d.length, 18);
        h.writeUInt32LE(d.length, 22);
        h.writeUInt16LE(name.length, 26);
        c.writeUInt32LE(0x02014b50);
        c.writeUInt16LE(20, 4);
        c.writeUInt16LE(20, 6);
        c.writeUInt32LE(crc(d), 16);
        c.writeUInt32LE(d.length, 20);
        c.writeUInt32LE(d.length, 24);
        c.writeUInt16LE(name.length, 28);
        c.writeUInt32LE(offset, 42);
        local.push(h, name, d);
        central.push(c, name);
        offset += 30 + name.length + d.length;
    }
    const cd = Buffer.concat(central), end = Buffer.alloc(22);
    end.writeUInt32LE(wolf ? 0x48530a02 : 0x06054b50);
    end.writeUInt16LE(files.length, 8);
    end.writeUInt16LE(files.length, 10);
    end.writeUInt32LE(cd.length, 12);
    end.writeUInt32LE(offset, 16);
    return Buffer.concat([...local, cd, end ]);
}
const base = out + '/data/base';
mkdirSync(base, {recursive : true});
writeFileSync(base + '/default.cfg', '// Synthetic filesystem-only test configuration.\n');
writeFileSync(base + '/ordinary.txt', 'ordinary engine loose file\n');
writeFileSync(
    base + '/fixture-standard.pk4',
    zip(
        [
            [ 'standard-probe.txt', Buffer.from('ordinary standard ZIP\n') ], [ 'fixtures/standard.spk', spk ]
        ],
        false));
writeFileSync(
    base + '/fixture-wolf.pk4',
    zip([ [ 'wolf-probe.txt', Buffer.from('custom footer ZIP\n') ], [ 'fixtures/wolf.spk', spk ] ], true));
writeFileSync(out + '/artifacts.json', JSON.stringify({
    productionSHA256 : before,
    probeSHA256 : digest(out + '/libdoom3-probe.so'),
    runnerSHA256 : digest(out + '/probe-runner'),
    scope : 'actual engine filesystem initialization only; no renderer or game module/boot'
},
                                                      null, 2));
'@
[IO.File]::WriteAllText((Join-Path $output 'build.mjs'),$builder,$utf8)
Capture 'node' @((Join-Path $output 'build.mjs'),$output,(Join-Path $PSScriptRoot 'EngineFilesystemProbe.cpp'),$BuildDirectory,$ndk) (Join-Path $output 'build.log')
$abi=(& $adb -s $Serial shell getprop ro.product.cpu.abi).Trim()
if($LASTEXITCODE -ne 0 -or $abi -ne 'x86_64'){throw 'This smoke probe requires the selected Android x86_64 target'}
$remote='/data/local/tmp/wolf2009-engine-probe-'+$nonce
if($remote -notmatch '^/data/local/tmp/wolf2009-engine-probe-[a-f0-9]{12}$'){throw 'Invalid temporary target'}
try {
    Capture $adb @('-s',$Serial,'shell','mkdir','-p',"$remote/data/base/maps/game/trainyard","$remote/data/base/streampacks") (Join-Path $output 'mkdir.log')
    Capture $adb @('-s',$Serial,'push',(Join-Path $output 'data'),"$remote/") (Join-Path $output 'push-fixtures.log')
    Capture $adb @('-s',$Serial,'push',(Join-Path $output 'probe-runner'),(Join-Path $output 'libdoom3-probe.so'),"$remote/") (Join-Path $output 'push-tools.log')
    foreach($file in $userFiles){$parent=($file -replace '/[^/]+$','');Capture $adb @('-s',$Serial,'push',(Join-Path $base $file),"$remote/data/base/$parent/") (Join-Path $output ('push-'+[IO.Path]::GetFileName($file)+'.log'))}
    Capture $adb @('-s',$Serial,'shell','chmod','700',"$remote/probe-runner") (Join-Path $output 'chmod.log')
    Capture $adb @('-s',$Serial,'shell',"$remote/probe-runner","$remote/libdoom3-probe.so","$remote/data") (Join-Path $output 'run.log')
    Get-Content -LiteralPath (Join-Path $output 'run.log') | Select-String -Pattern 'QA_SCOPE|QA_READ|QA_ENGINE_STATE|FILESYSTEM_QA_OK|QA_RUNNER_RESULT'
    if(!(Select-String -LiteralPath (Join-Path $output 'run.log') -Pattern 'WOLF2009_ACTUAL_ENGINE_FILESYSTEM_QA_OK' -Quiet)){throw 'Actual engine success marker missing'}
    Write-Output "Filesystem-only probe passed. No renderer/game initialization or app install. Evidence: $output"
} finally {
    if(!$KeepRemote -and $remote -match '^/data/local/tmp/wolf2009-engine-probe-[a-f0-9]{12}$'){
        & $adb -s $Serial shell rm -rf -- $remote | Out-Null
    }
}
