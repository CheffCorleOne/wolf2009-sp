// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in core fixture. Original data is read only; all writes stay in the new
// runner-owned filesystem root. It prepares the NORMAL devmap/player path.
#include "ZipArchive.h"
#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <dlfcn.h>
#include "idlib/precompiled.h"
#include "framework/Wolf2009Streams.h"
#include "SourceSessionChecks.h"
#include "EngineGameplay.h"
#include "cm/CollisionModel.h"
namespace {
bool requested=false,trainyard=false;
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct Close {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
typedef std::unique_ptr<idFile,Close> File;
void WriteExclusive(const char* path,const std::vector<unsigned char>& bytes){
    File old(fileSystem->OpenFileRead(path,false));Need(!old,"source session fixture output already exists");
    Need(fileSystem->WriteFile(path,bytes.data(),int(bytes.size()))==int(bytes.size()),"source session fixture write failed");
}
bool Probe(const char* library,const char* symbol){
    if(!requested)return true;
    void* handle=dlopen(library,RTLD_NOW|RTLD_NOLOAD);Need(handle!=NULL,"source session game module is not loaded");
    typedef int (*Entry)();Entry entry=reinterpret_cast<Entry>(dlsym(handle,symbol));
    const bool ok=entry&&entry()==1;dlclose(handle);return ok;
}
}
bool Wolf2009_PrepareSourceSessionFixture(){
    try{
        File trainyardFlag(fileSystem->OpenFileRead("fixtures/trainyard-session.enabled",false));trainyard=bool(trainyardFlag);
        File flag(fileSystem->OpenFileRead("fixtures/source-session.enabled",false));if(!flag&&!trainyard)return true;
        Need(!requested,"source session fixture cannot prepare twice");requested=true;
        Need(Wolf2009_Mount("sp_global","maps/game/global/global.spk"),"source session global mount failed");
        Need(Wolf2009_Mount("sp_map","maps/game/trainyard/trainyard.mpk"),"source session map mount failed");
        Need(Wolf2009_Mount("sp_locale","maps/game/trainyard/trainyard_vo_english.spk"),"source session map locale mount failed");
        Need(Wolf2009_Mount("sp_mp40","streampacks/mp40.spk"),"source session weapon detail mount failed");
        File input(fileSystem->OpenFileRead("fixtures/wolf-materials.bin",false));Need(bool(input),"source session owner archive is absent");
        const int length=input->Length();Need(length>0&&length<=(64<<20),"source session owner archive byte budget");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));Need(input->Read(bytes.data(),length)==length,"short source session owner archive read");
        std::istringstream stream(std::string(bytes.begin(),bytes.end()),std::ios::in|std::ios::binary);
        const auto archive=wolf2009::ZipArchive::Scan(stream);
        for(unsigned role=0;role<2;++role){
            const char* wanted=role?"maps/game/global/global_vo_english.decls":"maps/game/global/global.decls";
            const char* target=role?"fixtures/source-session-global-locale.decls":"fixtures/source-session-global.decls";
            std::size_t matches=0,index=0;
            for(std::size_t i=0;i<archive.Entries().size();++i){std::string name=archive.Entries()[i].name;
                for(char& c:name){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}
                if(name==wanted){index=i;++matches;}}
            Need(matches==1,"source session exact global declaration member is missing/ambiguous");WriteExclusive(target,archive.ReadEntry(stream,index));
        }
        std::string config=
            "Wolf2009SourceSession 1\n{\n"
            "\"policy\" \"NativePlayerOnlyV1\"\n";
        config+=trainyard?"\"map\" \"wolf2009/main/PROC/maps/game/trainyard/trainyard\"\n":"\"map\" \"wolf2009/main/PROC/maps/game/empty/empty\"\n";
        config+=
            "\"sourceMapName\" \"maps/game/trainyard/trainyard\"\n"
            "\"player\" \"player_marine\"\n"
            "\"startup\" \"game_trainyard_trainyard_startingWeaponsAndPowers\"\n"
            "\"startupLibrary\" \"startingWeaponsAndPowersLibrary\"\n"
            "\"startupKey\" \"def_trainyard\"\n"
            "\"globalMain\" \"fixtures/source-session-global.decls\"\n"
            "\"globalLocale\" \"fixtures/source-session-global-locale.decls\"\n"
            "\"mapMain\" \"wolf2009/sp_map/DECL/maps/game/trainyard/trainyard\"\n"
            "\"mapLocale\" \"wolf2009/sp_locale/DECL/maps/game/trainyard/trainyard\"\n"
            "\"clipResource\" \"wolf2009/sp_global/HKXS/maps/game/global/global\"\n"
            "\"standBody\" \"models/characters/player/playershape\"\n"
            "\"crouchBody\" \"models/characters/player/playershape_crouch\"\n";
        if(trainyard)config+="\"mapEntitiesProfile\" \"MeasuredTrainyardIntroV1\"\n"
            "\"mapEntities\" \"wolf2009/main/ENTS/maps/game/trainyard/trainyard\"\n"
            "\"mapCollision\" \"wolf2009/main/HKXS/maps/game/trainyard/trainyard\"\n"
            "\"materialArchive\" \"fixtures/wolf-materials.bin\"\n";
        config+="}\n";
        WriteExclusive("wolf2009-session.cfg",std::vector<unsigned char>(config.begin(),config.end()));
        printf("WOLF2009_SOURCE_SESSION_FIXTURE_READY original_read_only=1 generated_descriptor=1 normal_devmap=1 full_campaign=0\n");return true;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_SOURCE_SESSION_FIXTURE_FAIL reason=%s\n",error.what());return false;}
}
bool Wolf2009_SourceSessionFixtureRequested(){return requested;}
bool Wolf2009_TrainyardSessionFixtureRequested(){return trainyard;}
bool Wolf2009_RunSourceSessionFixture(const char* library){try{
    if(requested&&!trainyard){Wolf2009_EnableNativeCollisionFixture(true);const auto handle=collisionModelManager->LoadModel("wolf2009/_qa/native-collision",false);Wolf2009_EnableNativeCollisionFixture(false);Need(handle>0,"Native mover QA collision preload failed");}
    return Probe(library,trainyard?"Wolf2009_RunTrainyardSessionQA":"Wolf2009_RunSourceSessionQA");
}catch(const std::exception& e){fprintf(stderr,"WOLF2009_SOURCE_SESSION_HOST_FAIL reason=%s\n",e.what());return false;}}
bool Wolf2009_FinishSourceSessionFixture(const char* library){try{return Probe(library,"Wolf2009_FinishSourceSessionQA");}catch(const std::exception& e){fprintf(stderr,"WOLF2009_SOURCE_SESSION_HOST_FAIL reason=%s\n",e.what());return false;}}
