// SPDX-License-Identifier: GPL-3.0-or-later
// Generate only GPL runtime support; original game archives are user inputs.
import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from 'node:fs';
import { dirname, isAbsolute, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const project = resolve(dirname(fileURLToPath(import.meta.url)), '..');
if (process.argv.length !== 3) throw Error('usage: node tools/prepare-engine-support.mjs <new base output directory under build/>');
const output = resolve(process.argv[2]);
const build = join(project, 'build');
mkdirSync(build, { recursive: true });
const baseReal = realpathSync(build);
const projectReal = realpathSync(project);
const inside = (parent, child) => {
  const part = relative(parent, child);
  return part !== '' && !isAbsolute(part) && part !== '..' && !part.startsWith('..' + sep);
};
if (!inside(projectReal, baseReal))
  throw Error('Project build/ resolves outside the source checkout');
if (!inside(build, output) || existsSync(output))
  throw Error('Output must be a new directory inside the project build/ directory');
let existing = dirname(output);
while (!existsSync(existing)) existing = dirname(existing);
const existingReal = realpathSync(existing);
if (existingReal !== baseReal && !inside(baseReal, existingReal))
  throw Error('Output parent resolves outside the project build/ directory');
mkdirSync(output, { recursive: true });

function put(name, bytes) {
  const path = join(output, name);
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, bytes, { flag: 'wx' });
}
const ownBase = join(project, 'src/idtech4a_gameplay/assets/base');
for (const name of [
  'def/wolf2009_runtime.def',
  'script/doom_main.script',
  'materials/wolf2009_collision.mtr',
  'models/wolf2009/runtime_player.md5mesh',
]) put(name, readFileSync(join(ownBase, name)));

put('default.cfg', `// GPL compatibility support. Original game assets are imported separately.
seta s_noSound 1
seta r_multithread 0
seta g_showHud 0
bind "w" "_forward"
bind "s" "_back"
bind "a" "_moveleft"
bind "d" "_moveright"
bind "SPACE" "_moveUp"
bind "SHIFT" "_speed"
`);
put('config.spec', '// Platform setup is supplied by the Android host.\n');
put('DoomConfig.cfg', '// Private configuration for the native engine process.\n');
put('script/doom_defs.script', '// Independently authored bootstrap include.\n');
put('def/bootstrap.def', 'entityDef aas_types { }\n');
put('materials/bootstrap.mtr', `_default { { map _default } }
_white { { map _white } }
fonts/_white { { map _white } }
_tracemodel { solid }
lights/defaultPointLight { lightFalloffImage _quadratic { map _white } }
lights/defaultProjectedLight { { map _white } }
`);
const emptyGui = 'windowDef Desktop { rect 0, 0, 640, 480 visible 1 }\n';
for (const name of ['mainmenu', 'restart', 'gameover', 'msg', 'takeNotes', 'intro', 'loading', 'pda'])
  put(`guis/${name}.gui`, emptyGui);
// The engine requires valid old-format font metadata before loading a map.
// This uses its built-in white image and supplies no recreated Wolf HUD/font.
for (const size of [12, 24, 48]) {
  const font = Buffer.alloc(20548);
  for (let glyph = 0; glyph < 256; ++glyph) {
    const at = glyph * 80;
    for (const field of [0, 4, 16, 20, 24]) font.writeInt32LE(8, at + field);
    font.writeFloatLE(1, at + 36);
    font.writeFloatLE(1, at + 40);
    font.write('fonts/_white', at + 48, 'ascii');
  }
  font.writeFloatLE(48 / size, 20480);
  put(`fonts/english/fontImage_${size}.dat`, font);
}
console.log('WOLF2009_ENGINE_SUPPORT=' + output);
console.log('Original game archives and synthetic test maps are not bundled.');
