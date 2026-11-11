// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in core fixture. Original data is read only; all writes stay in the new
// runner-owned filesystem root. It prepares the NORMAL devmap/player path.
#include "ZipArchive.h"
#include "RenderData.h"
#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <dlfcn.h>
#include "idlib/precompiled.h"
#include "Wolf2009Render.h"
#include "framework/Wolf2009Streams.h"
#include "SourceSessionChecks.h"
#include "EngineGameplay.h"
#include "cm/CollisionModel.h"
#ifdef WOLF2009_SOURCE_AUDIO_BANK_QA
void Wolf2009_QAPurgeUnreferencedSounds();
#endif
namespace {
bool requested=false,trainyard=false,trainyardArrival=false,trainyardActor=false,trainyardDoor=false,trainyardGreeter=false,trainyardExit=false;
bool trainyardAI=false,trainyardMove=false,trainyardPickups=false;
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
// Hands the greeter QA the ordinary native cache level-load purge (headless
// sound has no device, so its public level-load wrappers return early).
void GreeterPurgeSeam(const char* library){
#ifdef WOLF2009_SOURCE_AUDIO_BANK_QA
    void* handle=dlopen(library,RTLD_NOW|RTLD_NOLOAD);Need(handle!=NULL,"source session game module is not loaded");
    typedef void (*Setter)(void(*)());Setter set=reinterpret_cast<Setter>(dlsym(handle,"Wolf2009_SetTrainyardGreeterPurgeQA"));
    if(set)set(Wolf2009_QAPurgeUnreferencedSounds);dlclose(handle);
#else
    (void)library;
#endif
}
}
bool Wolf2009_PrepareSourceSessionFixture(){
    try{
        File trainyardFlag(fileSystem->OpenFileRead("fixtures/trainyard-session.enabled",false));trainyard=bool(trainyardFlag);
        File arrivalFlag(fileSystem->OpenFileRead("fixtures/trainyard-arrival.enabled",false));trainyardArrival=bool(arrivalFlag);
        Need(!trainyardArrival||trainyard,"Arrival QA requires the explicit source trainyard fixture");
        File actorFlag(fileSystem->OpenFileRead("fixtures/trainyard-actor.enabled",false));trainyardActor=bool(actorFlag);
        Need(!trainyardActor||trainyardArrival,"Scripted actor QA requires the explicit arrival fixture");
        File greeterFlag(fileSystem->OpenFileRead("fixtures/trainyard-greeter.enabled",false));trainyardGreeter=bool(greeterFlag);
        // The route owns the exact train_greeter/train_door_1 names; the separate
        // actor fixture would need them too, so the two requests are exclusive.
        Need(!trainyardGreeter||(trainyardArrival&&!trainyardActor),"Greeter route QA requires the arrival fixture without the separate actor fixture");
        File exitFlag(fileSystem->OpenFileRead("fixtures/trainyard-exit.enabled",false));trainyardExit=bool(exitFlag);
        File aiFlag(fileSystem->OpenFileRead("fixtures/trainyard-ai-shell.enabled",false));trainyardAI=bool(aiFlag);
        Need(!trainyardAI||(trainyardArrival&&!trainyardActor&&!trainyardGreeter),"AI shell QA requires exclusive ownership of the trainyard actors");
        Need(!trainyardExit||trainyardGreeter,"Trainyard exit QA requires the greeter route fixture");
        // SourceAIShellGreeterRouteV1: the shell hosts the greeter route's network.
        File moveFlag(fileSystem->OpenFileRead("fixtures/trainyard-greeter-move.enabled",false));trainyardMove=bool(moveFlag);
        Need(!trainyardMove||(trainyardArrival&&!trainyardActor&&!trainyardGreeter&&!trainyardAI&&!trainyardExit),"Greeter move QA owns the greeter, door and AI shell exclusively");
        // SourcePickupInventoryV1 runs inside the AI shell/exit session (shake_train node 31).
        File pickupFlag(fileSystem->OpenFileRead("fixtures/trainyard-pickups.enabled",false));trainyardPickups=bool(pickupFlag);
        Need(!trainyardPickups||trainyardAI,"Pickup QA requires the AI shell and exit session");
        File flag(fileSystem->OpenFileRead("fixtures/source-session.enabled",false));
        File doorFlag(fileSystem->OpenFileRead("fixtures/source-animated-door.enabled",false));trainyardDoor=bool(doorFlag);
        Need(!trainyardDoor||(flag&&!trainyard),"Animated-door QA requires a separate original empty source session");
        if(!flag&&!trainyard)return true;
        Need(!requested,"source session fixture cannot prepare twice");requested=true;
        Need(Wolf2009_Mount("sp_global","maps/game/global/global.spk"),"source session global mount failed");
        Need(Wolf2009_Mount("sp_map","maps/game/trainyard/trainyard.mpk"),"source session map mount failed");
        Need(Wolf2009_Mount("sp_locale","maps/game/trainyard/trainyard_vo_english.spk"),"source session map locale mount failed");
        Need(Wolf2009_Mount("sp_mp40","streampacks/mp40.spk"),"source session weapon detail mount failed");
        if(trainyardActor||trainyardDoor||trainyardGreeter||trainyardAI||trainyardMove)Need(Wolf2009_Mount("sp_greeter","streampacks/allies_kreisau_agt02.spk"),"source actor detail mount failed");
        if(trainyardAI||trainyardMove){
            Need(Wolf2009_Mount("sp_ai_agt01","streampacks/allies_kreisau_agt01.spk"),"source hostage detail mount failed");
            Need(Wolf2009_Mount("sp_ai_wehrmacht","streampacks/wehrmacht_infantry_mp40.spk"),"source interrogator detail mount failed");
        }
        // The isolated empty fixture has no trainyard map-entity descriptor to
        // preload the catalog. Do this before any source model is imported.
        if(trainyardDoor)Need(Wolf2009_LoadMaterialCatalog("fixtures/wolf-materials.bin"),"isolated source door material catalog preload failed");
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
        if(trainyardArrival)config+="\"mapArrivalProfile\" \"MeasuredTrainyardArrivalV1\"\n";
        if(trainyardGreeter)config+="\"mapGreeterProfile\" \"MeasuredTrainyardGreeterRouteV1\"\n"
            "\"mapGreeterModel\" \"wolf2009/sp_greeter/modl/models/characters/civilians/civ_male_04/anims/body_cm04.md5r\"\n"
            "\"mapGreeterRig\" \"wolf2009/sp_global/skel/maps/game/global/global\"\n"
            "\"mapGreeterVoiceLocale\" \"english\"\n";
        if(trainyardExit)config+="\"mapExitProfile\" \"MeasuredTrainyardExitV1\"\n";
        if(trainyardMove){config+="\"mapGreeterProfile\" \"MeasuredTrainyardGreeterRouteV1\"\n"
            "\"mapGreeterModel\" \"wolf2009/sp_greeter/modl/models/characters/civilians/civ_male_04/anims/body_cm04.md5r\"\n"
            "\"mapGreeterRig\" \"wolf2009/sp_global/skel/maps/game/global/global\"\n"
            "\"mapGreeterVoiceLocale\" \"english\"\n"
            "\"mapAIProfile\" \"SourceAIShellV1\"\n"
            // Node 10 faces the native sewer trapdoor (TRAPDOOR T1/T3).
            "\"mapDoorProfile\" \"SourceRotatingDoorOldV1\"\n";
            // Node 10 routes on the native aas28 file (WolfAas140V1).
            cvarSystem->SetCVarString("wolf2009_aasProfile","WolfAas140V1");}
        if(trainyardAI)config+="\"mapAIProfile\" \"SourceAIShellV1\"\n"
            "\"mapExitProfile\" \"MeasuredTrainyardExitV1\"\n";
        if(trainyardPickups)config+="\"mapPickupProfile\" \"SourcePickupInventoryV1\"\n";
        config+="}\n";
        WriteExclusive("wolf2009-session.cfg",std::vector<unsigned char>(config.begin(),config.end()));
        printf("WOLF2009_SOURCE_SESSION_FIXTURE_READY original_read_only=1 generated_descriptor=1 normal_devmap=1 full_campaign=0\n");return true;
    }catch(const std::exception& error){fprintf(stderr,"WOLF2009_SOURCE_SESSION_FIXTURE_FAIL reason=%s\n",error.what());return false;}
}
bool Wolf2009_SourceSessionFixtureRequested(){return requested;}
bool Wolf2009_TrainyardSessionFixtureRequested(){return trainyard;}
bool Wolf2009_RunSourceSessionFixture(const char* library){try{
    if(trainyardPickups)return Probe(library,"Wolf2009_RunTrainyardPickupQA");
    if(trainyardAI)return Probe(library,"Wolf2009_RunTrainyardAIShellQA");
    if(trainyardMove)return Probe(library,"Wolf2009_RunTrainyardGreeterMoveQA");
    if(trainyardDoor)return Probe(library,"Wolf2009_RunTrainyardAnimatedDoorQA");
    if(requested&&!trainyard){Wolf2009_EnableNativeCollisionFixture(true);const auto handle=collisionModelManager->LoadModel("wolf2009/_qa/native-collision",false);Wolf2009_EnableNativeCollisionFixture(false);Need(handle>0,"Native mover QA collision preload failed");}
    // The greeter route consumes the director's relay request itself, so its
    // QA replaces the arrival QA that takes that request directly.
    if(trainyardGreeter)GreeterPurgeSeam(library);
    if(!Probe(library,trainyardExit?"Wolf2009_RunTrainyardExitQA":trainyardGreeter?"Wolf2009_RunTrainyardGreeterRouteQA":trainyardArrival?"Wolf2009_RunTrainyardArrivalQA":trainyard?"Wolf2009_RunTrainyardSessionQA":"Wolf2009_RunSourceSessionQA"))return false;
    if(trainyardActor&&!Probe(library,"Wolf2009_RunTrainyardScriptedActorQA"))return false;
    return true;
}catch(const std::exception& e){fprintf(stderr,"WOLF2009_SOURCE_SESSION_HOST_FAIL reason=%s\n",e.what());return false;}}
bool Wolf2009_FinishSourceSessionFixture(const char* library){try{return Probe(library,"Wolf2009_FinishSourceSessionQA");}catch(const std::exception& e){fprintf(stderr,"WOLF2009_SOURCE_SESSION_HOST_FAIL reason=%s\n",e.what());return false;}}
