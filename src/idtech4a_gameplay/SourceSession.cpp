// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourcePlayerProfile.h"
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
#include "SourceSession.h"

namespace {
void Need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
struct CloseFile {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
struct ReleaseView {void operator()(WolfSourceWeaponView* view)const{if(view)Wolf2009_ReleaseSourceWeaponView(view);}};
struct Session {
    std::unique_ptr<WolfSourceDeclarationSet> declarations;
    std::unique_ptr<WolfSourcePlayerPrepared> player;
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
        gameLocal.Printf("wolf2009DevEquipMP40: MP40 equipped\n");
    }
}
bool devEquipCommandRegistered=false;
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
            "globalMain","globalLocale","mapMain","mapLocale","clipResource","standBody","crouchBody"};
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
        pending->declarations=Wolf2009_LoadSourceDeclarationStartup(startup,reason);Need(bool(pending->declarations),reason.c_str());
        WolfSourcePlayerRequest request;request.policy=WolfSourcePlayer_NativeIdPlayerFirstPersonV1;
        request.playerDefinition=Required(config,"player");request.sourceMapName=Required(config,"sourceMapName");
        request.startupDeclaration=Required(config,"startup");request.startupLibrary=Required(config,"startupLibrary");
        request.startupLibraryKey=Required(config,"startupKey");request.exactClipResourcePath=Required(config,"clipResource");
        request.exactStandBodyName=Required(config,"standBody");request.exactCrouchBodyName=Required(config,"crouchBody");
        pending->player.reset(Wolf2009_PrepareSourcePlayer(*pending->declarations,request,reason));Need(bool(pending->player),reason.c_str());
        Need(Wolf2009_InstallSourcePlayer(*pending->player,reason),reason.c_str());active=std::move(pending);
        if(!devEquipCommandRegistered&&cmdSystem){
            cmdSystem->AddCommand("wolf2009DevEquipMP40",DevEquipMP40_f,CMD_FL_GAME|CMD_FL_CHEAT,"Equip MP40 in active source session");
            devEquipCommandRegistered=true;
        }
        gameLocal.Printf("WOLF2009_SOURCE_SESSION_READY map=%s source_map=%s policy=NativePlayerOnlyV1 full_campaign=0\n",active->map.c_str(),request.sourceMapName);
        return true;
    }catch(const std::exception& error){reason=error.what();Wolf2009_ClearSourcePlayer();active.reset();return false;}
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
    if(devEquipCommandRegistered&&cmdSystem){
        cmdSystem->RemoveCommand("wolf2009DevEquipMP40");
        devEquipCommandRegistered=false;
    }
}
const WolfSourceDeclarationSet* Wolf2009_SourceSessionDeclarations(){return active?active->declarations.get():NULL;}
bool Wolf2009_HasSourceSession(){return bool(active);}
