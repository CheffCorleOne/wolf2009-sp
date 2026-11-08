// SPDX-License-Identifier: GPL-3.0-or-later
// Stages native audio-bank QA packs into a NEW private fixture directory.
// Authored packs contain only generated MPEG1 LayerIII silence frames and
// opaque bytes. Derived packs are byte edits of the owner's own original pack
// copy and must stay in ignored evidence/device fixture directories.
// usage: node MakeNativeFixtures.mjs <owner original english spk> <output base dir>
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const [original, output] = process.argv.slice(2);
if (!original || !output) throw Error('usage: MakeNativeFixtures.mjs <original.spk> <output base dir>');
const packs = path.join(path.resolve(output), 'streampacks');
fs.mkdirSync(packs, { recursive: true });

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
  parts.push(pad(size)); size += pad(size).length;
  for (const e of entries) { parts.push(e.bytes, pad(e.bytes.length)); size += e.bytes.length + pad(e.bytes.length).length; }
  return Buffer.concat(parts);
}
function chunk(type, body) {
  const compressed = zlib.deflateSync(body, { level: 6 });
  const tag = Buffer.from(type.split('').reverse().join(''), 'latin1');
  return Buffer.concat([tag, u32(body.length), u32(compressed.length), compressed]);
}
const pack = chunks => Buffer.concat([u32(300), ...chunks]);
function parse(bytes) {
  if (bytes.readUInt32LE(0) !== 300) throw Error('original pack version differs');
  const chunks = [];
  for (let at = 4; at < bytes.length;) {
    const type = bytes.subarray(at, at + 4).toString('latin1').split('').reverse().join('');
    const inflatedSize = bytes.readUInt32LE(at + 4), compressedSize = bytes.readUInt32LE(at + 8);
    const body = zlib.inflateSync(bytes.subarray(at + 12, at + 12 + compressedSize));
    if (body.length !== inflatedSize) throw Error('original chunk size differs');
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
    if (c !== body.length) throw Error('original chunk table coverage differs');
    chunks.push({ type, entries });
    at += 12 + compressedSize;
  }
  return chunks;
}
// Authored MPEG1 LayerIII silence: 128 kbit/s, 44100 Hz, mono, no padding.
function silence(frames) {
  const out = Buffer.alloc(frames * 417);
  for (let i = 0; i < frames; ++i) Buffer.from([0xff, 0xfb, 0x90, 0xc0]).copy(out, i * 417);
  return out;
}
const write = (name, bytes) => fs.writeFileSync(path.join(packs, `${name}_vo_english.spk`), bytes, { flag: 'wx' });

const source = fs.readFileSync(original);
const chunks = parse(source);
if (chunks.length !== 1 || chunks[0].type !== 'SNDS' || chunks[0].entries.length !== 19)
  throw Error('owner pack differs from the audited one-chunk/19-member English bank');

// Derived from the owner's copy (ignored fixture only).
const lastBad = chunks[0].entries.map(e => ({ name: e.name, bytes: e.bytes }));
lastBad[18] = { name: lastBad[18].name, bytes: lastBad[18].bytes.subarray(0, lastBad[18].bytes.length - 1) };
write('qa_lastbad', pack([chunk('SNDS', plain(lastBad))]));
const corrupt = Buffer.from(source); corrupt[corrupt.length - 1] ^= 0x5a; // zlib Adler-32 byte
write('qa_corrupt', corrupt);
write('qa_truncated', source.subarray(0, source.length - 7));

// Authored only.
const a = silence(2), b = silence(3), c = silence(4);
write('qa_silence', pack([chunk('SNDS', plain([{ name: 'sound/qa/a', bytes: a }, { name: 'sound/qa/b', bytes: b },
  { name: 'sound/qa/c', bytes: c }]))]));
write('qa_badcodec', pack([chunk('SNDS', plain([{ name: 'sound/qa/a', bytes: a }, { name: 'sound/qa/b', bytes: b },
  { name: 'sound/qa/c', bytes: Buffer.alloc(64, 0x42) }]))]));
write('qa_unknown', pack([chunk('SNDS', plain([{ name: 'sound/qa/a', bytes: a }])),
  chunk('DECL', plain([{ name: 'opaque', bytes: Buffer.from([1, 2, 3]) }]))]));
write('qa_alias', pack([chunk('SNDS', plain([{ name: 'sound/qa/x', bytes: a }, { name: 'sound/qa/x.mp3', bytes: b }]))]));
write('qa_dupcase', pack([chunk('SNDS', plain([{ name: 'sound/qa/a', bytes: a }, { name: 'SOUND/QA/A', bytes: b }]))]));
process.stdout.write(JSON.stringify({ derivedFromOwnerCopy: ['qa_lastbad', 'qa_corrupt', 'qa_truncated'],
  authored: ['qa_silence', 'qa_badcodec', 'qa_unknown', 'qa_alias', 'qa_dupcase'],
  lastMember: chunks[0].entries[18].name }) + '\n');
