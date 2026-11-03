// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic bootstrap resources, authored here; no Doom3/Wolf retail resources.
import {mkdirSync, writeFileSync, cpSync, existsSync} from 'node:fs';
import {resolve, dirname} from 'node:path';
const root = resolve(process.argv[2]);
function put(name, value) {
    const path = resolve(root, 'base', name);
    mkdirSync(dirname(path), {recursive: true});
    writeFileSync(path, value);
}
put('default.cfg', '// Synthetic GPL lifecycle fixture.\nseta s_noSound 1\n');
put('config.spec', '// Disable machine detection for a deterministic headless fixture.\n');
put('DoomConfig.cfg', '// Synthetic GPL lifecycle fixture.\n');
put('materials/bootstrap.mtr', `_default { { map _default } }
_white { { map _white } }
_tracemodel { solid }
lights/defaultPointLight { lightFalloffImage _quadratic { map _white } }
lights/defaultProjectedLight { { map _white } }
`);
put('script/doom_main.script', 'void doom_main() { }\n');
put('script/doom_defs.script', '// Independently authored bootstrap include.\n');
put('def/bootstrap.def', 'entityDef aas_types { }\n');
const gui = 'windowDef Desktop { rect 0, 0, 640, 480 visible 1 }\n';
for (const name of ['mainmenu', 'restart', 'gameover', 'msg', 'takeNotes', 'intro', 'loading'])
    put(`guis/${name}.gui`, gui);
// Native old-font wire format is 256 glyphs of 80 bytes + float scale + name[64].
// Each glyph uses the built-in white texture; no external font asset is needed.
for (const size of [12, 24, 48]) {
    const font = Buffer.alloc(20548);
    for (let i = 0; i < 256; ++i) {
        const off = i * 80;
        for (const n of [0, 4, 16, 20, 24]) font.writeInt32LE(8, off + n);
        font.writeFloatLE(1, off + 36);
        font.writeFloatLE(1, off + 40);
        font.write('fonts/_white', off + 48, 'ascii');
    }
    font.writeFloatLE(48 / size, 20480);
    put(`fonts/english/fontImage_${size}.dat`, font);
}
if (process.argv[3]) {
    const gameplay = resolve(process.argv[3]);
    if (!existsSync(gameplay)) throw Error('missing optional independently authored gameplay fixture');
    cpSync(gameplay, resolve(root, 'base'), {recursive: true});
    if (existsSync(resolve(root, 'base/maps/wolf2009/runtime.map'))) {
        // Independently authored visual floor and one BSP area for this fixture.
        // Collision still comes from the actual generated .map via the engine.
        put('maps/wolf2009/runtime.proc', `mapProcFile003
model { "_area0" 1
    { "_default" 4 6
      ( -256 -256 0 0 0 0 0 1 )
      (  256 -256 0 1 0 0 0 1 )
      (  256  256 0 1 1 0 0 1 )
      ( -256  256 0 0 1 0 0 1 )
      0 1 2 0 2 3
    }
}
interAreaPortals { 1 0 }
nodes { 1 ( 1 0 0 0 ) -1 -1 }
`);
    }
}
console.log('WOLF2009_RUNTIME_SYNTHETIC_FIXTURE=' + root);
