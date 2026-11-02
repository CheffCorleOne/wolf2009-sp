import { readFileSync, writeFileSync, mkdirSync, mkdtempSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve, join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const build = join(root, 'build'); mkdirSync(build, { recursive: true });
const scratch = mkdtempSync(join(build, 'patch-check-'));
const files = ['doom3/neo/framework/Unzip.cpp', 'doom3/neo/externlibs/minizip/unzip.cpp'];
function git(args, cwd) {
  const result = spawnSync('git', args, { cwd, encoding: null });
  if (result.error) throw result.error;
  if (result.status !== 0) throw Error(result.stderr.toString());
  return result.stdout;
}
for (const file of files) {
  const dest = join(scratch, file); mkdirSync(dirname(dest), { recursive: true });
  writeFileSync(dest, git(['show', `HEAD:${file}`], join(root, 'engine')));
}
const patch = join(root, 'patches/idtech4a-wolf2009-pk4.patch');
// A distinct repo prevents Git from treating this nested scratch directory as
// a prefix within the outer project and silently skipping all patch paths.
git(['init', '--quiet'], scratch);
git(['apply', '--check', patch], scratch);
git(['apply', patch], scratch);
const normalized = path => readFileSync(path, 'utf8').replace(/\r\n/g, '\n');
for (const file of [...files, 'doom3/neo/framework/Wolf2009Zip.h']) {
  if (normalized(join(scratch, file)) !== normalized(join(root, 'engine', file)))
    throw Error(`Patched source differs from tested engine checkout: ${file}`);
}
console.log('Patch applies to pinned originals; all three sources match the tested checkout.');
