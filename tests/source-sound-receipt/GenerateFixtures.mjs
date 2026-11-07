// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
const output=process.argv[2];
if(!output)throw Error('Pass a new private fixture output directory');
const target=path.resolve(output);
fs.mkdirSync(target,{recursive:false});
const valid=Buffer.alloc(4*417);
for(let i=0;i<4;++i)Buffer.from([0xff,0xfb,0x90,0xc0]).copy(valid,i*417);
fs.writeFileSync(path.join(target,'receipt-valid.mp3'),valid,{flag:'wx'});
fs.writeFileSync(path.join(target,'receipt-truncated.mp3'),valid.subarray(0,-1),{flag:'wx'});
fs.writeFileSync(path.join(target,'receipt-error.mp3'),Buffer.alloc(64,0x42),{flag:'wx'});
process.stdout.write('Authored silence/truncation/error fixtures only; no original assets.\n');
