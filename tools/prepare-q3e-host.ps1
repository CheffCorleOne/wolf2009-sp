# SPDX-License-Identifier: GPL-3.0-or-later
param([string]$EngineRoot, [string]$OutputDirectory)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$EngineRoot){$EngineRoot=Join-Path $project 'engine'}
if(!$OutputDirectory){$OutputDirectory=Join-Path $project 'build/q3e-host-src'}
# Node preserves blob bytes; PowerShell text pipelines would change line endings.
$extract=@'
const fs=require('node:fs'), path=require('node:path'), cp=require('node:child_process'), crypto=require('node:crypto');
const engine=path.resolve(process.argv[1]), out=path.resolve(process.argv[2]);
const commit='ce49510f3b0c04969eab8ebe8238416e84e4f0bb';
const files={
 'q3eeventqueue.cpp':'683034b92723cb308666cf9cd1b9d1b3da191e06edadd91e54467a23e18fd417',
 'q3eeventqueue.h':'8519d5a82c17da42f499478e4872977982f28c7e724dfd01734bfad5749c6db7',
 'q3eenv.c':'24dcc33a613eb93663a44ecec22dfee91835ce804f8535162d512efd7fbe9918',
 'q3eenv.h':'a50ce6edb5aeb0cb25cfdd82dbe52ec3764670495f77858010d3e36066bb0ca1',
 'q3estd.h':'513bc7fc7a8a9c1d9063b6ac32dc51a315b2086dc24d22a37b5f3734c6033e0d'
};
const git=(args)=>cp.execFileSync('git',['-C',engine,...args],{maxBuffer:8*1024*1024});
if(git(['rev-parse','HEAD']).toString().trim()!==commit)throw Error('Q3E checkout differs from pinned engine');
const hash=(b)=>crypto.createHash('sha256').update(b).digest('hex');
const blobs=Object.entries(files).map(([name,sha256])=>{
 const source='Q3E/src/main/jni/q3e/'+name;
 const bytes=git(['show',commit+':'+source]);
 if(hash(bytes)!==sha256)throw Error('Unexpected pinned Q3E blob: '+source);
 const target=path.join(out,name);
 if(fs.existsSync(target)&&hash(fs.readFileSync(target))!==sha256)throw Error('Refusing changed host source: '+target);
 return {name,source,sha256,bytes};
});
fs.mkdirSync(out,{recursive:true});
for(const b of blobs)if(!fs.existsSync(path.join(out,b.name)))fs.writeFileSync(path.join(out,b.name),b.bytes,{flag:'wx'});
const manifest={commit,license:'Upstream Q3E GPL-2.0-or-later; preserve upstream notices',files:blobs.map(({name,source,sha256})=>({name,source,sha256}))};
fs.writeFileSync(path.join(out,'source-manifest.local.json'),JSON.stringify(manifest,null,2)+'\n');
console.log('Q3E_HOST_SOURCES_VERIFIED='+out);
'@
& node -e $extract $EngineRoot $OutputDirectory
if($LASTEXITCODE){throw 'Unable to prepare pinned Q3E host sources'}
