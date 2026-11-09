// SPDX-License-Identifier: GPL-3.0-or-later
// Local native trainyard staging only. Commercial inputs never enter the APK.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {zipDirectory,zipMember} from './lib/packages.mjs';

const project=fs.realpathSync(path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'));
const required=[
  ['assets.pk4','fixtures/wolf-materials.bin'],
  ['maps/game/empty/empty.mpk','maps/game/empty/empty.mpk'],
  ['maps/game/trainyard/trainyard.mpk','maps/game/trainyard/trainyard.mpk'],
  ['maps/game/global/global.spk','maps/game/global/global.spk'],
  ['maps/game/trainyard/trainyard_vo_english.spk','maps/game/trainyard/trainyard_vo_english.spk'],
  ['streampacks/mp40.spk','streampacks/mp40.spk']
];
const aiPacks=['allies_kreisau_agt01','allies_kreisau_agt02','wehrmacht_infantry_mp40']
  .map(name=>`streampacks/${name}.spk`);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
function need(ok,why){if(!ok)throw Error(why);}
function within(root,target){const relative=path.relative(root,target);return relative!==''&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);}
function ownerBase(gameRoot){
  const root=path.resolve(gameRoot),candidates=[root,path.join(root,'base'),path.join(root,'SP/base')];
  const found=candidates.filter(candidate=>fs.existsSync(path.join(candidate,'assets.pk4'))&&fs.existsSync(path.join(candidate,'maps/game/empty/empty.mpk')));
  need(found.length===1,'Select one owner SP/base containing assets.pk4 and empty.mpk');return fs.realpathSync(found[0]);
}
function newPrivateRoot(output){
  const requested=path.resolve(output);need(!fs.existsSync(requested),'Private source stage already exists; choose a new directory');
  let ancestor=path.dirname(requested);while(!fs.existsSync(ancestor)){const next=path.dirname(ancestor);need(next!==ancestor,'No existing private stage parent');ancestor=next;}
  const actual=path.resolve(fs.realpathSync(ancestor),path.relative(ancestor,requested));
  need(within(project,actual)&&['build','evidence'].some(name=>within(path.join(project,name),actual)),
    'Commercial source staging must stay in a new project build/ or evidence/ directory');
  return actual;
}
function writeNew(root,relative,bytes){const target=path.join(root,relative);need(within(root,target),'Private source output escaped its root');
  fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});}
function copySupport(source,destination){
  need(fs.statSync(path.join(source,'def/wolf2009_runtime.def')).isFile(),'Missing independently authored engine support');
  need(!fs.existsSync(path.join(source,'maps')),'APK support must not contain maps');let files=0,bytes=0;
  function copy(directory,relative=''){
    for(const entry of fs.readdirSync(directory,{withFileTypes:true})){
      need(!entry.isSymbolicLink(),'Engine support symlinks are unsupported');const next=path.join(relative,entry.name),input=path.join(directory,entry.name);
      if(entry.isDirectory()){copy(input,next);continue;}
      need(entry.isFile()&&!entry.name.endsWith('.enabled'),'Engine support cannot contain automatic QA flags or special files');
      const info=fs.statSync(input);bytes+=info.size;need(++files<=256&&bytes<=16*1024*1024,'Engine support exceeds the existing APK import budget');
      const target=path.join(destination,next);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(input,target,fs.constants.COPYFILE_EXCL);
    }
  }
  copy(source);return {files,bytes};
}
export function prepareNativeTrainyardSource({gameRoot,supportBase,output,profile}){
  need(profile===undefined||profile==='SourceAIShellV1','Unknown trainyard source profile');
  const ai=profile==='SourceAIShellV1';
  const inputs=ai?[...required,...aiPacks.map(member=>[member,member])]:required;
  const owner=ownerBase(gameRoot),support=fs.realpathSync(path.resolve(supportBase)),root=newPrivateRoot(output);
  need(!within(owner,root)&&root!==owner,'Original game data is read-only');
  need(root!==support&&!within(support,root),'Private source stage cannot be inside the support input');
  for(const [relative] of inputs){const file=path.join(owner,relative);need(fs.existsSync(file)&&fs.statSync(file).isFile(),`Missing owner input ${relative}`);}
  const archiveBytes=fs.readFileSync(path.join(owner,'assets.pk4'));need(archiveBytes.length>0&&archiveBytes.length<=64*1024*1024,'Owner material archive byte budget');
  const archive=zipDirectory(archiveBytes),extracts=[];
  for(const [member,target] of [
    ['maps/game/global/global.decls','fixtures/source-session-global.decls'],
    ['maps/game/global/global_vo_english.decls','fixtures/source-session-global-locale.decls']
  ]){
    const matches=archive.entries.filter(entry=>entry.name.replaceAll('\\','/').toLowerCase()===member);
    need(matches.length===1,`Exact compiled global member absent/ambiguous: ${member}`);
    const data=zipMember(archiveBytes,matches[0],16*1024*1024);need(data.length>0,`Empty compiled global member ${member}`);
    extracts.push({member,target,data,bytes:data.length,sha256:hash(data),crc32:matches[0].crc32});
  }
  fs.mkdirSync(path.dirname(root),{recursive:true});fs.mkdirSync(root);fs.mkdirSync(path.join(root,'base'));fs.mkdirSync(path.join(root,'save'));
  const supportReport=copySupport(support,path.join(root,'base')),sourceFiles=[];
  for(const [source,target] of inputs){
    const input=path.join(owner,source),destination=path.join(root,'base',target);fs.mkdirSync(path.dirname(destination),{recursive:true});
    // Keep material bytes and extracted DECLs on the same immutable read.
    if(source==='assets.pk4')fs.writeFileSync(destination,archiveBytes,{flag:'wx'});
    else fs.copyFileSync(input,destination,fs.constants.COPYFILE_EXCL);
    const data=fs.readFileSync(destination);sourceFiles.push({source,target:'base/'+target,bytes:data.length,sha256:hash(data)});
  }
  for(const entry of extracts)writeNew(root,'base/'+entry.target,entry.data);
  const fields={
    policy:'NativePlayerOnlyV1',map:'wolf2009/main/PROC/maps/game/trainyard/trainyard',sourceMapName:'maps/game/trainyard/trainyard',
    player:'player_marine',startup:'game_trainyard_trainyard_startingWeaponsAndPowers',startupLibrary:'startingWeaponsAndPowersLibrary',startupKey:'def_trainyard',
    globalMain:'fixtures/source-session-global.decls',globalLocale:'fixtures/source-session-global-locale.decls',
    mapMain:'wolf2009/sp_map/DECL/maps/game/trainyard/trainyard',mapLocale:'wolf2009/sp_locale/DECL/maps/game/trainyard/trainyard',
    clipResource:'wolf2009/sp_global/HKXS/maps/game/global/global',standBody:'models/characters/player/playershape',crouchBody:'models/characters/player/playershape_crouch',
    mapEntitiesProfile:'MeasuredTrainyardIntroV1',mapEntities:'wolf2009/main/ENTS/maps/game/trainyard/trainyard',
    mapCollision:'wolf2009/main/HKXS/maps/game/trainyard/trainyard',materialArchive:'fixtures/wolf-materials.bin'
  };
  if(ai)Object.assign(fields,{
    mapArrivalProfile:'MeasuredTrainyardArrivalV1',mapAIProfile:profile,mapExitProfile:'MeasuredTrainyardExitV1'
  });
  const config='Wolf2009SourceSession 1\n{\n'+Object.entries(fields).map(([key,value])=>`"${key}" "${value}"\n`).join('')+'}\n';
  writeNew(root,'base/wolf2009-session.cfg',config);
  // Publish ready markers only after every source, extraction and config write.
  writeNew(root,'owner.ready','original-assets-and-empty\n');
  writeNew(root,'trainyard.ready','MeasuredTrainyardIntroV1\n');
  if(ai)writeNew(root,'source-ai-shell.ready',profile+'\n');
  return {scope:'Private ordinary native trainyard source data; no QA flags, automatic checks or APK assets',
    root,support:supportReport,sourceFiles,extracted:extracts.map(({data,...metadata})=>metadata),configSha256:hash(config),
    ready:ai?['owner.ready','trainyard.ready','source-ai-shell.ready']:['owner.ready','trainyard.ready']};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  try{
    const options={};for(let i=2;i<process.argv.length;i+=2){const name=process.argv[i],value=process.argv[i+1];
      need(['--game-root','--support-base','--output','--profile'].includes(name)&&value&&!options[name],'Usage: --game-root <owner game or SP/base> --support-base <authored base> --output <new private source root> [--profile SourceAIShellV1]');options[name]=value;}
    need(options['--game-root']&&options['--support-base']&&options['--output'],'Require --game-root, --support-base and --output');
    process.stdout.write(JSON.stringify(prepareNativeTrainyardSource({gameRoot:options['--game-root'],supportBase:options['--support-base'],output:options['--output'],profile:options['--profile']}),null,2)+'\n');
  }catch(error){process.stderr.write(`SOURCE_SESSION_STAGE_FAIL ${error.message}\n`);process.exitCode=1;}
}
