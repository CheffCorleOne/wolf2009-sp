// SPDX-License-Identifier: GPL-3.0-or-later
// Verify every vendored byte and reverse the documented local patch to the pin.
import {readFileSync, readdirSync, mkdirSync, mkdtempSync, cpSync, realpathSync, rmSync} from 'node:fs';
import {resolve, dirname, relative, sep} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
const project = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const vendor = resolve(project, 'third_party/bullet_narrowphase');
const pin = JSON.parse(readFileSync(resolve(vendor, 'UPSTREAM.json'), 'utf8'));
function need(value, message) { if (!value) throw Error(message); }
function inside(root, name) {
    need(typeof name === 'string' && /^[A-Za-z0-9_./-]+$/.test(name) &&
        name.split('/').every(p => p && p !== '.' && p !== '..'), `Invalid relative path: ${name}`);
    const result = resolve(root, name);
    need(result.startsWith(root + sep), `Path escaped vendor: ${name}`);
    return result;
}
const hash = data => createHash('sha256').update(data).digest('hex');
need(pin.commit === 'd3afe76132fc76b0f9b2359cbaef47a3766e7fbb', 'Unexpected Bullet revision');
need(pin.upstreamFiles.length === 53 && pin.translationUnits.length === 11, 'Unexpected source closure');
const changes = new Map();
for (const change of pin.localChanges) {
    inside(vendor, change.relative); inside(vendor, change.patch);
    need(!changes.has(change.relative), 'Duplicate local change');
    changes.set(change.relative, change);
}
need(changes.size === 2, 'Expected two reviewed local source changes');
const files = new Map();
for (const entry of pin.upstreamFiles) {
    need(!files.has(entry.relative), 'Duplicate upstream path');
    const bytes = readFileSync(inside(vendor, entry.relative));
    const changed = changes.get(entry.relative);
    need(hash(bytes) === (changed ? changed.sha256 : entry.sha256), `Changed vendor bytes: ${entry.relative}`);
    if (!changed) need(bytes.length === entry.bytes, `Changed vendor length: ${entry.relative}`);
    need(entry.url === `https://raw.githubusercontent.com/bulletphysics/bullet3/${pin.commit}/${entry.relative}`,
        `Unexpected upstream URL: ${entry.relative}`);
    files.set(entry.relative, entry);
}
for (const name of changes.keys()) need(files.has(name), 'Local change has no upstream provenance');
for (const name of pin.translationUnits) need(files.has(name) && name.endsWith('.cpp'), 'Unpinned translation unit');
function walk(root, prefix = '') {
    for (const entry of readdirSync(root, {withFileTypes: true})) {
        const name = prefix + entry.name;
        need(!entry.isSymbolicLink(), `Symlink in vendor source: ${name}`);
        if (entry.isDirectory()) walk(resolve(root, entry.name), name + '/');
        else need(files.has('src/' + name), `Unreviewed vendor source: ${name}`);
    }
}
walk(resolve(vendor, 'src'));
const build = resolve(project, 'build');
mkdirSync(build, {recursive: true});
const scratch = mkdtempSync(resolve(build, 'bullet-subset-verify-'));
function git(args) {
    const result = spawnSync('git', ['-c', 'core.autocrlf=false', '-c', 'core.safecrlf=false', '-C', scratch, ...args],
        {encoding: 'utf8', windowsHide: true});
    need(result.status === 0, `Patch verification failed: ${result.stderr || result.error || 'git exit ' + result.status}`);
}
try {
    for (const name of files.keys()) {
        const target = inside(scratch, name);
        mkdirSync(dirname(target), {recursive: true});
        cpSync(inside(vendor, name), target, {errorOnExist: true, force: false});
    }
    git(['init', '--quiet']);
    const patches = [...new Set([...changes.values()].map(c => c.patch))];
    for (const patch of patches.slice().reverse()) git(['apply', '--reverse', '--whitespace=nowarn', inside(vendor, patch)]);
    for (const [name, entry] of files) {
        const bytes = readFileSync(inside(scratch, name));
        need(bytes.length === entry.bytes && hash(bytes) === entry.sha256,
            `Patch does not recover pinned source: ${name} bytes=${bytes.length}/${entry.bytes} sha=${hash(bytes)}/${entry.sha256}`);
    }
    for (const patch of patches) git(['apply', '--whitespace=nowarn', inside(vendor, patch)]);
    for (const name of files.keys())
        need(hash(readFileSync(inside(scratch, name))) === hash(readFileSync(inside(vendor, name))), 'Patch round trip differs');
    console.log(`WOLF2009_BULLET_SUBSET_OK files=${files.size} localChanges=${changes.size} reverseAndForward=1`);
} finally {
    // This directory was created exclusively above. Validate its actual
    // absolute location before any recursive cleanup on Windows.
    const actual = realpathSync(scratch), base = realpathSync(build);
    need(actual.startsWith(base + sep) && relative(base, actual).startsWith('bullet-subset-verify-'),
        'Refusing cleanup outside the verification workspace');
    rmSync(actual, {recursive: true, force: false});
}
