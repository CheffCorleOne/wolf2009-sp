// SPDX-License-Identifier: GPL-3.0-or-later
// Derives private QA packs from the owner's own English wi2 trainyard pack
// copy. Output stays in ignored evidence/device fixture directories.
//   lastbad <wi2 copy> <output.spk>  final member shortened by one byte
//   missing <wi2 copy> <output.spk>  final member removed from the SNDS table
import fs from 'node:fs';
import zlib from 'node:zlib';

const [mode, original, output] = process.argv.slice(2);
if (!['lastbad', 'missing'].includes(mode) || !original || !output)
  throw Error('usage: MakeInterrogatorFixtures.mjs lastbad|missing <wi2 copy> <output.spk>');
const u32 = v => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0); return b; };
const pad = n => Buffer.alloc((4 - (n & 3)) & 3);
function plain(entries) {
  const parts = [u32(entries.length)];
  let size = 4;
  for (const e of entries) {
    const name = Buffer.concat([Buffer.from(e.name, 'latin1'), Buffer.from([0])]);
    parts.push(name, u32(e.bytes.length), u32(e.bytes.length));
    size += name.length + 8;
  }
  parts.push(pad(size));
  for (const e of entries) parts.push(e.bytes, pad(e.bytes.length));
  return Buffer.concat(parts);
}
function chunk(type, body) {
  const compressed = zlib.deflateSync(body, { level: 6 });
  return Buffer.concat([Buffer.from(type.split('').reverse().join(''), 'latin1'), u32(body.length), u32(compressed.length), compressed]);
}
const bytes = fs.readFileSync(original);
if (bytes.readUInt32LE(0) !== 300) throw Error('pack version differs');
const type = bytes.subarray(4, 8).toString('latin1').split('').reverse().join('');
const inflated = bytes.readUInt32LE(8), compressed = bytes.readUInt32LE(12);
if (type !== 'SNDS' || 16 + compressed !== bytes.length) throw Error('owner wi2 pack is not the audited one-chunk SNDS bank');
const body = zlib.inflateSync(bytes.subarray(16, 16 + compressed));
if (body.length !== inflated) throw Error('chunk size differs');
const count = body.readUInt32LE(0), entries = [];
let c = 4;
for (let i = 0; i < count; ++i) {
  const end = body.indexOf(0, c);
  const name = body.subarray(c, end).toString('latin1');
  c = end + 1;
  const size = body.readUInt32LE(c); c += 8;
  entries.push({ name, size });
}
c += (4 - (c & 3)) & 3;
for (const e of entries) { e.bytes = Buffer.from(body.subarray(c, c + e.size)); c += e.size; c += (4 - (c & 3)) & 3; }
if (c !== body.length || entries.length !== 9) throw Error('owner wi2 pack differs from the measured 9-member bank');
const last = entries[8];
if (mode === 'lastbad') entries[8] = { name: last.name, bytes: last.bytes.subarray(0, last.bytes.length - 1) };
else entries.pop();
fs.writeFileSync(output, Buffer.concat([u32(300), chunk('SNDS', plain(entries))]));
process.stdout.write(JSON.stringify({ mode, derivedFromOwnerCopy: true, members: entries.length, finalMember: last.name }) + '\n');
