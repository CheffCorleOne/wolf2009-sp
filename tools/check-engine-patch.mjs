import { readFileSync, writeFileSync, mkdirSync, mkdtempSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve, join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const build = join(root, 'build'); mkdirSync(build, { recursive: true });
const scratch = mkdtempSync(join(build, 'patch-check-'));
const provenance = JSON.parse(readFileSync(join(root, 'docs/PROVENANCE.json'), 'utf8')).engine;
const files = [
  'doom3/neo/framework/Unzip.cpp', 'doom3/neo/externlibs/minizip/unzip.cpp',
  'doom3/neo/CMakeLists.txt', 'doom3/neo/framework/FileSystem.cpp',
  'doom3/neo/sound/snd_mp3.cpp',
];
const added = [
  'doom3/neo/framework/Wolf2009Zip.h',
  'doom3/neo/framework/Wolf2009Streams.h', 'doom3/neo/framework/Wolf2009Streams.cpp',
];
function git(args, cwd) {
  const result = spawnSync('git', args, { cwd, encoding: null });
  if (result.error) throw result.error;
  if (result.status !== 0) throw Error(result.stderr.toString());
  return result.stdout;
}
for (const file of files) {
  const dest = join(scratch, file); mkdirSync(dirname(dest), { recursive: true });
  writeFileSync(dest, git(['show', `${provenance.commit}:${file}`], join(root, 'engine')));
}
const patches = [provenance.patch, provenance.streamPatch, provenance.soundPatch].map(path => join(root, path));
// A distinct repo prevents Git from treating this nested scratch directory as
// a prefix within the outer project and silently skipping all patch paths.
git(['init', '--quiet'], scratch);
git(['apply', '--check', ...patches], scratch);
git(['apply', ...patches], scratch);
// Existing checkouts can migrate the exact previous stream source in place.
const migration = join(root, provenance.streamUpgradePatch);
const bridge = 'doom3/neo/framework/Wolf2009Streams.cpp';
git(['apply', '--reverse', migration], scratch);
const previousBlob = git(['hash-object', '--path=' + bridge, bridge], scratch).toString().trim();
if (previousBlob !== '1eaee0b8de8f954c9303cbc2a252c7e969c9d1f2') throw Error('Unexpected migration baseline');
git(['apply', '--reverse', '--check', '--exclude=' + bridge, patches[1]], scratch);
git(['apply', migration], scratch);
const normalized = path => readFileSync(path, 'utf8').replace(/\r\n/g, '\n');
for (const file of [...files, ...added]) {
  if (normalized(join(scratch, file)) !== normalized(join(root, 'engine', file)))
    throw Error(`Patched source differs from tested engine checkout: ${file}`);
}
console.log(`All ${patches.length} patches apply to ${provenance.commit}; all ${files.length + added.length} files match the prepared checkout.`);
