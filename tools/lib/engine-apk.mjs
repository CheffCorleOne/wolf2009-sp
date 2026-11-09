// SPDX-License-Identifier: GPL-3.0-or-later
// Inspect the exact ELF/ZIP bytes which will be delivered, without executing them.
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { basename, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateRawSync } from 'node:zlib';

export const abis = ['arm64-v8a', 'x86_64'];
export const libraries = ['libdoom3.so', 'libgame.so', 'libwolf2009-enginehost.so'];
export const supportFiles = [
  'DoomConfig.cfg', 'config.spec', 'default.cfg', 'def/bootstrap.def',
  'def/wolf2009_runtime.def', 'script/doom_defs.script', 'script/doom_main.script',
  'materials/bootstrap.mtr', 'materials/wolf2009_collision.mtr',
  'models/wolf2009/runtime_player.md5mesh',
  ...['mainmenu', 'restart', 'gameover', 'msg', 'takeNotes', 'intro', 'loading', 'pda'].map(x => `guis/${x}.gui`),
  ...[12, 24, 48].map(x => `fonts/english/fontImage_${x}.dat`),
].sort();
export const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const check = (value, message) => { if (!value) throw Error(message); };
const number = value => {
  check(value <= BigInt(Number.MAX_SAFE_INTEGER), 'ELF integer exceeds safe file bounds');
  return Number(value);
};
function span(bytes, offset, length) {
  check(Number.isSafeInteger(offset) && Number.isSafeInteger(length) && offset >= 0 && length >= 0 &&
    offset + length <= bytes.length, 'Truncated binary structure');
  return bytes.subarray(offset, offset + length);
}
function cString(bytes, offset) {
  check(offset >= 0 && offset < bytes.length, 'String offset outside binary');
  const end = bytes.indexOf(0, offset);
  check(end >= offset, 'Unterminated binary string');
  return bytes.toString('utf8', offset, end);
}

export function inspectElf(bytes, abi, library) {
  span(bytes, 0, 64);
  check(bytes.toString('hex', 0, 4) === '7f454c46' && bytes[4] === 2 && bytes[5] === 1,
    'Expected a little-endian ELF64 library');
  check(bytes.readUInt16LE(16) === 3, 'Expected ET_DYN');
  const machine = bytes.readUInt16LE(18);
  check(machine === ({ 'arm64-v8a': 183, x86_64: 62 })[abi], `Wrong ELF machine for ${abi}`);
  const phoff = number(bytes.readBigUInt64LE(32)), phsize = bytes.readUInt16LE(54), phnum = bytes.readUInt16LE(56);
  check(phsize >= 56 && phnum > 0 && phnum < 1024, 'Invalid ELF program headers');
  const loads = [];
  let dynamic;
  for (let i = 0; i < phnum; ++i) {
    const h = span(bytes, phoff + i * phsize, 56);
    const type = h.readUInt32LE(0), offset = number(h.readBigUInt64LE(8));
    const address = number(h.readBigUInt64LE(16)), size = number(h.readBigUInt64LE(32));
    span(bytes, offset, size);
    if (type === 1) {
      const alignment = number(h.readBigUInt64LE(48));
      check(alignment >= 16384 && (alignment & (alignment - 1)) === 0 &&
        offset % 16384 === address % 16384, 'ELF LOAD is not 16 KiB aligned');
      loads.push({ offset, address, size, alignment });
    } else if (type === 2) dynamic = { offset, size };
  }
  check(loads.length > 0 && dynamic, 'Missing ELF LOAD/dynamic segment');
  const tags = new Map();
  for (let at = dynamic.offset; at + 16 <= dynamic.offset + dynamic.size; at += 16) {
    const tag = number(bytes.readBigUInt64LE(at)), value = number(bytes.readBigUInt64LE(at + 8));
    if (!tag) break;
    tags.set(tag, [...(tags.get(tag) || []), value]);
  }
  const strAddress = tags.get(5)?.[0], strSize = tags.get(10)?.[0];
  const strLoad = loads.find(x => strAddress >= x.address && strAddress + strSize <= x.address + x.size);
  check(strLoad, 'ELF dynamic strings are outside LOAD segments');
  const strings = span(bytes, strLoad.offset + strAddress - strLoad.address, strSize);
  const needed = (tags.get(1) || []).map(x => cString(strings, x));
  const soname = cString(strings, tags.get(14)?.[0] ?? -1);
  check(soname === library, `Unexpected ELF SONAME: ${soname}`);
  const allowed = new Set(['libandroid.so', 'liblog.so', 'libdl.so', 'libEGL.so', 'libGLESv3.so',
    'libOpenSLES.so', 'libz.so', 'libm.so', 'libc.so']);
  check(needed.every(x => allowed.has(x)), `Unexpected shared dependency: ${needed.join(', ')}`);

  const shoff = number(bytes.readBigUInt64LE(40)), shsize = bytes.readUInt16LE(58), shnum = bytes.readUInt16LE(60);
  check(shsize >= 64 && shnum > 0 && shnum < 65536, 'Missing ELF section table');
  const sections = Array.from({ length: shnum }, (_, i) => {
    const h = span(bytes, shoff + i * shsize, 64);
    return { type: h.readUInt32LE(4), offset: number(h.readBigUInt64LE(24)),
      size: number(h.readBigUInt64LE(32)), link: h.readUInt32LE(40), entry: number(h.readBigUInt64LE(56)) };
  });
  const exports = [];
  for (const section of sections.filter(x => x.type === 11)) {
    check(section.entry >= 24 && sections[section.link], 'Invalid ELF dynamic symbol table');
    const linked = sections[section.link], names = span(bytes, linked.offset, linked.size);
    span(bytes, section.offset, section.size);
    for (let at = section.offset; at + 24 <= section.offset + section.size; at += section.entry) {
      const binding = bytes[at + 4] >> 4, visibility = bytes[at + 5] & 3;
      if ((binding === 1 || binding === 2) && (visibility === 0 || visibility === 3) && bytes.readUInt16LE(at + 6))
        exports.push(cString(names, bytes.readUInt32LE(at)));
    }
  }
  const required = library === 'libdoom3.so' ? ['GetIDTechAPI'] : library === 'libgame.so' ? ['GetGameAPI'] :
    ['JNI_OnLoad', ...['start', 'surface', 'pause', 'resume', 'requestQuit', 'clearInput', 'key', 'analog', 'motion', 'stats', 'queryView', 'view']
      .map(x => `Java_org_wolf2009_engine_EngineNative_${x}`)];
  check(required.every(x => exports.includes(x)), `Missing ${library} entry point: ${required.filter(x => !exports.includes(x)).join(', ')}`);
  // QA code can remain hidden, so CMakeCache is also checked before accepting input libraries.
  check(!exports.some(x => /Wolf2009_Run(RenderChecks|OffscreenChecks|TrainyardAIShellQA)|GLimp_Wolf2009EnablePbuffer/.test(x)),
    'Runtime QA exports entered a production library');
  return { abi, library, bytes: bytes.length, sha256: sha256(bytes), machine, soname, needed, loads,
    requiredExports: required, elf16KiBAligned: true };
}

export function inspectProductionConfiguration(project, cache, abi) {
  check(abis.includes(abi), `Unsupported production ABI: ${abi}`);
  const expected = { ANDROID_ABI: abi, ANDROID_PLATFORM: 'android-26', ANDROID_STL: 'c++_static',
    CMAKE_BUILD_TYPE: 'Release', CORE: 'ON', BASE: 'ON', WOLF2009_STREAMS: 'ON', WOLF2009_RENDERER: 'ON',
    WOLF2009_GAMEPLAY: 'ON', WOLF2009_BOOTSTRAP: 'ON', WOLF2009_ANIMATION: 'ON',
    WOLF2009_RUNTIME_RENDER_QA: 'OFF', WOLF2009_RUNTIME_GL_QA: 'OFF', WOLF2009_BRAIN_QA: 'OFF',
    WOLF2009_SKINNED_QA: 'OFF', WOLF2009_ANIMATION_QA: 'OFF', WOLF2009_ANIMATION_V2_QA: 'OFF', WOLF2009_MESH_V2_QA: 'OFF' };
  // QA may live in hidden game/core code. Require every known switch and also
  // refuse future enabled QA switches; *_QA_SOURCE paths are not switches.
  for (const name of Object.keys(cache)) if (/^WOLF2009_.*_QA$/.test(name)) expected[name] = 'OFF';
  for (const [name, value] of Object.entries(expected)) check(cache[name] === value, `${abi}: ${name} must be ${value}, found ${cache[name]}`);
  const enginePath = resolve(project, 'engine/doom3/neo');
  check(resolve(cache.CMAKE_HOME_DIRECTORY || '') === enginePath, `${abi}: different engine source checkout`);
  return expected;
}

export function inspectProduction(project, directory, abi) {
  const cache = Object.fromEntries(readFileSync(join(directory, 'CMakeCache.txt'), 'utf8').split(/\r?\n/)
    .map(x => /^([^#/][^:]*):[^=]*=(.*)$/.exec(x)).filter(Boolean).map(x => [x[1], x[2]]));
  const configuration = inspectProductionConfiguration(project, cache, abi);
  return { directory: resolve(directory), configuration, cacheSha256: sha256(readFileSync(join(directory, 'CMakeCache.txt'))),
    native: libraries.slice(0, 2).map(library => ({ path: join(resolve(directory), library),
      ...inspectElf(readFileSync(join(directory, library)), abi, library) })) };
}

// APK v2/v3 signatures are not ZIP entries; the central directory still gives exact LFH offsets.
export function readZip(bytes) {
  let end = bytes.length - 22;
  while (end >= Math.max(0, bytes.length - 65557) && bytes.readUInt32LE(end) !== 0x06054b50) --end;
  check(end >= Math.max(0, bytes.length - 65557), 'ZIP end record missing');
  check(bytes.readUInt16LE(end + 4) === 0 && bytes.readUInt16LE(end + 6) === 0 &&
    end + 22 + bytes.readUInt16LE(end + 20) === bytes.length, 'Unsupported ZIP layout');
  const count = bytes.readUInt16LE(end + 10), size = bytes.readUInt32LE(end + 12), start = bytes.readUInt32LE(end + 16);
  check(count !== 65535 && start + size <= end, 'Unsupported ZIP64/central directory');
  let at = start;
  const entries = new Map();
  for (let i = 0; i < count; ++i) {
    const h = span(bytes, at, 46);
    check(h.readUInt32LE(0) === 0x02014b50, 'ZIP central header missing');
    const flags = h.readUInt16LE(8), method = h.readUInt16LE(10), compressed = h.readUInt32LE(20), uncompressed = h.readUInt32LE(24);
    const nameSize = h.readUInt16LE(28), extraSize = h.readUInt16LE(30), commentSize = h.readUInt16LE(32), local = h.readUInt32LE(42);
    const name = span(bytes, at + 46, nameSize).toString('utf8');
    check(name && !name.includes('\\') && !name.startsWith('/') && !name.split('/').some(x => x === '..' || x === '.') &&
      !entries.has(name) && !(flags & 1), `Unsafe or duplicate APK entry: ${name}`);
    const lh = span(bytes, local, 30);
    check(lh.readUInt32LE(0) === 0x04034b50 && lh.readUInt16LE(8) === method, 'ZIP local header mismatch');
    check(span(bytes, local + 30, lh.readUInt16LE(26)).toString('utf8') === name, 'ZIP local name mismatch');
    const dataOffset = local + 30 + lh.readUInt16LE(26) + lh.readUInt16LE(28);
    const packed = span(bytes, dataOffset, compressed);
    check(method === 0 || method === 8, `Unsupported ZIP compression: ${name}`);
    const data = method === 0 ? packed : inflateRawSync(packed, { maxOutputLength: uncompressed });
    check(data.length === uncompressed, `ZIP size mismatch: ${name}`);
    entries.set(name, { name, data, method, dataOffset });
    at += 46 + nameSize + extraSize + commentSize;
  }
  check(at === start + size, 'ZIP central directory size mismatch');
  return entries;
}

export function verifyApk(bytes, work) {
  const entries = readZip(bytes), expectedNative = abis.flatMap(abi => libraries.map(library => `lib/${abi}/${library}`));
  const actualNative = [...entries.keys()].filter(x => x.startsWith('lib/'));
  check(JSON.stringify(actualNative.sort()) === JSON.stringify(expectedNative.sort()), 'APK must contain exactly core, GPL game and engine host for both ABIs');
  const native = expectedNative.map(name => {
    const entry = entries.get(name), [, abi, library] = name.split('/');
    check(entry.method === 0 && entry.dataOffset % 16384 === 0, `APK native entry is not stored/16 KiB aligned: ${name}`);
    check(sha256(entry.data) === sha256(readFileSync(join(work, name))), `Packaged native bytes differ from stripped copy: ${name}`);
    return { entry: name, zipDataOffset: entry.dataOffset, ...inspectElf(entry.data, abi, library) };
  });
  const prefix = 'assets/engine-support/base/';
  const actualSupport = [...entries.keys()].filter(x => x.startsWith(prefix)).map(x => x.slice(prefix.length)).sort();
  check(JSON.stringify(actualSupport) === JSON.stringify(supportFiles), 'APK runtime support allowlist differs; maps/game assets are forbidden');
  const support = supportFiles.map(name => {
    const entry = prefix + name, data = entries.get(entry).data;
    check(sha256(data) === sha256(readFileSync(join(work, entry))), `Support file changed while packaging: ${name}`);
    return { entry, bytes: data.length, sha256: sha256(data) };
  });
  for (const name of entries.keys()) check(['AndroidManifest.xml', 'resources.arsc', 'classes.dex'].includes(name) ||
    name.startsWith('res/') || name.startsWith('META-INF/') || expectedNative.includes(name) ||
    name.startsWith('assets/licenses/') || name.startsWith(prefix), `Unexpected APK content: ${name}`);
  const dex = entries.get('classes.dex')?.data;
  check(dex?.subarray(0, 4).toString() === 'dex\n' && dex.includes(Buffer.from('Lorg/wolf2009/engine/EngineActivity;')),
    'Actual EngineActivity is missing from DEX');
  check(!dex.includes(Buffer.from('ViewerActivity')) && !dex.includes(Buffer.from('libwolf2009-viewer')),
    'Legacy viewer entered engine APK');
  return { bytes: bytes.length, sha256: sha256(bytes), native, support,
    zip16KiBAligned: true, gameAssetsIncluded: false, legacyViewerIncluded: false, runtimeTested: false };
}

export function sourceSnapshot(project) {
  const roots = ['android/engine', 'android/res', 'patches', 'src/wolf2009', 'src/idtech4a_boot',
    'src/idtech4a_gameplay', 'src/idtech4a_render', 'src/idtech4a_anim', 'third_party/bullet_narrowphase'];
  const files = [];
  function walk(directory) {
    for (const entry of readdirSync(directory, { withFileTypes: true })) {
      const path = join(directory, entry.name);
      if (entry.isDirectory()) walk(path);
      else if (entry.isFile()) files.push(path);
    }
  }
  for (const root of roots) walk(join(project, root));
  for (const name of ['LICENSE', 'tools/build-engine-apk.ps1', 'tools/lib/engine-apk.mjs', 'tools/prepare-engine.ps1',
    'tools/check-engine-patch.mjs', 'tools/prepare-engine-support.mjs', 'tools/prepare-q3e-host.ps1',
    'tools/prepare-source-session.mjs', 'tools/lib/packages.mjs', 'docs/PROVENANCE.json',
    'tests/engine-runtime/Build-EngineRuntime.ps1']) files.push(join(project, name));
  const sources = files.map(path => ({ path: relative(project, path).replaceAll('\\', '/'), sha256: sha256(readFileSync(path)) }))
    .sort((a, b) => a.path.localeCompare(b.path, 'en'));
  const git = (...args) => execFileSync('git', ['-C', project, ...args], { encoding: 'utf8' }).trim();
  return { revision: git('rev-parse', 'HEAD'), engineRevision: execFileSync('git', ['-C', join(project, 'engine'), 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    workingTreeStatus: git('status', '--porcelain=v1'), sourceSha256: sha256(JSON.stringify(sources)), files: sources };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [mode, ...args] = process.argv.slice(2);
  let result, output;
  if (mode === 'inputs' && args.length === 4) {
    const [project, arm, x86] = args; output = args[3];
    result = { builds: [inspectProduction(project, arm, abis[0]), inspectProduction(project, x86, abis[1])], source: sourceSnapshot(project) };
  } else if (mode === 'apk' && args.length === 3) {
    result = verifyApk(readFileSync(args[0]), args[1]); output = args[2];
  } else if (mode === 'snapshot' && args.length === 2) {
    result = sourceSnapshot(args[0]); output = args[1];
  } else throw Error('usage: engine-apk.mjs inputs <project> <arm build> <x86 build> <report> | apk <apk> <work> <report> | snapshot <project> <report>');
  writeFileSync(output, JSON.stringify(result, null, 2) + '\n');
  console.log(`${mode.toUpperCase()}_VERIFIED=${basename(output)}`);
}
