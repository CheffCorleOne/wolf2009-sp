import test from 'node:test';
import assert from 'node:assert/strict';
import { deflateRawSync } from 'node:zlib';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { spawnSync } from 'node:child_process';
import { crc32 } from '../tools/lib/packages.mjs';

function zip(wolf, method, commentSize = 0) {
  const name = Buffer.from('test.txt'), bytes = Buffer.from('synthetic archive payload');
  const packed = method === 8 ? deflateRawSync(bytes) : bytes;
  const local = Buffer.alloc(30 + name.length + packed.length);
  local.writeUInt32LE(0x04034b50); local.writeUInt16LE(20, 4); local.writeUInt16LE(method, 8);
  local.writeUInt32LE(crc32(bytes), 14); local.writeUInt32LE(packed.length, 18);
  local.writeUInt32LE(bytes.length, 22); local.writeUInt16LE(name.length, 26);
  name.copy(local, 30); packed.copy(local, 30 + name.length);
  const central = Buffer.alloc(46 + name.length);
  central.writeUInt32LE(0x02014b50); central.writeUInt16LE(20, 4); central.writeUInt16LE(20, 6);
  central.writeUInt16LE(method, 10); central.writeUInt32LE(crc32(bytes), 16);
  central.writeUInt32LE(packed.length, 20); central.writeUInt32LE(bytes.length, 24);
  central.writeUInt16LE(name.length, 28); name.copy(central, 46);
  const end = Buffer.alloc(22 + commentSize);
  end.writeUInt32LE(wolf ? 0x48530a02 : 0x06054b50);
  end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10);
  end.writeUInt32LE(central.length, 12); end.writeUInt32LE(local.length, 16);
  end.writeUInt16LE(commentSize, 20);
  return { bytes: Buffer.concat([local, central, end]), end: local.length + central.length };
}

for (const backend of ['miniz', 'legacy']) {
  test(`native ${backend}: standard and Wolf2009 PK4, stored/deflate, comments, CRC, malformed footer`, () => {
    const exe = resolve('build/native', `zip-${backend}.exe`);
    const temp = mkdtempSync(join(tmpdir(), 'wolf2009-native-zip-'));
    let ordinal = 0;
    function probe(bytes, expected, reject = false) {
      const file = join(temp, `${ordinal++}.zip`); writeFileSync(file, bytes);
      const result = spawnSync(exe, [...(reject ? ['--reject'] : []), file], { encoding: 'utf8', timeout: 15000 });
      assert.ifError(result.error);
      assert.equal(result.status, expected, result.stderr || result.stdout);
    }
    try {
      for (const wolf of [false, true]) for (const method of [0, 8]) for (const comment of [0, 1, 1010, 1024, 2047])
        probe(zip(wolf, method, comment).bytes, 0);
      for (const mutate of [
        (b, e) => b.writeUInt16LE(1, e + 4),
        (b, e) => b.writeUInt16LE(2, e + 10),
        (b, e) => b.writeUInt32LE(0, e + 16),
        (b, e) => b.writeUInt32LE(0xffffffff, e + 12),
        (b, e) => b.writeUInt16LE(1, e + 20),
        (b, e) => b.writeUInt32LE(0x01020304, e),
      ]) { const f = zip(true, 0); mutate(f.bytes, f.end); probe(f.bytes, 0, true); }
      const corrupt = zip(true, 0); corrupt.bytes[38] ^= 1; probe(corrupt.bytes, 1);
      probe(zip(true, 0).bytes.subarray(0, -1), 0, true);
    } finally {
      assert.equal(dirname(resolve(temp)), resolve(tmpdir()));
      assert.ok(temp.includes('wolf2009-native-zip-'));
      rmSync(temp, { recursive: true });
    }
  });
}
