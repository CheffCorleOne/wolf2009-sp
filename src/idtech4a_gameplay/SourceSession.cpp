// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourcePlayerProfile.h"
#include "SourceWeaponProfile.h"
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationSetRuntime.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceWeapon.h"
#include "NativeSourceWeaponView.h"
#include "NativeSourceMap.h"
#include "SourceSession.h"

namespace {
void Need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){if(!ok)throw std::runtime_error(reason.IsEmpty()?fallback:reason.c_str());}
struct CloseFile {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
struct ReleaseView {void operator()(WolfSourceWeaponView* view)const{if(view)Wolf2009_ReleaseSourceWeaponView(view);}};
struct Session {
    std::unique_ptr<WolfSourceDeclarationSet> declarations;
    std::unique_ptr<WolfSourcePlayerPrepared> player;
    std::unique_ptr<WolfSourceMapRuntime> mapRuntime;
    std::string map;
};
std::unique_ptr<Session> active;
std::string MapName(const char* value){
    Need(value!=NULL,"source session map name is absent");std::size_t n=0;
    while(n<=4096&&value[n])++n;Need(n&&n<=4096,"source session map name budget");
    std::string result(value,n);for(char& c:result){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}
    if(result.size()>4&&result.compare(result.size()-4,4,".map")==0)result.resize(result.size()-4);
    Need(result.compare(0,9,"wolf2009/")==0,"source session requires a source-qualified map");
    for(std::size_t i=0;i<result.size();){const auto end=result.find('/',i);const auto part=result.substr(i,end==std::string::npos?end:end-i);
        Need(!part.empty()&&part!="."&&part!="..","unsafe source session map path");
        for(unsigned char c:part)Need((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.',"unsafe source session map byte");
        if(end==std::string::npos)break;i=end+1;}
    return result;
}
const char* Required(const idDict& dict,const char* name){const char* value=dict.GetString(name);Need(value&&*value,"source session required field is absent");return value;}
void Input(WolfSourceDeclarationRequest& request,const char* path,const char* identity,WolfSourceDeclarationRole role){
    request.resourcePath=path;request.loadIdentity=identity;request.role=role;request.policy=WolfSourceDeclaration_SelectedTypedPacketV1;
}
const char* WeaponPhaseName(int phase){
    switch(phase){
        case int(wolf2009::SourceWeaponPhase::None):return "None";
        case int(wolf2009::SourceWeaponPhase::Ready):return "Ready";
        case int(wolf2009::SourceWeaponPhase::Reloading):return "Reloading";
        case int(wolf2009::SourceWeaponPhase::Disabled):return "Disabled";
        default:return "Unknown";
    }
}
void PrintWeaponStatus(const idPlayer* player,const char* command){
    if(!active||!player){
        gameLocal.Printf("WOLF2009_WEAPON_STATUS command=%s time=%d query=0 reason=\"%s\"\n",
            command,gameLocal.time,!active?"no active source session":"no local player");
        return;
    }
    WolfSourceWeaponState state;idStr reason;
    if(!Wolf2009_QuerySourceWeapon(player,state,reason)){
        gameLocal.Printf("WOLF2009_WEAPON_STATUS command=%s time=%d player=%d query=0 reason=\"%s\"\n",
            command,gameLocal.time,player->entityNumber,reason.c_str());
        return;
    }
    // These are queried controller counters and an owned view descriptor, not
    // evidence of visible pixels or retail effects. Input is the native usercmd
    // snapshot; the controller's private attackHeld latch is not exposed here.
    gameLocal.Printf("WOLF2009_WEAPON_STATUS command=%s time=%d player=%d query=1 source_weapon=%s "
        "clip=%d reserve=%d phase=%d phase_name=%s reloading=%d has_view_model=%d input_attack=%d "
        "shots=%u reloads=%u empty_clicks=%u damage_calls=%u misses=%u frame_commands=%u "
        "last_hit_entity=%d last_shot_time=%d reload_end_time=%d unsupported=%d reason=\"%s\"\n",
        command,gameLocal.time,player->entityNumber,state.sourceWeapon?state.sourceWeapon:"<absent>",
        state.clip,state.reserve,state.phase,WeaponPhaseName(state.phase),
        int(state.phase==int(wolf2009::SourceWeaponPhase::Reloading)),int(state.hasViewModel),
        int((player->usercmd.buttons&BUTTON_ATTACK)!=0),state.shots,state.reloads,state.emptyClicks,
        state.damageCalls,state.misses,state.frameCommands,state.lastHitEntity,state.lastShotTime,
        state.reloadEndTime,int(state.unsupported),reason.c_str());
}
void WeaponStatus_f(const idCmdArgs& args){PrintWeaponStatus(gameLocal.GetLocalPlayer(),"wolf2009WeaponStatus");}
void DevEquipMP40_f(const idCmdArgs& args){
    idPlayer* player=gameLocal.GetLocalPlayer();
    if(!player){
        gameLocal.Printf("wolf2009DevEquipMP40: no local player\n");
        return;
    }
    idStr reason;
    if(!Wolf2009_DevEquipSourceSessionMP40(player,reason)){
        gameLocal.Warning("wolf2009DevEquipMP40 failed: %s",reason.c_str());
    }else{
        PrintWeaponStatus(player,"wolf2009DevEquipMP40");
    }
}
bool sourceCommandsRegistered=false;
void RemoveSourceSessionCommands(){
    if(sourceCommandsRegistered&&cmdSystem){
        cmdSystem->RemoveCommand("wolf2009DevEquipMP40");
        cmdSystem->RemoveCommand("wolf2009WeaponStatus");
    }
    sourceCommandsRegistered=false;
}
}
bool Wolf2009_BeginSourceSession(const char* mapFileName,idStr& reason){
    reason.Clear();
    if(active){
        reason="previous source session was not released before map startup";
        return false;
    }
    try{
        if(!mapFileName||idStr::Icmpn(mapFileName,"wolf2009/",9)!=0)return true;
        std::unique_ptr<idFile,CloseFile> file(fileSystem->OpenFileRead("wolf2009-session.cfg",false));
        if(!file)return true;
        const int length=file->Length();Need(length>0&&length<=32768,"source session descriptor byte budget");
        std::vector<char> text(std::size_t(length)+1,0);Need(file->Read(text.data(),length)==length,"short source session descriptor read");
        Need(std::memchr(text.data(),0,std::size_t(length))==NULL,"source session descriptor contains embedded NUL");
        idLexer parser(LEXFL_NOERRORS|LEXFL_NOFATALERRORS|LEXFL_NOWARNINGS|LEXFL_NOSTRINGCONCAT|LEXFL_ALLOWPATHNAMES);
        Need(parser.LoadMemory(text.data(),length,"wolf2009-session.cfg"),"source session parser refused input");
        idToken token;Need(parser.ReadToken(&token)&&token=="Wolf2009SourceSession","source session signature differs");
        Need(parser.ReadToken(&token)&&token=="1","unsupported source session descriptor version");
        // This descriptor is data only. Read a bounded quoted dictionary with
        // the lexer directly, without idParser's C preprocessor or a variant-
        // specific idDict::Parse overload.
        Need(parser.ReadToken(&token)&&token=="{","source session dictionary is absent");
        idDict config;
        for(;;){
            Need(parser.ReadToken(&token),"unterminated source session dictionary");
            if(token=="}")break;
            Need(token.type==TT_STRING&&config.GetNumKeyVals()<32,"source session requires bounded quoted keys");
            const idStr key=token;
            Need(!config.FindKey(key.c_str()),"duplicate source session key");
            Need(parser.ReadToken(&token)&&token.type==TT_STRING,"source session requires quoted values");
            config.Set(key.c_str(),token.c_str());
        }
        Need(!parser.ReadToken(&token)&&!parser.HadError(),"invalid trailing source session data");
        const std::set<std::string> keys={"policy","map","sourceMapName","player","startup","startupLibrary","startupKey",
            "globalMain","globalLocale","mapMain","mapLocale","clipResource","standBody","crouchBody",
            "mapEntitiesProfile","mapArrivalProfile","mapEntities","mapCollision","materialArchive",
            "mapGreeterProfile","mapGreeterModel","mapGreeterRig","mapGreeterVoiceLocale","mapExitProfile","mapAIProfile"};
        for(int i=0;i<config.GetNumKeyVals();++i)Need(keys.count(config.GetKeyVal(i)->GetKey().c_str())!=0,"unknown source session option");
        Need(idStr::Cmp(Required(config,"policy"),"NativePlayerOnlyV1")==0,"unsupported source session policy");
        std::unique_ptr<Session> pending(new Session);pending->map=MapName(mapFileName);
        Need(pending->map==MapName(Required(config,"map")),"source session descriptor targets a different map");
        Need(!gameLocal.isClient&&!gameLocal.isMultiplayer,"source session requires local single-player");
        WolfSourceDeclarationStartupRequest startup;startup.policy=WolfSourceDeclarationStartup_CompiledLocalizedV1;
        Input(startup.globalMain,Required(config,"globalMain"),"session.global.main",WolfSourceDeclaration_SelectedGlobal);
        Input(startup.globalLocale,Required(config,"globalLocale"),"session.global.locale",WolfSourceDeclaration_SelectedGlobal);
        Input(startup.mapMain,Required(config,"mapMain"),"session.map.main",WolfSourceDeclaration_SelectedMap);
        Input(startup.mapLocale,Required(config,"mapLocale"),"session.map.locale",WolfSourceDeclaration_SelectedMap);
        pending->declarations=Wolf2009_LoadSourceDeclarationStartup(startup,reason);NeedReason(bool(pending->declarations),reason,"source session declarations were not prepared");
        WolfSourcePlayerRequest request;request.policy=WolfSourcePlayer_NativeIdPlayerFirstPersonV1;
        request.playerDefinition=Required(config,"player");request.sourceMapName=Required(config,"sourceMapName");
        request.startupDeclaration=Required(config,"startup");request.startupLibrary=Required(config,"startupLibrary");
        request.startupLibraryKey=Required(config,"startupKey");request.exactClipResourcePath=Required(config,"clipResource");
        request.exactStandBodyName=Required(config,"standBody");request.exactCrouchBodyName=Required(config,"crouchBody");
        pending->player.reset(Wolf2009_PrepareSourcePlayer(*pending->declarations,request,reason));NeedReason(bool(pending->player),reason,"source session player was not prepared");
        const char* entitiesProfile=config.GetString("mapEntitiesProfile","");
        const char* arrivalProfile=config.GetString("mapArrivalProfile","");
        Need(!arrivalProfile[0]||entitiesProfile[0],"Source arrival requires an explicit map entity profile");
        const char* greeterProfile=config.GetString("mapGreeterProfile","");
        Need(!greeterProfile[0]||arrivalProfile[0],"Source greeter route requires an explicit arrival profile");
        const char* exitProfile=config.GetString("mapExitProfile","");
        const char* aiProfile=config.GetString("mapAIProfile","");
        Need(!aiProfile[0]||(arrivalProfile[0]&&!greeterProfile[0]),"Source AI shell requires arrival and exclusive greeter ownership");
        Need(!exitProfile[0]||greeterProfile[0]||aiProfile[0],"Source trainyard exit requires an explicit greeter owner");
        if(entitiesProfile&&*entitiesProfile){
            Need(idStr::Cmp(entitiesProfile,"MeasuredTrainyardIntroV1")==0&&request.sourceMapName==std::string("maps/game/trainyard/trainyard")&&pending->map=="wolf2009/main/proc/maps/game/trainyard/trainyard",
                "Source map entity profile/map selection differs");
            WolfSourceMapRequest map;map.profile=entitiesProfile;map.sourceMapName=request.sourceMapName;
            map.arrivalProfile=arrivalProfile;
            if(greeterProfile[0]){map.greeterProfile=greeterProfile;map.greeterModel=Required(config,"mapGreeterModel");map.greeterRig=Required(config,"mapGreeterRig");
                map.greeterVoiceLocale=Required(config,"mapGreeterVoiceLocale");}
            map.exitProfile=exitProfile;map.aiProfile=aiProfile;
            map.exactEntities=Required(config,"mapEntities");map.exactCollision=Required(config,"mapCollision");map.materialArchive=Required(config,"materialArchive");
            pending->mapRuntime=Wolf2009_PrepareSourceMapRuntime(*pending->declarations,map,reason);NeedReason(bool(pending->mapRuntime),reason,"source session map runtime was not prepared");
        }
        NeedReason(Wolf2009_InstallSourcePlayer(*pending->player,reason),reason,"source session player installation failed");active=std::move(pending);
        if(!sourceCommandsRegistered&&cmdSystem){
            cmdSystem->AddCommand("wolf2009DevEquipMP40",DevEquipMP40_f,CMD_FL_GAME|CMD_FL_CHEAT,"Equip MP40 in active source session");
            cmdSystem->AddCommand("wolf2009WeaponStatus",WeaponStatus_f,CMD_FL_GAME,"Report current source weapon state without changing it");
            sourceCommandsRegistered=true;
        }
        gameLocal.Printf("WOLF2009_SOURCE_SESSION_READY map=%s source_map=%s policy=NativePlayerOnlyV1 full_campaign=0\n",active->map.c_str(),request.sourceMapName);
        return true;
    }catch(const std::exception& error){reason=error.what();Wolf2009_ClearSourcePlayer();active.reset();RemoveSourceSessionCommands();return false;}
}
bool Wolf2009_AttachSourceSessionPlayer(idPlayer* player,idStr& reason){
    reason.Clear();if(!active)return true;
    const auto* profile=Wolf2009_SourcePlayerProfile(player);
    if(!profile){reason="source session player factory returned no source profile";return false;}
    const auto slot=profile->StartingWeaponSlot();
    if(slot>=profile->WeaponSlots().size()){reason="source session selected weapon slot exceeds source table";return false;}
    return Wolf2009_AttachSourceWeapon(player,*active->declarations,profile->WeaponSlots()[slot].c_str(),reason);
}
bool Wolf2009_DevEquipSourceSessionMP40(idPlayer* player,idStr& reason){
    reason.Clear();
    if(!active){reason="no active source session for dev equip";return false;}
    if(!player){reason="dev equip requires a live player";return false;}
    if(!active->declarations){reason="source session has no loaded declarations";return false;}
    const auto* profile=Wolf2009_SourcePlayerProfile(player);
    if(!profile){reason="player has no source player profile";return false;}
    // This is an explicit development equip, not the trainyard starting
    // inventory. Prepare a fresh view/presentation owner for each entity
    // generation; the published weapon retains it after this local ref closes.
    WolfSourceWeaponViewRequest request;Wolf2009_SourceMP40FixtureViewRequest(request);
    std::unique_ptr<WolfSourceWeaponView,ReleaseView> view(
        Wolf2009_CreateSourceWeaponView(*active->declarations,request,reason));
    if(!view)return false;
    WolfSourceWeaponVisual visual;
    if(!Wolf2009_SourceWeaponViewDescriptor(view.get(),visual,reason))return false;
    return Wolf2009_EquipSourceWeapon(player,*active->declarations,"weapon_mp40",&visual,32,32,reason);
}
void Wolf2009_EndSourceSession(){
    Wolf2009_ClearSourcePlayer();
    active.reset();
    RemoveSourceSessionCommands();
}
const WolfSourceDeclarationSet* Wolf2009_SourceSessionDeclarations(){return active?active->declarations.get():NULL;}
bool Wolf2009_HasSourceSession(){return bool(active);}
bool Wolf2009_SpawnSourceSessionMapEntities(idStr& reason){reason.Clear();return !active||!active->mapRuntime||active->mapRuntime->Spawn(reason);}
WolfSourceMapRuntime* Wolf2009_SourceSessionMapRuntime(){return active?active->mapRuntime.get():NULL;}
