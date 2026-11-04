// SPDX-License-Identifier: GPL-3.0-or-later
// Independently authored ENTS/BRAI bytes. No original game assets.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

function u32(value) {const b=Buffer.alloc(4);b.writeUInt32LE(value>>>0);return b;}
function floats(values) {const b=Buffer.alloc(values.length*4);values.forEach((v,i)=>b.writeFloatLE(v,i*4));return b;}
function cstrings(values) {return Buffer.from(values.join('\0')+'\0');}
function entities() {
  const keys=['classname','name','model','origin','brain'];
  const values=['func_mover','source_mover_qa_a','models/own/source_mover','own/source_mover_a.brain',
    'source_mover_qa_b','own/source_mover_b.brain','info_null','source_mover_qa_finish_a','source_mover_qa_finish_b'];
  const records=[[[0,0,1,0],[1,0,1,1],[2,0,1,2],[3,1,3,0],[4,0,1,3]],
    [[0,0,1,0],[1,0,1,4],[2,0,1,2],[3,1,3,3],[4,0,1,5]],
    [[0,0,1,6],[1,0,1,7],[3,1,3,6]],[[0,0,1,6],[1,0,1,8],[3,1,3,9]]];
  const keyBytes=cstrings(keys),valueBytes=cstrings(values),numbers=[1024,0,0,2048,0,0,1024,64,0,2048,64,0];
  const properties=records.reduce((n,r)=>n+r.length,0);
  const chunks=[Buffer.from(`Version 4\nbinary\nentities ${records.length} ${records.length*4+properties*16}\nkeys ${keys.length} ${keyBytes.length}\nvalues ${values.length} ${valueBytes.length}\nintValues 0\nfloatValues ${numbers.length}\n`),floats(numbers)];
  let count=chunks.reduce((n,b)=>n+b.length,0);
  const append=b=>{chunks.push(b);count+=b.length;};
  const align=()=>{const n=(4-count%4)%4;if(n)append(Buffer.alloc(n));};
  append(keyBytes);align();for(let i=0;i<keys.length;++i)append(u32(records.flat().filter(p=>p[0]===i).length));
  append(valueBytes);align();for(let i=0;i<values.length;++i)append(u32(records.flat().filter(p=>p[1]===0&&p[3]===i).length));
  for(const r of records){append(u32(r.length));for(const p of r)for(const n of p)append(u32(n));}
  append(Buffer.from('\n'));return Buffer.concat(chunks);
}
function output(target) {return '\t'+Array.from({length:99},(_,i)=>i===0?'16':i===1?'1':i===2?String(target):i===98?'1.000000':'0').join('|')+'|\n\t\t-1|\n';}
function brain(destination,speed) {
  const text='h1023|32|32|32|0|\n1|20|0|0|0|^1|1|^74|1024|\n'+output(9)+
    `132|9|0|0|0|^132|16|^131|1024|mMover|Me|mDestination|${destination}|mSpeed|${speed}|mTime|0|mAccelTime|0|mDecelTime|0|mMaxSpeed|0|mMaxTime|0|mUseCustomSounds|false|\n`+
    '\t16|1|11|0|0|\n456|11|0|0|0|^456|0|mCommand|RETAIN_DO_NOT_EXECUTE|\n';
  return Buffer.from(text.replaceAll('|','\0'));
}
export function sourceMoverFixtures() {
  return new Map([['fixtures/source-mover/own.ents',entities()],
    ['fixtures/source-mover/a.brai',brain('source_mover_qa_finish_a',25)],
    ['fixtures/source-mover/b.brai',brain('source_mover_qa_finish_b',40)]]);
}
export function prepareSourceMoverFixture(baseDirectory) {
  const root=path.resolve(baseDirectory),files=[];
  for(const [relative,bytes] of sourceMoverFixtures()){
    const target=path.resolve(root,relative);if(!target.startsWith(root+path.sep))throw Error('Source mover fixture path containment');
    fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);files.push({path:relative,bytes:bytes.length});
  }
  return {scope:'Independently authored source-mover data; caller preloads a qualified own native collision model',files};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  if(process.argv.length!==4||process.argv[2]!=='--output')throw Error('Usage: --output <fixture base directory>');
  process.stdout.write(JSON.stringify(prepareSourceMoverFixture(process.argv[3]),null,2)+'\n');
}
