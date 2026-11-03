// SPDX-License-Identifier: GPL-3.0-or-later
// Independently authored diagnostics; no original BRAI/resource bytes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';

function node(type, id, fields = []) {
  return `${type}|${id}|0|0|0|^${type}|0|${fields.map(([a,b]) => `${a}|${b}|`).join('')}\n`;
}
function output(target, condition = true) {
  return '\t' + Array.from({length:99}, (_,i) =>
    i === 0 ? '16' : i === 1 ? '1' : i === 2 ? String(target) : i === 98 ? '1.000000' : '0'
  ).join('|') + '|\n' + (condition ? '\t\t-1|\n' : '');
}
function graph(body) { return Buffer.from(('h1023|32|32|32|0|\n' + body).replaceAll('|','\0')); }
const wait = (id, next) => node(30,id,[['^65','1024'],['mDuration','32 0'],['mDurationMax','0 0'],
  ['mActualDuration','0'],['mTimeSet','0']]) + output(next);
export function brainFixtures() {
  const spawn = node(1,1,[['^74','1024']]);
  const set = (id,name,value) => node(220,id,[['mVar',name],['mExpression',value]]);
  const sender = spawn + output(2) + wait(2,3) +
    node(68,3,[['mActor','brain_actor'],['mUseTarget','brain_receiver'],['mUseKey','deliver']]) +
    output(4) + set(4,'sender_done','1');
  const receiver = node(3,1,[['^64','1024'],['mUseKey','deliver']]) + output(2) + wait(2,3) +
    set(3,'received','7');
  const replacement = node(3,1,[['^64','1024'],['mUseKey','deliver']]) + output(2) + set(2,'received','9');
  const writer = spawn + output(2) + set(2,'received','99');
  return new Map([
    ['wolf2009-brain-qa/own/sender.brai',graph(sender)],
    ['wolf2009-brain-qa/own/receiver.brai',graph(receiver)],
    ['wolf2009-brain-qa/own/replacement.brai',graph(replacement)],
    ['wolf2009-brain-qa/own/writer.brai',graph(writer)],
    ['wolf2009-brain-qa/own/bad-expression.brai',graph(spawn + output(2) + set(2,'received','1 + 2'))],
    ['wolf2009-brain-qa/own/bad-field.brai',graph(spawn + output(2) +
      node(220,2,[['mVar','received'],['mExpression','1'],['unknownField','1']]))],
    ['wolf2009-brain-qa/own/bad-unreachable.brai',graph(writer + node(999,3))],
    ['wolf2009-brain-qa/own/bad-receiver.brai',graph(spawn + output(2) +
      node(68,2,[['mUseTarget','brain_plain_actor'],['mUseKey','deliver']]))],
    ['wolf2009-brain-qa/own/defaults.txt',Buffer.from('wolf2009-brain-globals-v1\nreceived=0\nsender_done=0\n')],
    ['def/wolf2009_brain_qa.def',Buffer.from(`// Independently authored native Brain diagnostic entities.\nentityDef wolf2009_brain_task {\n "spawnclass" "idWolfBrainTask"\n "wolf_brain_mount" "own"\n "wolf_brain_diagnostic" "1"\n}\nentityDef wolf2009_brain_globals {\n "spawnclass" "idWolfBrainGlobals"\n "wolf_brain_mount" "own"\n "wolf_brain_diagnostic" "1"\n "wolf_brain_clock" "diagnostic-ms"\n "wolf_brain_defaults" "wolf2009-brain-qa/own/defaults.txt"\n}\nentityDef wolf2009_brain_plain_actor {\n "spawnclass" "idWolf2009Static"\n}\n`)]
  ]);
}
export function prepareBrainFixture(baseDirectory) {
  const root = path.resolve(baseDirectory);
  const files = [];
  for (const [relative,bytes] of brainFixtures()) {
    const target = path.resolve(root,relative);
    if (!target.startsWith(root + path.sep)) throw new Error('Fixture path containment');
    fs.mkdirSync(path.dirname(target),{recursive:true});
    fs.writeFileSync(target,bytes);
    files.push({path:relative,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')});
  }
  return {scope:'Independently authored diagnostic-ms Brain host fixture; no retail assets',base:root,files};
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (process.argv.length !== 4 || process.argv[2] !== '--output') throw new Error('Usage: --output <fixture base directory>');
  process.stdout.write(JSON.stringify(prepareBrainFixture(process.argv[3]),null,2) + '\n');
}
