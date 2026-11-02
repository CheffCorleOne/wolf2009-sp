import test from 'node:test';
import assert from 'node:assert/strict';
import { deflateSync } from 'node:zlib';
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, existsSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { spawnSync } from 'node:child_process';

// Invented geometry only. Repeated PBBF chunks and an unreferenced far vertex
// exercise archive lookup and world-only compaction across the CLI boundary.
function fixture() {
  const format = 'Position 4 Normal Dec_11_11_10N Tangent Dec_11_11_10N Binormal Dec_11_11_10N DiffuseColor TexCoord 2 0';
  const mesh = (first, start, bounds) => `Mesh { Material "synthetic/material" DrawBuffers 1 0 PrimBatch[1] { PrimBatch { DrawIndexedTriList ${first} 3 ${start} 1 } } Bounds ${bounds} }`;
  const proc = Buffer.from(`MemSize 1\nMD5RProcVersion 7\n0\nVertexBuffer[2] {
    VertexBuffer { VertexFormat { Position 4 } Vertex[8] {} }
    VertexBuffer { VertexFormat { ${format} } Vertex[7] { file "world_vb1.MD5RBin" } }
  } IndexBuffer[1] { IndexBuffer { BitDepth 16 Index[6] { file "world_ib0.MD5RBin" } } }
  Model[2] {
    Model "_area0" { Mesh[1] { ${mesh(1, 0, '0 0 0 2 2 0')} } Bounds 0 0 0 2 2 0 }
    Model "local_door" { Mesh[1] { ${mesh(4, 3, '100 0 0 101 1 0')} } Bounds 100 0 0 101 1 0 }
  }`);
  const points = [[1000,1000,1000], [0,0,0], [2,0,0], [0,2,0], [100,0,0], [101,0,0], [100,1,0]];
  const vertices = Buffer.alloc(points.length * 40);
  points.forEach((p, i) => { [...p,1].forEach((n,j) => vertices.writeFloatLE(n, i*40+j*4)); vertices.writeFloatLE(i/8,i*40+32); vertices.writeFloatLE(0.5,i*40+36); });
  const indices = Buffer.alloc(12); [1,2,3,4,5,6].forEach((n,i) => indices.writeUInt16LE(n,i*2));
  function chunk(type, name, bytes) {
    const n = Buffer.from(name + '\0'), table = Buffer.alloc(4+n.length+8);
    table.writeUInt32LE(1); n.copy(table,4); table.writeUInt32LE(bytes.length,4+n.length); table.writeUInt32LE(bytes.length,8+n.length);
    const tablePad = Buffer.alloc((4-table.length%4)%4), dataPad=Buffer.alloc((4-bytes.length%4)%4);
    const raw=Buffer.concat([table,tablePad,bytes,dataPad]), packed=deflateSync(raw), header=Buffer.alloc(12);
    header.write(type.split('').reverse().join('')); header.writeUInt32LE(raw.length,4); header.writeUInt32LE(packed.length,8);
    return Buffer.concat([header,packed]);
  }
  const magic=Buffer.alloc(4); magic.writeUInt32LE(300);
  return Buffer.concat([magic,chunk('PROC','maps/game/synthetic/world',proc),chunk('PBBF','maps/game/synthetic/world_vb1',vertices),chunk('PBBF','maps/game/synthetic/world_ib0',indices)]);
}
const sceneTool=resolve('build/native/wolf2009-scene.exe'), previewTool=resolve('build/native/mesh-preview.exe');
function run(exe,args) { const r=spawnSync(exe,args,{encoding:'utf8',timeout:20000}); assert.ifError(r.error); return r; }
function withFixture(fn) {
  const temp=mkdtempSync(join(tmpdir(),'wolf2009-scene-test-'));
  try { const folder=join(temp,'original','SP','base','maps','synthetic'); mkdirSync(folder,{recursive:true}); const input=join(folder,'world.mpk'); writeFileSync(input,fixture()); fn({temp,input}); }
  finally { assert.equal(dirname(resolve(temp)),resolve(tmpdir())); assert.ok(temp.includes('wolf2009-scene-test-')); rmSync(temp,{recursive:true}); }
}
test('scene CLI aggregates repeated PBBF and validates without output',()=>withFixture(({input})=>{
  const result=run(sceneTool,['--validate',input]); assert.equal(result.status,0,result.stderr);
  const stats=JSON.parse(result.stdout); assert.equal(stats.vertices,7); assert.equal(stats.triangles,2); assert.equal(stats.models,2);
}));
test('world export compacts referenced vertices and preserves UV; preview is nonempty',()=>withFixture(({temp,input})=>{
  const out=join(temp,'world.obj'), result=run(sceneTool,[input,out]); assert.equal(result.status,0,result.stderr);
  const stats=JSON.parse(result.stdout); assert.equal(stats.exportedTriangles,1); assert.equal(stats.exportedVertices,3);
  const obj=readFileSync(out,'utf8'); assert.equal(obj.split('\n').filter(s=>s.startsWith('v ')).length,3);
  assert.ok(!obj.includes('1000')); assert.ok(obj.includes('f 1/1 2/2 3/3'));
  const image=join(temp,'world.bmp'), rendered=run(previewTool,[out,image,'--width','128','--height','128']); assert.equal(rendered.status,0,rendered.stderr);
  assert.match(rendered.stdout,/covered-pixels=[1-9]\d*/); const before=readFileSync(image); assert.equal(before.toString('ascii',0,2),'BM');
  const again=run(previewTool,[out,image]); assert.notEqual(again.status,0); assert.deepEqual(readFileSync(image),before);
}));
test('scene CLI refuses overwriting output and writing within an installation',()=>withFixture(({temp,input})=>{
  const existing=join(temp,'existing.obj'); writeFileSync(existing,'preserve me');
  assert.notEqual(run(sceneTool,[input,existing]).status,0); assert.equal(readFileSync(existing,'utf8'),'preserve me');
  const forbidden=join(temp,'original','SP','forbidden.obj'); assert.notEqual(run(sceneTool,[input,forbidden]).status,0); assert.equal(existsSync(forbidden),false);
}));
