import test from 'node:test';
import assert from 'node:assert/strict';
import { deflateRawSync, deflateSync } from 'node:zlib';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve, dirname, basename } from 'node:path';
import { crc32, zipDirectory, zipMember, scanStreamPackage, streamPayload, sampleStreamPackage } from '../tools/lib/packages.mjs';

function fixtureZip({ wolf = false, method = 0 } = {}) {
  const name=Buffer.from('synthetic/test.txt'), data=Buffer.from('synthetic data\n'), compressed=method===8?deflateRawSync(data):data;
  const local=Buffer.alloc(30+name.length+compressed.length);
  local.writeUInt32LE(0x04034b50);local.writeUInt16LE(20,4);local.writeUInt16LE(method,8);
  local.writeUInt32LE(crc32(data),14);local.writeUInt32LE(compressed.length,18);local.writeUInt32LE(data.length,22);local.writeUInt16LE(name.length,26);
  name.copy(local,30);compressed.copy(local,30+name.length);
  const cd=Buffer.alloc(46+name.length);cd.writeUInt32LE(0x02014b50);cd.writeUInt16LE(20,4);cd.writeUInt16LE(20,6);cd.writeUInt16LE(method,10);
  cd.writeUInt32LE(crc32(data),16);cd.writeUInt32LE(compressed.length,20);cd.writeUInt32LE(data.length,24);cd.writeUInt16LE(name.length,28);name.copy(cd,46);
  const end=Buffer.alloc(22);end.writeUInt32LE(wolf?0x48530a02:0x06054b50);end.writeUInt16LE(1,8);end.writeUInt16LE(1,10);end.writeUInt32LE(cd.length,12);end.writeUInt32LE(local.length,16);
  return {buffer:Buffer.concat([local,cd,end]),data};
}
function fixturePayload() {
  const name=Buffer.from('synthetic/example\0'), entry=Buffer.from('DDS synthetic data');
  const tableBytes=4+name.length+8, aligned=Math.ceil(tableBytes/4)*4;
  const payload=Buffer.alloc(Math.ceil((aligned+entry.length)/4)*4);payload.writeUInt32LE(1);name.copy(payload,4);payload.writeUInt32LE(entry.length,4+name.length);payload.writeUInt32LE(entry.length,8+name.length);entry.copy(payload,aligned);
  return {payload,entry};
}
function fixtureStream(payload) {
  const compressed=deflateSync(payload),header=Buffer.alloc(16);
  header.writeUInt32LE(300);header.write('RTXT',4,'ascii');header.writeUInt32LE(payload.length,8);header.writeUInt32LE(compressed.length,12);
  return Buffer.concat([header,compressed]);
}

test('CRC32 known vector',()=>assert.equal(crc32(Buffer.from('123456789')),0xcbf43926));
for(const wolf of [false,true])for(const method of [0,8])test(`ZIP ${wolf?'Wolf2009':'standard'} method ${method}: directory and member CRC`,()=>{
  const {buffer,data}=fixtureZip({wolf,method});const parsed=zipDirectory(buffer);
  assert.equal(parsed.entries.length,1);assert.equal(parsed.entries[0].name,'synthetic/test.txt');assert.equal(parsed.footerType,wolf?'wolf2009-substituted-signature':'zip');assert.deepEqual(zipMember(buffer,parsed.entries[0]),data);
});
test('Rejects custom footer with inconsistent central bounds',()=>{
  const {buffer}=fixtureZip({wolf:true});buffer.writeUInt32LE(0,buffer.length-6);assert.throws(()=>zipDirectory(buffer),/bounds/);
});
test('Rejects multi-disk, entry count mismatch, truncated footer and forged local header',()=>{
  for(const mutate of [b=>b.writeUInt16LE(1,b.length-18),b=>b.writeUInt16LE(2,b.length-12),b=>b.writeUInt32LE(0,0)]){
    const {buffer}=fixtureZip({wolf:true});mutate(buffer);assert.throws(()=>zipDirectory(buffer));
  }
  assert.throws(()=>zipDirectory(fixtureZip().buffer.subarray(0,-1)),/end record/);
});
test('Rejects changed ZIP member CRC and decompression budget',()=>{
  const {buffer}=fixtureZip();const parsed=zipDirectory(buffer);buffer[parsed.entries[0].dataOffset]^=1;
  assert.throws(()=>zipMember(buffer,parsed.entries[0]),/CRC/);assert.throws(()=>zipMember(buffer,parsed.entries[0],1),/budget/);
});
test('Stream payload table preserves alignment and data identity',()=>{
  const {payload,entry}=fixturePayload(),parsed=streamPayload(payload);assert.equal(parsed.count,1);assert.equal(parsed.files[0].dataOffset%4,0);assert.equal(parsed.files[0].bytes,entry.length);assert.equal(parsed.files[0].prefixAscii,'DDS synthetic data');
});
test('Rejects malformed stream count, missing terminator and duplicated size mismatch',()=>{
  assert.throws(()=>streamPayload(Buffer.from([1,0,0,0])),/capacity/);
  const missing=Buffer.alloc(32,0x41);missing.writeUInt32LE(1);assert.throws(()=>streamPayload(missing),/name/);
  const {payload}=fixturePayload();payload.writeUInt32LE(999,4+Buffer.byteLength('synthetic/example\0'));assert.throws(()=>streamPayload(payload),/disagree/);
});
test('Rejects stream payload bounds and unexplained trailing data',()=>{
  const {payload}=fixturePayload();assert.throws(()=>streamPayload(payload.subarray(0,-8)),/Truncated/);assert.throws(()=>streamPayload(Buffer.concat([payload,Buffer.alloc(4)])),/trailing/);
});
test('Rejects truncated final stream padding',()=>{
  const {payload}=fixturePayload();assert.throws(()=>streamPayload(payload.subarray(0,-1)),/alignment exceeds/);
});
test('Zero-entry stream table must contain only the count word',()=>{
  assert.equal(streamPayload(Buffer.alloc(4)).count,0);
  assert.throws(()=>streamPayload(Buffer.alloc(5)),/trailing/);
  assert.throws(()=>streamPayload(Buffer.alloc(8)),/trailing/);
});
test('Rejects empty stream file names',()=>{
  const payload=Buffer.alloc(16);payload.writeUInt32LE(1);assert.throws(()=>streamPayload(payload),/name empty/);
});
test('Stream file scan verifies chunk boundaries; sample inflate verifies table',()=>{
  const dir=mkdtempSync(join(tmpdir(),'wolf2009-inventory-'));
  try {
    const file=join(dir,'synthetic.spk'),{payload}=fixturePayload(),archive=fixtureStream(payload);writeFileSync(file,archive);
    const parsed=scanStreamPackage(file);assert.equal(parsed.version,300);assert.equal(parsed.chunks[0].type,'TXTR');assert.equal(sampleStreamPackage(file,parsed)[0].files,1);
    const withTrailing=Buffer.concat([archive,Buffer.from([0x12,0x34])]);withTrailing.writeUInt32LE(archive.readUInt32LE(12)+2,12);writeFileSync(file,withTrailing);
    assert.throws(()=>sampleStreamPackage(file),/compressed payload contains trailing/);
    writeFileSync(file,archive.subarray(0,-1));assert.throws(()=>scanStreamPackage(file),/bounds/);
    writeFileSync(file,Buffer.concat([archive,Buffer.from([1])]));assert.throws(()=>scanStreamPackage(file),/header truncated/);
    archive.writeUInt32LE(301);writeFileSync(file,archive);assert.throws(()=>scanStreamPackage(file),/magic/);
  } finally {
    const target=resolve(dir),parent=resolve(tmpdir());
    assert.equal(dirname(target),parent);assert.ok(basename(target).startsWith('wolf2009-inventory-'));
    rmSync(target,{recursive:true,force:true});
  }
});
