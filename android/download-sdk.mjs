// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded ranged download for the exact official SDK metadata URL and digest.
import {open, stat, rename, unlink} from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import {createHash} from 'node:crypto';
const [url,sizeArg,algorithm,expected,destination]=process.argv.slice(2);
const size=Number(sizeArg),chunk=2*1024*1024;
if(!url?.startsWith('https://dl.google.com/android/repository/') || !Number.isSafeInteger(size) || size<1 || !['sha1','sha256'].includes(algorithm) || !new RegExp(`^[0-9a-f]{${algorithm==='sha1'?40:64}}$`).test(expected) || !destination) throw Error('Invalid official SDK download parameters');
async function digest(path,algo){const h=createHash(algo);for await(const b of createReadStream(path))h.update(b);return h.digest('hex');}
try {if((await stat(destination)).size===size && await digest(destination,algorithm)===expected){process.stdout.write(`Verified cached ${destination}\n`);process.exit(0);}throw Error('Existing destination has different bytes; preserve it and select a fresh tools directory');}catch(e){if(e.code!=='ENOENT')throw e;}
const temporary=destination+'.partial';
try{await unlink(temporary);}catch(e){if(e.code!=='ENOENT')throw e;}
const file=await open(temporary,'wx');let next=0,done=0;
try {
  await file.truncate(size);
  async function worker(){while(next<size){const start=next;next+=chunk;const end=Math.min(size,start+chunk)-1;let bytes;
    for(let attempt=0;attempt<4;attempt++){try{const response=await fetch(url,{headers:{Range:`bytes=${start}-${end}`},signal:AbortSignal.timeout(45000)});if(response.status!==206 || response.headers.get('content-range')!==`bytes ${start}-${end}/${size}`)throw Error('Unexpected range response');bytes=Buffer.from(await response.arrayBuffer());if(bytes.length!==end-start+1)throw Error('Truncated range');break;}catch(e){if(attempt===3)throw e;}}
    let written=0;while(written<bytes.length){const result=await file.write(bytes,written,bytes.length-written,start+written);if(!result.bytesWritten)throw Error('Short disk write');written+=result.bytesWritten;}
    done+=bytes.length;if(done%(64*1024*1024)<chunk||done===size)process.stdout.write(`${Math.round(done/size*100)}% ${done}/${size}\n`);
  }}
  const results=await Promise.allSettled(Array.from({length:6},worker));const failure=results.find(r=>r.status==='rejected');if(failure)throw failure.reason;
}finally{await file.close();}
if(await digest(temporary,algorithm)!==expected)throw Error('Official SDK digest mismatch; partial file retained');
await rename(temporary,destination);
process.stdout.write(`Verified ${algorithm}: ${destination}\nSHA256 ${await digest(destination,'sha256')}\n`);
