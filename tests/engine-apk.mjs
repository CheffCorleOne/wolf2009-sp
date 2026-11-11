// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { crc32, deflateRawSync } from 'node:zlib';
import { abis, appendStored, inspectProductionConfiguration, readZip } from '../tools/lib/engine-apk.mjs';

const project = resolve('production-verifier-fixture');
const qaSwitches = ['WOLF2009_RUNTIME_RENDER_QA', 'WOLF2009_RUNTIME_GL_QA', 'WOLF2009_BRAIN_QA',
  'WOLF2009_SKINNED_QA', 'WOLF2009_ANIMATION_QA', 'WOLF2009_ANIMATION_V2_QA', 'WOLF2009_MESH_V2_QA'];
function sourceEngine(abi) {
  return { ANDROID_ABI: abi, ANDROID_PLATFORM: 'android-26', ANDROID_STL: 'c++_static', CMAKE_BUILD_TYPE: 'Release',
    CORE: 'ON', BASE: 'ON', WOLF2009_STREAMS: 'ON', WOLF2009_RENDERER: 'ON', WOLF2009_GAMEPLAY: 'ON',
    WOLF2009_BOOTSTRAP: 'ON', WOLF2009_ANIMATION: 'ON', WOLF2009_AAS140: 'ON', WOLF2009_SOURCE_AUDIO_BANK: 'ON',
    CMAKE_HOME_DIRECTORY: resolve(project, 'engine/doom3/neo'),
    ...Object.fromEntries(qaSwitches.map(name => [name, 'OFF'])) };
}

test('production accepts source-animation gameplay for both APK ABIs', () => {
  for (const abi of abis) {
    const cache = sourceEngine(abi), before = { ...cache };
    const result = inspectProductionConfiguration(project, cache, abi);
    assert.equal(result.WOLF2009_ANIMATION, 'ON');
    assert.deepEqual(cache, before, 'verification must not rewrite its input configuration');
  }
});

test('production refuses missing source systems before inspecting native libraries', () => {
  for (const name of ['CORE', 'BASE', 'WOLF2009_STREAMS', 'WOLF2009_RENDERER', 'WOLF2009_GAMEPLAY', 'WOLF2009_BOOTSTRAP', 'WOLF2009_ANIMATION',
    'WOLF2009_AAS140', 'WOLF2009_SOURCE_AUDIO_BANK']) {
    const cache = sourceEngine(abis[0]);
    cache[name] = 'OFF';
    assert.throws(() => inspectProductionConfiguration(project, cache, abis[0]), new RegExp(`${name} must be ON`));
    delete cache[name];
    assert.throws(() => inspectProductionConfiguration(project, cache, abis[0]), new RegExp(`${name} must be ON`));
  }
});

test('every known QA switch must be explicitly disabled, including hidden game checks', () => {
  for (const name of qaSwitches) {
    const cache = sourceEngine(abis[1]);
    cache[name] = 'ON';
    assert.throws(() => inspectProductionConfiguration(project, cache, abis[1]), new RegExp(`${name} must be OFF`));
    delete cache[name];
    assert.throws(() => inspectProductionConfiguration(project, cache, abis[1]), new RegExp(`${name} must be OFF`));
  }
});

test('future enabled QA is refused while inactive QA source paths remain valid', () => {
  const cache = sourceEngine(abis[0]);
  cache.WOLF2009_FUTURE_QA = 'ON';
  assert.throws(() => inspectProductionConfiguration(project, cache, abis[0]), /WOLF2009_FUTURE_QA must be OFF/);
  cache.WOLF2009_FUTURE_QA = 'OFF';
  cache.WOLF2009_RUNTIME_RENDER_QA_SOURCE = '/unused/path/to/qa.cpp';
  assert.equal(inspectProductionConfiguration(project, cache, abis[0]).WOLF2009_FUTURE_QA, 'OFF');
});

test('production refuses another engine checkout or architecture', () => {
  const cache = sourceEngine(abis[0]);
  assert.throws(() => inspectProductionConfiguration(project, cache, abis[1]), /ANDROID_ABI must be/);
  assert.throws(() => inspectProductionConfiguration(project, cache, 'armeabi-v7a'), /Unsupported production ABI/);
  cache.CMAKE_HOME_DIRECTORY = resolve(project, 'other-engine/neo');
  assert.throws(() => inspectProductionConfiguration(project, cache, abis[0]), /different engine source checkout/);
});

test('stored APK writer keeps resource entries and appends stored members', () => {
  // One deflated resource member, as aapt2 writes it.
  const name = Buffer.from('res/a.xml'), data = Buffer.from('resource bytes '.repeat(20)), packed = deflateRawSync(data);
  const local = Buffer.alloc(30), central = Buffer.alloc(46), end = Buffer.alloc(22);
  const crc = crc32(data);
  local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(8, 8); local.writeUInt32LE(crc, 14);
  local.writeUInt32LE(packed.length, 18); local.writeUInt32LE(data.length, 22); local.writeUInt16LE(name.length, 26);
  central.writeUInt32LE(0x02014b50, 0); central.writeUInt16LE(8, 10); central.writeUInt32LE(crc, 16);
  central.writeUInt32LE(packed.length, 20); central.writeUInt32LE(data.length, 24); central.writeUInt16LE(name.length, 28);
  const directoryOffset = 30 + name.length + packed.length;
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10);
  end.writeUInt32LE(46 + name.length, 12); end.writeUInt32LE(directoryOffset, 16);
  const input = Buffer.concat([local, name, packed, central, name, end]);
  const dir = mkdtempSync(join(tmpdir(), 'engine-apk-')), library = join(dir, 'lib.so');
  try {
    writeFileSync(library, Buffer.from('ELF payload'));
    const out = readZip(appendStored(input, [{ name: 'lib/x86_64/libgame.so', path: library }]));
    assert.equal(out.get('res/a.xml').method, 8);
    assert.deepEqual(out.get('res/a.xml').data, data);
    assert.equal(out.get('lib/x86_64/libgame.so').method, 0);
    assert.equal(out.get('lib/x86_64/libgame.so').data.toString(), 'ELF payload');
    assert.throws(() => appendStored(input, [{ name: 'res/a.xml', path: library }]), /Unexpected existing APK member/);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
