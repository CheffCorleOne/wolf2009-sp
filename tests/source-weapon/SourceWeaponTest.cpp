// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceWeaponProfile.h"
#include "CompiledDeclarations.h"
#include "CompiledDeclarationCatalog.h"
#include "CompiledDeclarationNamespace.h"
#include "Archive.h"
#include "ZipArchive.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace wolf2009;
typedef std::vector<unsigned char> Bytes;
unsigned checks=0;
void Need(bool v,const char* text){++checks;if(!v)throw std::runtime_error(text);}
template<class F>void Reject(F body,const char* text){bool rejected=false;try{body();}catch(const std::exception&){rejected=true;}Need(rejected,text);}
void U32(Bytes& bytes,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(value>>(8*i)));}
void Append(Bytes& bytes,const std::string& value){bytes.insert(bytes.end(),value.begin(),value.end());}
std::string FloatWord(float n){std::uint32_t bits=0;std::memcpy(&bits,&n,4);return std::to_string(bits);}
Bytes Packet(const std::vector<std::string>& records){Bytes bytes;U32(bytes,0);U32(bytes,0);Append(bytes,"1\nown/weapon.def\n3\n"+std::to_string(records.size())+"\n");
    for(std::size_t i=0;i<records.size();++i)Append(bytes,std::to_string(i)+"\n"+std::to_string(records[i].size())+"\n"+records[i]);Append(bytes,"0\n");return bytes;}
struct Fixtures {
    CompiledDeclarationCatalog catalog;
    SourceDefinitions definitions;
    explicit Fixtures(const std::vector<std::string>& records){
        CompiledCatalogSource source;source.role=CompiledCatalogRole::Explicit;source.loadIdentity="own.weapon";source.path="own/weapon.decls";
        catalog.AddDocument(CompiledDeclarations::Read(Packet(records),CompiledDeclFormat::TwoSectionLfTextV1),source);
        CompiledDeclarationNamespace names(CompiledNamespacePolicy::OrdinaryTextOwnersV1);names.ApplySection(catalog,0,0,CompiledNamespaceReaderMode::Ordinary);
        for(const auto& entry:names.SelectedEntries())if(entry.type=="entityDef"||entry.type=="model")definitions.AddCompiledRecord(entry);
    }
};
std::vector<std::string> Records(){
    return {
        "entityDef weapon_none {\n\"HasPrimaryAttack\" 4 1 0\n\"HasMeleeAttack\" 4 1 0\n\"HasGrenadeAttack\" 4 1 0\n}",
        "entityDef own_weapon_base {\n\"spawnclass\" 0 0 \"idItem\"\n\"weaponclass\" 0 0 \"rvWeaponCommonGun\"\n\"ammoRequired\" 4 1 1\n}",
        "entityDef weapon_mp40 {\n\"inherit\" 0 0 \"own_weapon_base\"\n\"model_view\" 0 0 \"own_view\"\n\"model_world\" 0 0 \"own_world\"\n\"detailpack\" 0 0 \"own/weapon.spk\"\n\"clipSize\" 4 1 3\n\"InvAmmoCapacity\" 4 1 10\n\"InvInitialAmmo\" 4 1 6\n\"inv_ammo_mp40\" 4 1 3\n\"def_ammoType\" 0 0 \"own_ammo\"\n\"def_attack0\" 0 0 \"own_primary\"\n\"def_effect0\" 0 0 \"own_effect\"\n\"snd_reload\" 0 0 \"own_reload\"\n\"opaque_feature\" 7 42 93\n}",
        "entityDef own_ammo {\n\"ammo_type\" 0 0 \"own_ammo\"\n}",
        "entityDef own_primary {\n\"AttackClass\" 0 0 \"SoundHitScanAttack\"\n\"TriggerClass\" 0 0 \"Hold\"\n\"MaxRange\" 4 1 100\n\"def_ammoType\" 0 0 \"own_ammo\"\n\"num_projectiles\" 4 1 1\n\"AutoFireInterval\" 3 1 "+FloatWord(.125f)+"\n\"def_damage\" 0 0 \"own_damage\"\n}",
        "entityDef own_damage {\n\"damage\" 4 1 7\n\"radius\" 4 1 0\n\"retained_effect\" 0 0 \"own_wound\"\n}",
        "entityDef own_effect {\n\"EffectClass\" 0 0 \"OwnUnsupportedVisualEffect\"\n}",
        "model own_view {\nmesh models/own/gun.md5r\nskeleton models/own/gun.hkx\nanim idle models/own/idle.md5anim\nanim fire models/own/fire.md5anim { frame 1 weapon_attack }\nanim reload models/own/reload.md5anim { frame 1 point_of_interest joint .25 frame 2 sound_weapon snd_reload frame 1 eye_angles -1 2 3 10 1 frame 4 eye_angles 0 0 0 10 1 }\nanim melee models/own/melee.md5anim {frame 2 weapon_melee}\n}",
        "model own_world {\nmesh models/own/world.md5r\nskeleton models/own/world.hkx\nanim idle models/own/world_idle.md5anim\nanimated 0\n}"};
}
SourceWeaponTiming Timing(){SourceWeaponTiming value;value.policy=SourceWeaponClockPolicy::NativeSimulationMillisecondsV1;value.fireFrames=3;value.reloadFrames=5;value.fireDurationSeconds=.25;value.reloadDurationSeconds=.5;return value;}
std::shared_ptr<const SourceWeaponProfile> Prepare(Fixtures& f){return PrepareSourceWeaponProfile(f.definitions,"weapon_mp40",SourceWeaponPolicy::MP40PrimaryNativeV1);}
unsigned Count(const std::vector<SourceWeaponEvent>& events,SourceWeaponEventKind kind){unsigned count=0;for(const auto& event:events)if(event.kind==kind)++count;return count;}
void Replace(std::vector<std::string>& records,std::size_t at,const std::string& from,const std::string& to){const auto position=records.at(at).find(from);Need(position!=std::string::npos,"own test edit did not find its field");records[at].replace(position,from.size(),to);}
void Synthetic(){
    std::shared_ptr<const SourceWeaponProfile> profile,none;
    {Fixtures f(Records());profile=Prepare(f);none=PrepareSourceWeaponProfile(f.definitions,"weapon_none",SourceWeaponPolicy::NoneV1);}
    Need(profile->Configuration().clipSize==3&&profile->Configuration().autoFireIntervalMs==125&&profile->Configuration().damage==7,"typed weapon configuration changed");
    Need(profile->Weapon().Find("opaque_feature")&&profile->Weapon().Find("opaque_feature")->encoding==SourceLiteralEncoding::CompiledOpaque,"unknown compiled field was dropped");
    Need(profile->Weapon().selected.member.compiledRecord->IsUnmodified()&&profile->FireCommands()[0].origins[0].origin.member.compiledRecord->IsUnmodified(),"weapon profile lost source ownership");
    Need(profile->ViewModel().Find("melee")&&profile->EffectDefinitions().size()==1&&profile->ReloadCommands().size()==4,"unselected commands/effects were dropped");
    Need(profile->ReloadCommands()[0].kind==SourceWeaponCommandKind::PointOfInterest&&profile->ReloadCommands()[1].kind==SourceWeaponCommandKind::EyeAngles&&profile->ReloadCommands()[2].kind==SourceWeaponCommandKind::SoundWeapon,"source command stable frame ordering changed");
    SourceWeaponController empty(none,Timing(),0,0);Need(empty.Step(0,true,true).empty()&&empty.State().phase==SourceWeaponPhase::None,"weapon_none fired or reloaded");
    Reject([&]{SourceWeaponController c(none,Timing(),1,0);},"weapon_none accepted ammo");
    auto timing=Timing();timing.policy=SourceWeaponClockPolicy::Unspecified;Reject([&]{SourceWeaponController c(profile,timing,3,4);},"unspecified clock accepted");
    timing=Timing();timing.reloadFrames=3;Reject([&]{SourceWeaponController c(profile,timing,3,4);},"out-of-clip frame command accepted");
    timing=Timing();timing.fireDurationSeconds=std::numeric_limits<double>::quiet_NaN();Reject([&]{SourceWeaponController c(profile,timing,3,4);},"nonfinite duration accepted");
    for(const auto& pair:std::vector<std::pair<int,int>>{{-1,0},{4,0},{0,-1},{0,11}})Reject([&]{SourceWeaponController c(profile,Timing(),pair.first,pair.second);},"invalid initial ammo accepted");

    SourceWeaponController gun(profile,Timing(),3,4);auto events=gun.Step(1000,true,false);
    Need(gun.State().clip==2&&gun.State().shots==1&&gun.State().frameCommands==1&&Count(events,SourceWeaponEventKind::StartFire)==1&&Count(events,SourceWeaponEventKind::FrameCommand)==1,"first press did not dispatch one weapon_attack/one round");
    Need(gun.Step(1000,true,false).empty()&&gun.Step(1125,true,false).empty()&&gun.State().clip==2,"same frame or strict boundary fired");
    gun.Step(1126,true,false);Need(gun.State().clip==1&&gun.State().shots==2,"Hold did not fire after strict boundary");
    gun.Step(10000,true,false);Need(gun.State().clip==0&&gun.State().shots==3,"time jump produced a shot burst");
    gun.Step(10126,true,false);Need(gun.State().clip==0&&gun.State().emptyClicks==1,"empty clip became negative/missed empty click");
    gun.Step(20000,true,false);Need(gun.State().emptyClicks==1,"held empty weapon repeated click without release");
    gun.Step(20001,false,false);gun.Step(20002,true,false);Need(gun.State().emptyClicks==2,"release did not reset empty latch");
    const auto before=gun.State();Reject([&]{gun.Step(100,true,true);},"backwards clock accepted");
    Need(gun.State().lastTimeMs==before.lastTimeMs&&gun.State().clip==before.clip&&gun.State().reloads==before.reloads,"backwards step mutated state");

    events=gun.Step(21000,false,true);Need(gun.State().phase==SourceWeaponPhase::Reloading&&gun.State().reloadEndTimeMs==21500&&gun.State().clip==0&&gun.State().reserve==4&&Count(events,SourceWeaponEventKind::StartReload)==1&&Count(events,SourceWeaponEventKind::FrameCommand)==2,"reload start/frame1 commands mutated ammo or lost ordering");
    events=gun.Step(21125,true,false);Need(gun.State().shots==3&&Count(events,SourceWeaponEventKind::FrameCommand)==1,"reload fired or source sound frame crossing was lost");
    Need(gun.Step(21125,true,false).empty(),"same reload time repeated command");
    events=gun.Step(21499,false,false);Need(gun.State().clip==0&&Count(events,SourceWeaponEventKind::FrameCommand)==1,"reload endpoint committed early or missed eye-angle reset");
    events=gun.Step(21500,false,false);Need(gun.State().clip==3&&gun.State().reserve==1&&gun.State().phase==SourceWeaponPhase::Ready&&Count(events,SourceWeaponEventKind::ReloadComplete)==1,"reload endpoint failed conservation/transfer");
    gun.Step(22000,true,false);events=gun.Step(22001,false,true);Need(gun.State().phase==SourceWeaponPhase::Reloading,"partial reload did not start");
    const auto clip=gun.State().clip,reserve=gun.State().reserve;events=gun.Step(22002,true,false,false);
    Need(gun.State().phase==SourceWeaponPhase::Disabled&&gun.State().clip==clip&&gun.State().reserve==reserve&&Count(events,SourceWeaponEventKind::Disabled)==1,"disable did not cancel reload without moving ammo");
    Need(gun.Step(23000,false,false,false).empty(),"disabled step repeatedly emitted effects");
    gun.Step(23001,false,false);Need(gun.State().phase==SourceWeaponPhase::Ready&&gun.State().clip==clip,"reenable completed a cancelled reload");
    gun.Step(std::numeric_limits<int>::max()-100,false,false);const auto horizon=gun.State();
    Reject([&]{gun.Step(std::numeric_limits<int>::max()-100,false,true);},"reload crossed signed clock horizon");Need(gun.State().reloads==horizon.reloads&&gun.State().phase==horizon.phase,"clock-horizon refusal mutated reload");

    for(unsigned variant=0;variant<12;++variant){auto records=Records();
        switch(variant){
          case 0:Replace(records,4,"\"Hold\"","\"Burst\"");break;
          case 1:Replace(records,4,"\"SoundHitScanAttack\"","\"ProjectileAttack\"");break;
          case 2:Replace(records,4,"\"AutoFireInterval\" 3 1 "+FloatWord(.125f),"\"AutoFireInterval\" 3 1 "+FloatWord(std::numeric_limits<float>::infinity()));break;
          case 3:Replace(records,4,"\"num_projectiles\" 4 1 1","\"num_projectiles\" 4 1 2");break;
          case 4:Replace(records,2,"\"clipSize\" 4 1 3","\"clipSize\" 4 1 -1");break;
          case 5:Replace(records,4,"\"def_ammoType\" 0 0 \"own_ammo\"","\"def_ammoType\" 0 0 \"other\"");break;
          case 6:Replace(records,7,"frame 1 weapon_attack","frame 2 weapon_attack");break;
          case 7:Replace(records,7,"frame 1 weapon_attack","frame 1 weapon_attack frame 1 weapon_attack");break;
          case 8:Replace(records,7,"frame 2 sound_weapon snd_reload","frame 2 weapon_grenade");break;
          case 9:Replace(records,7,"frame 1 eye_angles -1 2 3 10 1","frame 1 eye_angles -1 2 3 10 1.5");break;
          case 10:records.erase(records.begin()+5);break;
          case 11:Replace(records,5,"\"radius\" 4 1 0","\"radius\" 4 1 5");break;
        }Fixtures bad(records);Reject([&]{Prepare(bad);},"unsupported source profile admitted");
    }
    {auto records=Records();Replace(records,2,"\"clipSize\" 4 1 3","\"clipSize\" 7 42 3");Fixtures bad(records);Reject([&]{Prepare(bad);},"opaque child clipSize exposed parent default");}
    std::cout<<"SOURCE_WEAPON_SYNTHETIC shots="<<gun.State().shots<<" reloads="<<gun.State().reloads<<" retained_all_commands=1 native_clock_policy=1\n";
}
Bytes Decl(const std::string& path){std::ifstream file(path,std::ios::binary);Need(bool(file),"selected original package absent");const auto archive=Archive::Scan(file);std::size_t at=0,count=0;
    for(std::size_t i=0;i<archive.Chunks().size();++i)if(archive.Chunks()[i].type=="DECL"){at=i;++count;}Need(count==1&&archive.Chunks()[at].entries.size()==1,"selected original DECL not unique");return archive.ReadEntry(file,at,0);}
void Original(const std::string& base){
    CompiledDeclarationCatalog catalog;std::ifstream assetsFile(base+"/assets.pk4",std::ios::binary);Need(bool(assetsFile),"owner assets PK4 absent");const auto assets=ZipArchive::Scan(assetsFile);
    const std::string paths[]={"maps/game/global/global_vo_english.decls","maps/game/global/global.decls","maps/game/trainyard/trainyard_vo_english.spk","maps/game/trainyard/trainyard.mpk"};
    for(unsigned i=0;i<4;++i){Bytes bytes;if(i<2){bool found=false;for(std::size_t n=0;n<assets.Entries().size();++n)if(assets.Entries()[n].name==paths[i]){Need(!found,"duplicate original member");bytes=assets.ReadEntry(assetsFile,n);found=true;}Need(found,"selected global member absent");}else bytes=Decl(base+"/"+paths[i]);
        CompiledCatalogSource source;source.role=i<2?CompiledCatalogRole::Global:CompiledCatalogRole::Map;source.loadIdentity="weapon.original."+std::to_string(i);source.path=paths[i];catalog.AddDocument(CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1),source);}
    CompiledDeclarationNamespace names(CompiledNamespacePolicy::OrdinaryTextOwnersV1);
    for(const auto& step:std::vector<std::pair<unsigned,unsigned>>{{0,0},{1,0},{1,1},{2,0},{2,1},{3,0},{3,1}})names.ApplySection(catalog,step.first,step.second,step.first<2?CompiledNamespaceReaderMode::ProtectAccepted:CompiledNamespaceReaderMode::Ordinary);
    SourceDefinitionLimits limits;limits.members=20000;limits.totalSourceBytes=128u<<20;SourceDefinitions definitions(limits,SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);
    for(const auto& entry:names.SelectedEntries())if(entry.type=="entityDef"||entry.type=="model")definitions.AddCompiledRecord(entry);
    const auto profile=PrepareSourceWeaponProfile(definitions,"weapon_mp40",SourceWeaponPolicy::MP40PrimaryNativeV1);
    const auto none=PrepareSourceWeaponProfile(definitions,"weapon_none",SourceWeaponPolicy::NoneV1);
    const auto& c=profile->Configuration();Need(c.clipSize==32&&c.reserveCapacity==200&&c.initialAmmo==50&&c.pickupAmmo==32&&c.maxRange==2000&&c.damage==5&&c.autoFireIntervalMs==120,"actual source MP40 configuration differs");
    Need(profile->FireCommands().size()==1&&profile->FireCommands()[0].frame==1&&profile->ReloadCommands().size()==7,"actual MP40 authored commands differ");
    Need(profile->ViewModel().Find("fire2")&&profile->ViewModel().Find("altfire")&&profile->ViewModel().Find("raise")&&none->Policy()==SourceWeaponPolicy::NoneV1,"source unselected animation or none facts were lost");
    std::cout<<"SOURCE_WEAPON_ORIGINAL clip="<<c.clipSize<<" reserve_capacity="<<c.reserveCapacity<<" interval_ms="<<c.autoFireIntervalMs<<" range="<<c.maxRange<<" damage="<<c.damage<<" effects="<<profile->EffectDefinitions().size()<<" commands="<<profile->ReloadCommands().size()+profile->FireCommands().size()<<" no_native_claim=1\n";
}
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--self-test")Synthetic();else if(argc==3&&std::string(argv[1])=="--source-base")Original(argv[2]);else throw std::runtime_error("usage: --self-test | --source-base <owner SP/base>");
    std::cout<<"SOURCE_WEAPON_OK checks="<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"SOURCE_WEAPON_FAIL checks="<<checks<<" reason="<<error.what()<<'\n';return 1;}}
