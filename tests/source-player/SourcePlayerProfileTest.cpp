// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourcePlayerProfile.h"
#include "CompiledDeclarationNamespace.h"
#include "HavokTestFixture.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace wolf2009;using namespace wolf2009_test;
unsigned checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void Reject(F f,const char* why){bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}Check(rejected,why);}
Bytes BytesOf(const std::string& text){return Bytes(text.begin(),text.end());}
std::string PlayerText(){std::string text="entityDef own_player { spawnclass idPlayer useClipModel 1 noDefaultPhysics 1 health 100 maxhealth 100 mass 200 capsuleradius 13.75 keepdistance 2 extraGravity 10 model own_body clipmodel_stand own/stand.hkx clipmodel_crouch own/crouch.hkx initial_weapons \"0 2 3\" current_weapon 2 ";
    for(unsigned i=0;i<12;++i)text+="def_weapon"+std::to_string(i)+" own_weapon"+std::to_string(i)+" ";return text+"}\n";}
SourcePlayerProfileRequest Request(){SourcePlayerProfileRequest r;r.policy=SourcePlayerPolicy::NativeIdPlayerFirstPersonV1;r.playerDefinition="own_player";r.sourceMapName="maps/own/start";r.startupDeclaration="own_start";r.startupLibrary="own_library";r.startupLibraryKey="def_start";return r;}
void Add(SourceDefinitions& d,const std::string& player=PlayerText(),const std::string& start="entityDef own_start { inherit own_base startWeapon own_weapon0 startGold 0 }"){
    SourceDefinitionArchiveIdentity a;a.name="own-fixture";a.bytes=10000;a.fingerprint=12345;
    d.AddMember(a,"own/player.def",BytesOf(player+"\nmodel own_body { mesh own/body.md5r skeleton own/body.hkx }\nentityDef own_library { def_start own_start }\nentityDef own_base { giveWeapon_always1 own_weapon0 giveWeapon_always2 own_weapon1 giveWeapon_always3 own_weapon11 }\n"+start));
}
HavokFixture Clips(){HavokFixture f(4096);for(unsigned j=0;j<2;++j){const unsigned body=256+j*1536,shape=800+j*1536,points=1024+j*1536,planes=1280+j*1536,name=1456+j*1536;
        f.types[body]="hkpRigidBody";f.types[shape]="hkpConvexVerticesShape";f.links[body+112]=name;Text(f.data,name,j?"own/crouch":"own/stand");f.links[body+16]=shape;
        for(unsigned row=0;row<3;++row)F(f.data,body+224+row*16+row*4,1);F(f.data,body+284,1);F(f.data,body+272,0.0000005f);
        U(f.data,shape+68,2);U(f.data,shape+76,8);U(f.data,shape+84,6);f.links[shape+64]=points;f.links[shape+80]=planes;
        const float height=j?.5f:1.f;for(unsigned i=0;i<8;++i){const auto p=points+(i/4)*48+(i%4)*4;F(f.data,p,(i&1)?.1f:-.1f);F(f.data,p+16,(i&2)?.1f:-.1f);F(f.data,p+32,(i&4)?height:0);}
        for(unsigned i=0;i<6;++i){const auto p=planes+i*16;F(f.data,p+(i/2)*4,(i&1)?1.f:-1.f);F(f.data,p+12,i<4?-.1f:(i==4?0:-height));}
    }return f;}
void Synthetic(){
    std::shared_ptr<const SourcePlayerProfile> profile;{SourceDefinitions d;Add(d);profile=PrepareSourcePlayerProfile(d,Request());}
    Check(profile->Health()==100&&profile->MaximumHealth()==100&&profile->Mass()==200,"Source health/mass retained");
    Check(profile->WeaponSlots().size()==12&&profile->DefaultWeaponSlots()==std::vector<unsigned>({0,2,3})&&profile->DefaultWeaponSlot()==2,"Default source slot metadata retained");
    Check(profile->StartingWeaponSlots()==std::vector<unsigned>({0,1,11})&&profile->StartingWeaponSlot()==0,"Map profile takes precedence over default armed slots");
    Check(profile->Player().Find("health")->origins[0].origin.member.name=="own/player.def"&&profile->Startup().declarations.size()==2,"Owning source closure/provenance survives frontend destruction");
    Check(profile->Controls().normalEyeHeight==64&&profile->Controls().crouchHeight==36&&profile->KeepDistance()==2,"Explicit native control facts retained separately");
    Check(!profile->UnsupportedPlayerFeatures().empty()&&profile->UnsupportedStartupFields().empty(),"Unsupported source player systems are reported");
    SourceDefinitions d;Add(d);auto req=Request();req.policy=SourcePlayerPolicy::Unspecified;Reject([&]{PrepareSourcePlayerProfile(d,req);},"Unspecified policy admitted");
    req=Request();req.startupDeclaration="missing";Reject([&]{PrepareSourcePlayerProfile(d,req);},"Missing selected map profile fell back to defaults");
    req=Request();req.startupLibraryKey="missing";Reject([&]{PrepareSourcePlayerProfile(d,req);},"Missing library key admitted");
    const auto bad=[&](const std::string& text){SourceDefinitions x;Add(x,text);Reject([&]{PrepareSourcePlayerProfile(x,Request());},"Invalid source player field admitted");};
    auto text=PlayerText();text.replace(text.find("health 100"),10,"health 101");bad(text);
    text=PlayerText();text.replace(text.find("useClipModel 1"),14,"useClipModel 0");bad(text);
    text=PlayerText();text.replace(text.find("current_weapon 2"),16,"current_weapon 12");bad(text);
    text=PlayerText();text.replace(text.find("own_weapon11"),12,"own_weapon1");bad(text);
    text=PlayerText();text.replace(text.find("0 2 3"),5,"0 2 2");bad(text);
    text=PlayerText();text.replace(text.find("0 2 3"),5,"0 2.5 3");bad(text);
    SourceDefinitions ungranted;Add(ungranted,PlayerText(),"entityDef own_start { inherit own_base startWeapon own_weapon2 startGold 0 }");Reject([&]{PrepareSourcePlayerProfile(ungranted,Request());},"Ungiven selected source weapon admitted");
    SourceDefinitions wrongWeapon;Add(wrongWeapon,PlayerText(),"entityDef own_start { inherit own_base startWeapon own_weapon0 startGold 0 giveWeapon1 nonexistent }");Reject([&]{PrepareSourcePlayerProfile(wrongWeapon,Request());},"Unknown loadout weapon admitted");
    SourceDefinitions active;Add(active,PlayerText(),"entityDef own_start { inherit own_base startWeapon own_weapon0 startGold 0 givePower1 own_power }");auto p=PrepareSourcePlayerProfile(active,Request());Check(p->UnsupportedStartupFields()==std::vector<std::string>({"givepower1"}),"Active unsupported map startup field silently dropped");
    auto fixture=Clips();const auto bytes=fixture.File();auto stand=DecodeSourcePlayerClip(bytes,"own/stand"),crouch=DecodeSourcePlayerClip(bytes,"own/crouch");
    Check(stand.source==bytes&&stand.sourceBytes==bytes.size()&&stand.sourceFingerprint&&stand.bodyOffset==256&&stand.shapeOffset==800,"Source player clip owns byte identity and exact body");
    Check(stand.nativeLocalPoints.size()==8&&stand.rawSupportWords.size()==8&&stand.rawPlaneWords.size()==6&&stand.rawRadiusBits==0,"Source support/plane counts retained");
    Check(stand.nativeLocalPoints[4][2]>crouch.nativeLocalPoints[4][2]&&stand.rawBodyTransform[12]!=0,"Source crouch shape and tiny body transform retained");
    Reject([&]{DecodeSourcePlayerClip(bytes,"OWN/stand");},"Non-exact body lookup admitted");
    auto badType=fixture;badType.types[800]="hkpCapsuleShape";Reject([&]{DecodeSourcePlayerClip(badType.File(),"own/stand");},"Capsule fallback silently substituted");
    auto rounded=fixture;F(rounded.data,816,.01f);Reject([&]{DecodeSourcePlayerClip(rounded.File(),"own/stand");},"Rounded source hull admitted without a radius implementation");
    auto malformed=fixture;U(malformed.data,876,65);Reject([&]{DecodeSourcePlayerClip(malformed.File(),"own/stand");},"Oversized source player hull admitted");
    auto nonfinite=fixture;U(nonfinite.data,1024,0x7fc00001);Reject([&]{DecodeSourcePlayerClip(nonfinite.File(),"own/stand");},"Nonfinite source vertex admitted");
    auto plane=fixture;F(plane.data,1280,-2);Reject([&]{DecodeSourcePlayerClip(plane.File(),"own/stand");},"Nonunit source plane admitted");
    auto trunc=bytes;trunc.pop_back();Reject([&]{DecodeSourcePlayerClip(trunc,"own/stand");},"Truncated source player pack admitted");
    std::cout<<"WOLF2009_SOURCE_PLAYER_SYNTHETIC checks="<<checks<<" native_spawn_claimed=0\n";
}
Bytes Read(const std::string& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Owner input unavailable");return Bytes((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());}
void Original(const std::string& declPath,const std::string& hkxsPath){
    const auto bytes=Read(declPath);auto doc=CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1);
    CompiledDeclarationCatalog catalog;CompiledCatalogSource s;s.loadIdentity="owner-global-player";s.path="maps/game/global/global.decls";s.role=CompiledCatalogRole::Global;catalog.AddDocument(doc,s);
    CompiledDeclarationNamespace ns(CompiledNamespacePolicy::OrdinaryTextOwnersV1);ns.ApplySection(catalog,0,0,CompiledNamespaceReaderMode::ProtectAccepted);ns.ApplySection(catalog,0,1,CompiledNamespaceReaderMode::ProtectAccepted);
    SourceDefinitionLimits limits;limits.members=20000;SourceDefinitions d(limits,SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);for(std::size_t i=0;i<ns.SelectedCount();++i){const auto e=ns.Selected(i);const auto kind=e.type;if(kind=="entityDef"||kind=="model")d.AddCompiledRecord(e);}
    auto req=Request();req.playerDefinition="player_marine";req.sourceMapName="maps/game/trainyard/trainyard";req.startupDeclaration="game_trainyard_trainyard_startingWeaponsAndPowers";req.startupLibrary="startingWeaponsAndPowersLibrary";req.startupLibraryKey="def_trainyard";
    auto p=PrepareSourcePlayerProfile(d,req);Check(p->Health()==100&&p->MaximumHealth()==100&&p->Mass()==200,"Actual source player health/mass");Check(p->DefaultWeaponSlot()==2&&p->DefaultWeaponSlots()==std::vector<unsigned>({0,1,2,3,10}),"Actual source default armed slots retained");
    Check(p->StartingWeaponSlot()==0&&p->StartingWeaponSlots()==std::vector<unsigned>({0,1,11})&&p->StartingGold()==0,"Actual trainyard loadout is none/fists/pendant");
    for(const auto& field:p->UnsupportedStartupFields())std::cerr<<"SOURCE_PLAYER_UNSUPPORTED_STARTUP "<<field<<'\n';
    Check(p->UnsupportedStartupFields().empty()&&p->Player().selected.member.compiledRecord,"Actual source player origins and supported map loadout");
    const auto hkxs=Read(hkxsPath);for(const auto* name:{"models/characters/player/playershape","models/characters/player/playershape_crouch"}){const auto clip=DecodeSourcePlayerClip(hkxs,name);Check(clip.nativeLocalPoints.size()==50&&clip.rawSupportWords.size()==52&&clip.rawPlaneWords.size()==72,"Actual50/52/72 source player hull contract");Check(clip.sourceBytes==65952&&clip.rawRadiusBits==0&&clip.rawFilter==0,"Actual source player raw resource/radius/filter");}
    std::cout<<"WOLF2009_SOURCE_PLAYER_ORIGINAL checks="<<checks<<" loadout=none,fists,pendant equipped=none source_stand_crouch=50vertices native_spawn_claimed=0\n";
}
}
int main(int argc,char** argv){try{if(argc==2&&std::string(argv[1])=="--self-test")Synthetic();else if(argc==5&&std::string(argv[1])=="--source-decls"&&std::string(argv[3])=="--source-hkxs")Original(argv[2],argv[4]);else throw std::runtime_error("Expected --self-test or --source-decls PATH --source-hkxs PATH");return 0;}catch(const std::exception& e){std::cerr<<"SOURCE_PLAYER_FAILED "<<e.what()<<'\n';return 1;}}
