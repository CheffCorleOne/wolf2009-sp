// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise preparation in disposable fixture repositories. The active engine,
// its index and its local modifications are only read, never reset or patched.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, copyFileSync, existsSync, statSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const active = join(root, 'engine');
const provenance = JSON.parse(readFileSync(join(root, 'docs/PROVENANCE.json'), 'utf8')).engine;
const patchNames = [
  provenance.patch, provenance.streamPatch, provenance.soundPatch,
  'patches/idtech4a-wolf2009-renderer.patch', 'patches/idtech4a-wolf2009-gameplay.patch',
  'patches/idtech4a-wolf2009-bootstrap.patch',
  'patches/idtech4a-wolf2009-offscreen-qa.patch',
  'patches/idtech4a-wolf2009-gles3-attachments.patch',
  'patches/idtech4a-wolf2009-solid-zfill.patch',
  'patches/idtech4a-wolf2009-materials-command.patch',
  'patches/idtech4a-wolf2009-android-exit.patch',
  'patches/idtech4a-wolf2009-vertex-color-uniform.patch',
  provenance.roundedCollisionPatch,
  provenance.skinnedModelPatch, provenance.sourceSupportPatch, provenance.sourceAnimationPatch,
  provenance.skinnedMeshV2Patch,
  provenance.sourceAnimationV2Patch,
  provenance.sourceAnimationSetsPatch,
  provenance.sourceActorBindingPatch,
];
function git(cwd, args, absent = false) {
  const result = spawnSync('git', args, { cwd, encoding: null, maxBuffer: 64 * 1024 * 1024 });
  assert.ifError(result.error);
  if (absent && result.status !== 0) return null;
  assert.equal(result.status, 0, result.stderr.toString());
  return result.stdout;
}
function fixture(prefix = 0, legacy = false) {
  const path = mkdtempSync(join(root, 'build/patch-chain-test-'));
  for (const folder of ['tools', 'docs', 'patches', 'engine']) mkdirSync(join(path, folder));
  copyFileSync(join(root, 'tools/check-engine-patch.mjs'), join(path, 'tools/check-engine-patch.mjs'));
  copyFileSync(join(root, 'docs/PROVENANCE.json'), join(path, 'docs/PROVENANCE.json'));
  for (const name of [...patchNames, provenance.streamUpgradePatch]) {
    copyFileSync(join(root, name), join(path, name));
  }
  const engine = join(path, 'engine');
  git(engine, ['init', '--quiet']);
  // A read-only alternates reference reuses the already present pinned objects.
  // It cannot change the active checkout, refs, index, worktree or object data.
  const objects = resolve(active, git(active, ['rev-parse', '--git-path', 'objects']).toString().trim());
  mkdirSync(join(engine, '.git/objects/info'), { recursive: true });
  writeFileSync(join(engine, '.git/objects/info/alternates'), objects.replaceAll('\\', '/') + '\n');
  git(engine, ['update-ref', 'HEAD', provenance.commit]);
  const files = new Set();
  for (const patch of patchNames) {
    const source = readFileSync(join(path, patch), 'utf8');
    for (const match of source.matchAll(/^diff --git a\/(\S+) b\/(\S+)$/gm)) files.add(match[1]);
  }
  for (const file of files) {
    const object = provenance.commit + ':' + file;
    if (git(active, ['cat-file', '-e', object], true) === null) continue;
    const target = join(engine, file); mkdirSync(dirname(target), { recursive: true });
    writeFileSync(target, git(active, ['show', object]));
  }
  for (let i = 0; i < prefix; ++i) git(engine, ['apply', join(path, patchNames[i])]);
  if (legacy) git(engine, ['apply', '--reverse', join(path, provenance.streamUpgradePatch)]);
  return { path, engine };
}
function check(value, prepare = false) {
  return spawnSync(process.execPath, [join(value.path, 'tools/check-engine-patch.mjs'), ...(prepare ? ['--prepare'] : [])],
    { cwd: value.path, encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 });
}
test('clean pinned baseline upgrades to all features and preparation is idempotent', () => {
  const value = fixture();
  let result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /All 20 patches apply/);
  const file = join(value.engine, 'doom3/neo/CMakeLists.txt');
  const before = readFileSync(file), modified = statSync(file).mtimeMs;
  result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 0 known files/);
  assert.deepEqual(readFileSync(file), before);
  assert.equal(statSync(file).mtimeMs, modified);
  result = check(value);
  assert.equal(result.status, 0, result.stderr);
});
test('previous archive release and exact old stream bridge migrate without a reset', () => {
  const value = fixture(3, true);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(check(value).status, 0);
  assert.match(readFileSync(join(value.engine, 'doom3/neo/framework/Wolf2009Streams.cpp'), 'utf8'),
    /Wolf2009_OpenGameplayMap/);
});
test('published nine-patch baseline upgrades materials, exit, colour and rounded collision', () => {
  const value = fixture(9);
  const manager = join(value.engine, 'doom3/neo/renderer/ModelManager.cpp');
  const cmake = join(value.engine, 'doom3/neo/CMakeLists.txt');
  const before = readFileSync(cmake);
  assert.doesNotMatch(readFileSync(manager, 'utf8'), /Wolf2009_InitRenderCommands/);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 19 known files/);
  assert.match(readFileSync(manager, 'utf8'), /Wolf2009_InitRenderCommands/);
  assert.notDeepEqual(readFileSync(cmake), before);
  assert.match(readFileSync(cmake, 'utf8'), /list\(APPEND d3_compile_definitions WOLF2009_GAMEPLAY\)/);
  assert.equal(check(value).status, 0);
});
test('published ten-patch baseline upgrades exit, colour and rounded collision', () => {
  const value = fixture(10);
  const cmake = join(value.engine, 'doom3/neo/CMakeLists.txt');
  const before = readFileSync(cmake);
  const exit = join(value.engine, 'doom3/neo/sys/posix/posix_main.cpp');
  assert.doesNotMatch(readFileSync(exit, 'utf8'), /WOLF2009_ANDROID_PROCESS_EXIT/);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 19 known files/);
  assert.match(readFileSync(exit, 'utf8'), /WOLF2009_ANDROID_PROCESS_EXIT/);
  assert.notDeepEqual(readFileSync(cmake), before);
  assert.equal(check(value).status, 0);
});
test('published eleven-patch baseline upgrades scalar colour and rounded collision', () => {
  const value = fixture(11);
  const file = join(value.engine, 'doom3/neo/renderer/draw_common.cpp');
  const cmake = join(value.engine, 'doom3/neo/CMakeLists.txt');
  const before = readFileSync(cmake);
  assert.match(readFileSync(file, 'utf8'), /GL_Uniform4fv\([^\n]*colorModulate/);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 18 known files/);
  assert.doesNotMatch(readFileSync(file, 'utf8'), /GL_Uniform4fv\([^\n]*colorModulate/);
  assert.notDeepEqual(readFileSync(cmake), before);
  assert.equal(check(value).status, 0);
});

test('published twelve-patch baseline upgrades rounded, skinned and source-world hooks', () => {
  const value = fixture(12);
  const model = join(value.engine, 'doom3/neo/cm/CollisionModel_translate.cpp');
  assert.doesNotMatch(readFileSync(model, 'utf8'), /Wolf2009_CMTranslation/);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 17 known files/);
  assert.match(readFileSync(model, 'utf8'), /Wolf2009_CMTranslation/);
  assert.equal(check(value).status, 0);
});

test('published rounded-collision baseline upgrades skinned models and source-map lifetime', () => {
  const value = fixture(13);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 10 known files/);
  assert.match(readFileSync(join(value.engine, 'doom3/neo/renderer/Model_local.h'), 'utf8'), /ImportWolf2009/);
  assert.match(readFileSync(join(value.engine, 'doom3/neo/framework/Wolf2009Streams.cpp'), 'utf8'), /Wolf2009_PinMount/);
  assert.equal(check(value).status, 0);
});

test('skinned-model baseline upgrades only source-map and lease integration', () => {
  const value = fixture(14);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 9 known files/);
  assert.match(readFileSync(join(value.engine, 'doom3/neo/cm/CollisionModel_load.cpp'), 'utf8'), /Wolf2009_CMBeginSourceMap/);
  assert.equal(check(value).status, 0);
});
test('source-world baseline upgrades native source animators and descriptor ownership', () => {
  const value = fixture(15);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 7 known files/);
  assert.match(readFileSync(join(value.engine, 'doom3/neo/game/anim/Anim.cpp'), 'utf8'), /GetPoseAtTime/);
  assert.match(readFileSync(join(value.engine, 'doom3/neo/framework/Wolf2009Streams.cpp'), 'utf8'), /Wolf2009_OpenSourceAnimation/);
  assert.equal(check(value).status, 0);
});
test('source-animation baseline upgrades mesh preparation and source frame access', () => {
  const value = fixture(16);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 5 known files/);
  assert.match(readFileSync(join(value.engine, 'doom3/neo/renderer/Model_local.h'), 'utf8'), /PreparedSkinnedGeometry/);
  assert.equal(check(value).status, 0);
});

test('mesh-v2 baseline adds source frame access and controlled hold hooks', () => {
  const value = fixture(17);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 4 known files/);
  assert.match(readFileSync(join(value.engine, 'doom3/neo/game/anim/Anim.cpp'), 'utf8'), /SourceCycleFrameCoordinate\(time, cyclecount, completedCycles\)/);
  assert.equal(check(value).status, 0);
});

test('animation-v2 baseline adds fixed-frame playback without changing legacy save wire', () => {
  const value = fixture(18);
  const result = check(value, true);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Updated 4 known files/);
  const blend = readFileSync(join(value.engine, 'doom3/neo/game/anim/Anim_Blend.cpp'), 'utf8');
  assert.match(blend, /Wolf2009_SourceFixedFrameAllowed/);
  assert.match(blend, /ReadShort\(legacyFrame\)/);
  assert.match(blend, /WriteShort\(static_cast<short>\(frame\)\)/);
  assert.equal(check(value).status, 0);
});

test('a local edit rejects preparation before any engine source is changed', () => {
  const value = fixture();
  const edited = join(value.engine, 'doom3/neo/renderer/Model.cpp');
  const untouched = join(value.engine, 'doom3/neo/framework/FileSystem.cpp');
  const before = readFileSync(untouched);
  const custom = Buffer.concat([readFileSync(edited), Buffer.from('\n// owner local change must survive\n')]);
  writeFileSync(edited, custom);
  const result = check(value, true);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /Preserve local engine edits/);
  assert.deepEqual(readFileSync(edited), custom);
  assert.deepEqual(readFileSync(untouched), before);
  assert.equal(existsSync(join(value.engine, 'doom3/neo/framework/Wolf2009Streams.cpp')), false);
});
