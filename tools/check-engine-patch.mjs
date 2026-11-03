// SPDX-License-Identifier: GPL-3.0-or-later
// Validate the complete ordered chain in a separate scratch repository.
// --prepare upgrades only exact previously known source states; it never
// resets the active index, checkout, or unrelated engine files.
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, existsSync, renameSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { resolve, join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const engine = join(root, 'engine');
const prepare = process.argv.includes('--prepare');
if (process.argv.slice(2).some(arg => arg !== '--prepare'))
  throw Error('usage: node tools/check-engine-patch.mjs [--prepare]');
const build = join(root, 'build'); mkdirSync(build, { recursive: true });
const scratch = mkdtempSync(join(build, 'patch-check-'));
const provenance = JSON.parse(readFileSync(join(root, 'docs/PROVENANCE.json'), 'utf8')).engine;
const patchNames = [
  provenance.patch, provenance.streamPatch, provenance.soundPatch,
  'patches/idtech4a-wolf2009-renderer.patch',
  'patches/idtech4a-wolf2009-gameplay.patch',
  'patches/idtech4a-wolf2009-bootstrap.patch',
  'patches/idtech4a-wolf2009-offscreen-qa.patch',
  'patches/idtech4a-wolf2009-gles3-attachments.patch',
  'patches/idtech4a-wolf2009-solid-zfill.patch',
  'patches/idtech4a-wolf2009-materials-command.patch',
  'patches/idtech4a-wolf2009-android-exit.patch',
  'patches/idtech4a-wolf2009-vertex-color-uniform.patch',
  provenance.roundedCollisionPatch,
];
const patches = patchNames.map(path => join(root, path));
const files = new Set();
for (const patch of patches) {
  const source = readFileSync(patch, 'utf8');
  for (const match of source.matchAll(/^diff --git a\/(\S+) b\/(\S+)$/gm)) {
    const file = match[1];
    if (file !== match[2] || !file.startsWith('doom3/neo/') ||
        file.split('/').some(part => part === '..' || part === '.'))
      throw Error('Unexpected patch path: ' + file);
    files.add(file);
  }
}
function runGit(args, cwd, allowMissing = false) {
  const result = spawnSync('git', args, { cwd, encoding: null, maxBuffer: 64 * 1024 * 1024 });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    if (allowMissing) return null;
    throw Error(result.stderr.toString() || 'git failed: ' + args.join(' '));
  }
  return result.stdout;
}
const actual = runGit(['rev-parse', 'HEAD'], engine).toString().trim();
if (actual !== provenance.commit) throw Error('Engine HEAD differs from the pinned baseline');
// A distinct repository prevents Git from silently skipping paths because this
// directory happens to be nested below the outer project checkout.
runGit(['init', '--quiet'], scratch);
for (const file of files) {
  const object = provenance.commit + ':' + file;
  if (runGit(['cat-file', '-e', object], engine, true) === null) continue;
  const dest = join(scratch, file); mkdirSync(dirname(dest), { recursive: true });
  writeFileSync(dest, runGit(['show', object], engine));
}
const normalized = bytes => bytes === null ? null : bytes.toString('utf8').replace(/\r\n/g, '\n');
const read = path => existsSync(path) ? readFileSync(path) : null;
const digest = bytes => bytes === null ? '<absent>' :
  createHash('sha256').update(normalized(bytes)).digest('hex');
const known = new Map([...files].map(file => [file, new Set()]));
function snapshot() {
  for (const file of files) known.get(file).add(digest(read(join(scratch, file))));
}
snapshot();
for (let i = 0; i < patches.length; ++i) {
  runGit(['apply', '--check', patches[i]], scratch);
  runGit(['apply', patches[i]], scratch);
  snapshot();
  if (i === 1) {
    // Accept only the exact older stream bridge that the previous published
    // release generated. Validate its migration before applying later features.
    const migration = join(root, provenance.streamUpgradePatch);
    const bridge = 'doom3/neo/framework/Wolf2009Streams.cpp';
    runGit(['apply', '--reverse', migration], scratch);
    const previous = runGit(['hash-object', '--path=' + bridge, bridge], scratch).toString().trim();
    if (previous !== '1eaee0b8de8f954c9303cbc2a252c7e969c9d1f2')
      throw Error('Unexpected stream migration baseline');
    known.get(bridge).add(digest(read(join(scratch, bridge))));
    runGit(['apply', migration], scratch);
  }
}
const changes = [];
const conflicts = [];
for (const file of files) {
  const current = read(join(engine, file)), expected = read(join(scratch, file));
  const currentDigest = digest(current), expectedDigest = digest(expected);
  if (currentDigest === expectedDigest) continue;
  if (!prepare || !known.get(file).has(currentDigest)) conflicts.push(file);
  else changes.push({ file, currentDigest, expected });
}
if (conflicts.length) throw Error(
  (prepare ? 'Preserve local engine edits before preparing the patch chain: ' :
             'Patched source differs from the complete tested chain: ') + conflicts.join(', '));
// Complete preflight above means a conflicting user edit cannot trigger a
// partial upgrade. Recheck each file immediately before its atomic replacement.
for (const change of changes) {
  const target = join(engine, change.file);
  if (digest(read(target)) !== change.currentDigest)
    throw Error('Engine source changed while preparing: ' + change.file);
  if (change.expected === null) throw Error('Source deletion is unsupported by this preparation helper');
}
for (const change of changes) {
  const target = join(engine, change.file);
  if (digest(read(target)) !== change.currentDigest)
    throw Error('Engine source changed before replacement: ' + change.file);
  mkdirSync(dirname(target), { recursive: true });
  const temporary = target + '.wolf2009-prepare-' + process.pid;
  writeFileSync(temporary, change.expected, { flag: 'wx' });
  renameSync(temporary, target);
}
console.log('All ' + patches.length + ' patches apply in order to ' + provenance.commit +
  '; ' + files.size + ' source files match the prepared checkout.' +
  (prepare ? ' Updated ' + changes.length + ' known files.' : ''));
