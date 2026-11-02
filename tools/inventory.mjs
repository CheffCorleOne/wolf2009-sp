#!/usr/bin/env node
import { readdirSync, statSync, openSync, readSync, closeSync, readFileSync, mkdirSync, writeFileSync, realpathSync, existsSync } from 'node:fs';
import { resolve, relative, extname, join, dirname, basename, sep, isAbsolute } from 'node:path';
import { createHash } from 'node:crypto';
import { scanStreamPackage, sampleStreamPackage, zipDirectory, zipMember } from './lib/packages.mjs';

const [rootArg, outputArg] = process.argv.slice(2);
if (!rootArg || !outputArg) { console.error('Usage: node tools/inventory.mjs GAME_ROOT OUTPUT_DIRECTORY'); process.exit(2); }
const root = realpathSync(resolve(rootArg)), output = resolve(outputArg);
function canonicalDestination(path) {
  if (existsSync(path)) return realpathSync(path);
  return join(canonicalDestination(dirname(path)), basename(path));
}
const outputRelative = relative(root, canonicalDestination(output));
if (!outputRelative || (!outputRelative.startsWith(`..${sep}`) && outputRelative !== '..' && !isAbsolute(outputRelative))) throw new Error('Output must be outside the game installation');
mkdirSync(output, { recursive: true });
const files = [];
function walk(dir) { for (const item of readdirSync(dir, { withFileTypes: true }).sort((a,b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0)) { const path = join(dir,item.name); if (item.isDirectory()) walk(path); else if(item.isFile()) files.push(path); } }
walk(root);
const totals = {}, streamTypes = {}, errors = [], records = [], streamPackages = [], sampleCandidates = new Map(), zipSamples = [];
for (const path of files) {
  const rel = relative(root,path).replaceAll('\\','/'), extension = extname(path).toLowerCase(), group = rel.split('/')[0];
  const bytes = statSync(path).size;
  const key = `${group}:${extension}`;
  totals[key] ??= { group, extension, files: 0, bytes: 0 }; totals[key].files++; totals[key].bytes += bytes;
  const fd = openSync(path,'r'), prefix = Buffer.alloc(Math.min(64,bytes));
  try { readSync(fd,prefix,0,prefix.length,0); } finally { closeSync(fd); }
  const record = { path: rel, bytes, prefixHex: prefix.subarray(0,16).toString('hex') };
  if (['.exe','.dll'].includes(extension)) {
    const binary=readFileSync(path);
    record.sha256=createHash('sha256').update(binary).digest('hex');
    if(binary.length >= 64 && binary.toString('ascii',0,2)==='MZ') {
      const pe=binary.readUInt32LE(0x3c);
      if(pe+24 <= binary.length && binary.readUInt32LE(pe)===0x4550) record.pe={ machine: `0x${binary.readUInt16LE(pe+4).toString(16)}`,coffTimestamp:binary.readUInt32LE(pe+8), sections:binary.readUInt16LE(pe+6) };
    }
  }
  if(group==='SP' && ['.spk','.mpk'].includes(extension)) {
    try {
      const structure=scanStreamPackage(path);
      const summary={path:rel,...structure};
      streamPackages.push(summary);
      for(const chunk of structure.chunks) {streamTypes[chunk.type]??={chunks:0,compressedBytes:0,uncompressedBytes:0};streamTypes[chunk.type].chunks++;streamTypes[chunk.type].compressedBytes+=chunk.compressedBytes;streamTypes[chunk.type].uncompressedBytes+=chunk.uncompressedBytes;}
      if(!sampleCandidates.has(extension))sampleCandidates.set(extension,{path,relativePath:rel,structure});
    }catch(error){errors.push({path:rel,stage:'stream-headers',error:error.message});}
  }
  if(group==='SP' && extension==='.pk4') {
    try {
      const archive=readFileSync(path), directory=zipDirectory(archive), extensions={}, shaderVersionTokens={}, markerMembers={}, memberPrefixSamples=[];let verifiedMembers=0,verifiedBytes=0;
      for(const entry of directory.entries) {
        const ex=entry.name.endsWith('/')?'[directory]':extname(entry.name).toLowerCase()||'[none]'; extensions[ex]??={members:0,bytes:0};extensions[ex].members++;extensions[ex].bytes+=entry.bytes;
        const data=zipMember(archive,entry); verifiedMembers++;verifiedBytes+=data.length;
        if(['.vso','.pso'].includes(ex) && data.length>=4) {
          const token=data.readUInt32LE(),hex=token.toString(16).padStart(8,'0');shaderVersionTokens[hex]=(shaderVersionTokens[hex]??0)+1;
        }
        if(!entry.name.endsWith('/') && memberPrefixSamples.filter(s=>s.extension===ex).length<2)memberPrefixSamples.push({name:entry.name,extension:ex,prefixHex:data.subarray(0,Math.min(32,data.length)).toString('hex'),prefixAscii:data.subarray(0,Math.min(32,data.length)).toString('latin1').replace(/[^\x20-\x7e]/g,'.')});
        if(['.cfg','.script','.mtr','.def','.tech','.guide','.fx'].includes(ex)) {
          const text=data.toString('utf8');
          for(const marker of ['entityDef','vertexShader','pixelShader','technique','Havok','Scaleform','Raven'])if(text.toLowerCase().includes(marker.toLowerCase()))markerMembers[marker]=(markerMembers[marker]??0)+1;
        }
      }
      zipSamples.push({path:rel,bytes,sha256:createHash('sha256').update(archive).digest('hex'),footerSignature:directory.footerSignature,footerType:directory.footerType,centralOffset:directory.centralOffset,centralBytes:directory.centralBytes,members:directory.entries.length,verifiedMembers,verifiedBytes,allMemberCrcsVerified:true,extensions,shaderVersionTokens,textMarkerMatchingMembers:markerMembers,textMarkerExtensions:['.cfg','.script','.mtr','.def','.tech','.guide','.fx'],memberPrefixSamples,memberNameSamples:directory.entries.filter(e=>!e.name.endsWith('/')).slice(0,20).map(e=>e.name)});
    }catch(error){errors.push({path:rel,stage:'zip-inventory',error:error.message});}
  }
  records.push(record);
}
const streams=[];
for(const sample of sampleCandidates.values()){
  try{streams.push({path:sample.relativePath,chunks:sampleStreamPackage(sample.path,sample.structure)});}catch(error){errors.push({path:sample.relativePath,stage:'stream-sample',error:error.message});}
}
const inventory={schemaVersion:1,generatedAt:new Date().toISOString(),root,coverage:{files:files.length,bytes:records.reduce((n,f)=>n+f.bytes,0),streamPackages:streamPackages.length,streamHeaderOnly:true,streamPayloadSamples:streams.length,spPk4Archives:zipSamples.length},totals:Object.values(totals),streamTypes,errors,files:records,streamPackages,limitations:['Directory and header observations do not establish runtime behavior or completeness against an original release manifest.','Only SP PK4 members are decompressed and CRC-checked; MP PK4 contents are not inspected.','All SPK/MPK chunk boundaries are checked; payload file tables are decompressed only for the explicitly listed samples.','SP game binaries are hashed and PE headers inspected; no program was executed and no game logic was reconstructed.','The 0x48530a02 PK4 footer is an observed signature substitution; no general compatibility beyond validated fields is claimed.']};
const packages={schemaVersion:1,generatedAt:inventory.generatedAt,zipArchives:zipSamples,streamSamples:streams,errors};
writeFileSync(join(output,'inventory.json'),JSON.stringify(inventory,null,2)+'\n');
writeFileSync(join(output,'package-sample.json'),JSON.stringify(packages,null,2)+'\n');
console.log(JSON.stringify({coverage:inventory.coverage,totals:inventory.totals,streamTypes,zipArchives:zipSamples.map(({path,members,verifiedMembers,verifiedBytes,footerType})=>({path,members,verifiedMembers,verifiedBytes,footerType})),streamSamples:streams.map(s=>({path:s.path,chunks:s.chunks.map(c=>({type:c.type,files:c.files}))})),errors},null,2));
if(errors.length)process.exitCode=1;
