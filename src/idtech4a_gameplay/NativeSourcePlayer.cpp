// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "NativeSourcePlayer.h"
#include "NativeSourceWeapon.h"
#include "SourcePlayerProfile.h"
#include "SourceDeclarationSetRuntime.h"
#include "EngineSourcePlayerHull.h"
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool Refuse(idStr& reason,const char* why){reason=why;return false;}
void Close(idFile* f){if(f)fileSystem->CloseFile(f);}
struct PlayerData {
    std::shared_ptr<const wolf2009::SourcePlayerProfile> profile;
    wolf2009::SourcePlayerClip stand,crouch;
    idTraceModel standModel,crouchModel;
    std::shared_ptr<idFile> source;
    std::string clipPath;
};
std::shared_ptr<const PlayerData> installed,spawning;
int livePlayers=0;
bool Resource(const char* raw){
    if(!raw)return false;const std::string path(raw);if(path.size()>4096||path.compare(0,9,"wolf2009/"))return false;
    const auto typed=path.find('/',9);if(typed==std::string::npos||typed==9||path.compare(typed,6,"/HKXS/"))return false;
    for(std::size_t start=9;start<path.size();){const auto end=path.find('/',start);const auto part=path.substr(start,end==std::string::npos?end:end-start);if(part.empty()||part=="."||part=="..")return false;
        for(unsigned char c:part)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
        if(end==std::string::npos)return true;start=end+1;
    }return false;
}
std::string Required(const char* p){Need(p&&*p&&std::char_traits<char>::length(p)<=4096,"Explicit bounded source player request text required");return p;}
std::string Word(float x){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<float>::max_digits10)<<x;return out.str();}
bool Reference(const std::string& authored,const std::string& exact){return authored==exact+".hkx";}
void Build(const wolf2009::SourcePlayerClip& clip,idTraceModel& model){
    std::vector<float> xyz;xyz.reserve(clip.nativeLocalPoints.size()*3);for(const auto& v:clip.nativeLocalPoints)xyz.insert(xyz.end(),v.begin(),v.end());
    idStr why;Need(Wolf2009_BuildSourcePlayerTraceModel(xyz.data(),int(clip.nativeLocalPoints.size()),model,why),why.IsEmpty()?"Native source player hull creation failed":why.c_str());
    Need(model.type==TRM_CUSTOM&&model.isConvex&&model.numVerts>=4,"Native source player hull has an invalid topology type");
    // No source support vertex may be dropped or shifted by the native seam.
    Need(model.numVerts==int(clip.nativeLocalPoints.size()),"Native source player hull discarded selected support vertices");
    for(const auto& point:clip.nativeLocalPoints){bool found=false;for(int i=0;i<model.numVerts;++i)if(model.verts[i][0]==point[0]&&model.verts[i][1]==point[1]&&model.verts[i][2]==point[2]){found=true;break;}Need(found,"Native source player hull shifted a selected support vertex");}
}
unsigned StartingMask(const wolf2009::SourcePlayerProfile& p){unsigned mask=0;for(unsigned slot:p.StartingWeaponSlots())mask|=1u<<slot;return mask;}
struct SpawnContext {
    explicit SpawnContext(std::shared_ptr<const PlayerData> p){Need(!spawning,"Source player Spawn reentry refused");spawning=std::move(p);}
    ~SpawnContext(){spawning.reset();}
};
}
struct WolfSourcePlayerPrepared::Impl {std::shared_ptr<const PlayerData> data;};
WolfSourcePlayerPrepared::WolfSourcePlayerPrepared(Impl* p):impl(p){}
WolfSourcePlayerPrepared::~WolfSourcePlayerPrepared(){delete impl;}

class idWolfSourcePlayer : public idPlayer {
  public:
    CLASS_PROTOTYPE(idWolfSourcePlayer);
    idWolfSourcePlayer():data(spawning),sourceDucked(false){++livePlayers;}
    ~idWolfSourcePlayer(){Wolf2009_DetachSourceWeapon(this);data.reset();--livePlayers;}
    // Class spawn dispatch calls all base Spawn functions first. The immutable
    // data is captured by the constructor before idPlayer::Spawn/SetClipModel.
    void Spawn(){Need(bool(data),"Source player allocated without prepared owning context");InitializeSource();}
    void Save(idSaveGame*)const{gameLocal.Error("Source player save-state serialization is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Source player restore is unimplemented");}
    const PlayerData* Data()const{return data.get();}
    bool Ducked()const{return sourceDucked;}
    void InitializeSource(){if(!data)return;const auto& p=*data->profile;inventory.maxHealth=p.MaximumHealth();inventory.weapons=int(StartingMask(p));inventory.armor=0;inventory.maxarmor=0;health=p.Health();Wolf2009_ResetSourceWeaponForPlayer(this);
        idStr reason;bool handled=false;Need(Wolf2009_TrySetSourcePlayerClip(this,*static_cast<idPhysics_Player*>(GetPlayerPhysics()),handled,reason)&&handled,reason.IsEmpty()?"Source player respawn clip reset failed":reason.c_str());}
    void SetDucked(bool value){sourceDucked=value;}
  private:
    std::shared_ptr<const PlayerData> data;bool sourceDucked;
};
CLASS_DECLARATION(idPlayer,idWolfSourcePlayer)
END_CLASS
namespace {
idWolfSourcePlayer* Player(idEntity* p){return p&&p->IsType(idWolfSourcePlayer::Type)?static_cast<idWolfSourcePlayer*>(p):NULL;}
const idWolfSourcePlayer* Player(const idEntity* p){return p&&p->IsType(idWolfSourcePlayer::Type)?static_cast<const idWolfSourcePlayer*>(p):NULL;}
}
WolfSourcePlayerPrepared* Wolf2009_PrepareSourcePlayer(const WolfSourceDeclarationSet& set,const WolfSourcePlayerRequest& r,idStr& reason){try{
    Need(r.policy==WolfSourcePlayer_NativeIdPlayerFirstPersonV1,"Explicit native source player policy required");
    Need(Resource(r.exactClipResourcePath),"Exact typed source player HKXS path required");
    wolf2009::SourcePlayerProfileRequest request;request.policy=wolf2009::SourcePlayerPolicy::NativeIdPlayerFirstPersonV1;
    request.playerDefinition=Required(r.playerDefinition);request.sourceMapName=Required(r.sourceMapName);request.startupDeclaration=Required(r.startupDeclaration);
    request.startupLibrary=Required(r.startupLibrary);request.startupLibraryKey=Required(r.startupLibraryKey);
    std::shared_ptr<PlayerData> data(new PlayerData);data->profile=wolf2009::PrepareSourcePlayerProfile(set.Definitions(),request);
    Need(data->profile->UnsupportedStartupFields().empty(),"Selected map loadout has unimplemented active startup fields");
    data->clipPath=r.exactClipResourcePath;data->source=std::shared_ptr<idFile>(fileSystem->OpenFileRead(r.exactClipResourcePath,false,NULL),Close);
    Need(bool(data->source),"Source player HKXS resource missing");const int length=data->source->Length();Need(length>=208&&length<=(1<<20),"Source player HKXS byte limit exceeded");
    std::vector<unsigned char> bytes(std::size_t(length),0);Need(data->source->Read(bytes.data(),length)==length,"Source player HKXS read truncated");
    data->stand=wolf2009::DecodeSourcePlayerClip(bytes,Required(r.exactStandBodyName));data->crouch=wolf2009::DecodeSourcePlayerClip(bytes,Required(r.exactCrouchBodyName));
    Need(!r.expectedClipBytes||r.expectedClipBytes==data->stand.sourceBytes,"Source player HKXS byte identity differs");
    Need(!r.expectedClipFingerprint||r.expectedClipFingerprint==data->stand.sourceFingerprint,"Source player HKXS fingerprint differs");
    Need(Reference(data->profile->StandClipReference(),data->stand.selectedBodyName)&&Reference(data->profile->CrouchClipReference(),data->crouch.selectedBodyName),"Selected player clip names differ from authored exact references");
    Build(data->stand,data->standModel);Build(data->crouch,data->crouchModel);
    Need(data->standModel.bounds[0][2]<=data->crouchModel.bounds[0][2]+.001f&&data->standModel.bounds[1][2]>data->crouchModel.bounds[1][2],"Source player stand/crouch height ordering differs");
    std::unique_ptr<WolfSourcePlayerPrepared::Impl> p(new WolfSourcePlayerPrepared::Impl);p->data=std::move(data);std::unique_ptr<WolfSourcePlayerPrepared> out(new WolfSourcePlayerPrepared(p.release()));reason.Clear();return out.release();
}catch(const std::exception& e){reason=e.what();return NULL;}}
bool Wolf2009_InstallSourcePlayer(const WolfSourcePlayerPrepared& p,idStr& reason){
    if(!p.impl||!p.impl->data)return Refuse(reason,"Empty source player preparation");
    if(livePlayers||spawning)return Refuse(reason,"Cannot replace source player profile while source players exist");
    installed=p.impl->data;reason.Clear();return true;
}
void Wolf2009_ClearSourcePlayer(){installed.reset();}
bool Wolf2009_HasSourcePlayerProfile(){return bool(installed);}
bool Wolf2009_TrySpawnSourcePlayer(int clientNum,const idDict& supplied,idEntity*& out,bool& handled,idStr& reason){
    handled=bool(installed);out=NULL;if(!handled){reason.Clear();return true;}try{
    Need(!gameLocal.isMultiplayer&&!gameLocal.isClient&&clientNum==gameLocal.localClientNum&&clientNum>=0&&clientNum<MAX_CLIENTS,"Source player requires the local single-player reserved slot");
    Need(!gameLocal.entities[clientNum]&&!spawning,"Source player reserved slot is occupied or reentered");
    const auto* native=gameLocal.FindEntityDef("player_doommarine",false);Need(native!=NULL,"Independent native player support definition missing");
    idDict args=native->dict;args.Copy(supplied);args.SetInt("spawn_entnum",clientNum);args.Set("spawnclass","idWolfSourcePlayer");args.Set("classname","wolf2009_source_player");
    const auto& p=*installed->profile;args.SetInt("health",p.Health());args.SetInt("maxhealth",p.MaximumHealth());args.Set("mass",Word(p.Mass()).c_str());args.SetInt("current_weapon",-1);
    args.SetInt("armor",0);args.SetInt("maxarmor",0);
    // Source slots are NOT stock Doom weapon definitions. The source weapon
    // controller owns their classes; retaining source names here would invoke
    // Doom's mandatory script/projectile cache while this player is spawning.
    for(int i=0;i<MAX_WEAPONS;++i)args.Delete(va("def_weapon%d",i));
    args.Delete("weapon");args.Delete("weapon_nightmare");
    const auto& c=p.Controls();const char* keys[]={"pm_normalheight","pm_crouchheight","pm_normalviewheight","pm_crouchviewheight","pm_walkspeed","pm_runspeed","pm_crouchspeed","pm_jumpheight"};
    const float values[]={c.normalHeight,c.crouchHeight,c.normalEyeHeight,c.crouchEyeHeight,c.walkSpeed,c.runSpeed,c.crouchSpeed,c.jumpHeight};
    for(unsigned i=0;i<8;++i)args.Set(keys[i],Word(values[i]).c_str());args.Set("pm_stamina","0");
    SpawnContext context(installed);std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourcePlayer::Type,&args));
    Need(entity&&entity->entityNumber==clientNum&&entity.get()==gameLocal.entities[clientNum]&&Player(entity.get()),"Source player reserved-slot allocation failed");
    const auto* player=Player(entity.get());Need(player->Data()&&player->health==p.Health()&&player->inventory.maxHealth==p.MaximumHealth()&&unsigned(player->inventory.weapons)==StartingMask(p),"Source player native health/loadout differs after Spawn");
    out=entity.release();reason.Clear();return true;
}catch(const std::exception& e){reason=e.what();return false;}}
bool Wolf2009_TrySetSourcePlayerClip(idPlayer* owner,idPhysics_Player& physics,bool& handled,idStr& reason){
    auto* p=Player(owner);handled=p!=NULL;if(!handled){reason.Clear();return true;}try{
    Need(p->Data()!=NULL,"Source player clip has no prepared owner");
    std::unique_ptr<idClipModel> clip(new idClipModel(p->Data()->standModel));clip->Translate(physics.PlayerGetOrigin());physics.SetClipModel(clip.get(),1.0f);clip.release();p->SetDucked(false);reason.Clear();return true;
}catch(const std::exception& e){reason=e.what();return false;}}
bool Wolf2009_TrySourcePlayerDuck(idEntity* owner,idClipModel* clip,const idVec3& origin,bool wantsDuck,bool dead,bool& ducked,bool& handled,idStr& reason){
    auto* p=Player(owner);handled=p!=NULL;if(!handled){reason.Clear();return true;}try{
    Need(p->Data()&&clip,"Source player duck lacks prepared clip/owner");bool accepted=p->Ducked();
    if(!dead){if(wantsDuck)accepted=true;else if(accepted){idClipModel standing(p->Data()->standModel);
            if(gameLocal.clip.Contents(origin,&standing,clip->GetAxis(),p->GetPlayerPhysics()->GetClipMask(),p)==0)accepted=false;}}
    if(accepted!=p->Ducked()){clip->LoadModel(accepted?p->Data()->crouchModel:p->Data()->standModel);p->SetDucked(accepted);}
    ducked=accepted;reason.Clear();return true;
}catch(const std::exception& e){reason=e.what();return false;}}
bool Wolf2009_IsSourcePlayer(const idEntity* p){return Player(p)!=NULL;}
const wolf2009::SourcePlayerProfile* Wolf2009_SourcePlayerProfile(const idEntity* owner){const auto* p=Player(owner);return p&&p->Data()?p->Data()->profile.get():NULL;}
void Wolf2009_SourcePlayerInitialized(idPlayer* owner){auto* p=Player(owner);if(p)p->InitializeSource();}
bool Wolf2009_QuerySourcePlayer(const idEntity* owner,WolfSourcePlayerState& out,idStr& reason){const auto* p=Player(owner);if(!p||!p->Data())return Refuse(reason,"Entity is not an owning source player");
    const auto& d=*p->Data();const auto& f=*d.profile;WolfSourcePlayerState result;result.sourcePlayer=true;result.exactLocalHull=true;result.ducked=p->Ducked();result.bodyPlaceholder=true;
    result.health=p->health;result.maximumHealth=p->inventory.maxHealth;result.startingGold=f.StartingGold();result.selectedSourceWeapon=int(f.StartingWeaponSlot());result.sourceOwnedWeapons=StartingMask(f);result.nativeInventoryWeapons=unsigned(p->inventory.weapons);
    result.standVertices=d.standModel.numVerts;result.crouchVertices=d.crouchModel.numVerts;result.standEdges=d.standModel.numEdges;result.crouchEdges=d.crouchModel.numEdges;result.standPolygons=d.standModel.numPolys;result.crouchPolygons=d.crouchModel.numPolys;
    result.mass=f.Mass();result.keepDistance=f.KeepDistance();result.normalEyeHeight=f.Controls().normalEyeHeight;result.crouchEyeHeight=f.Controls().crouchEyeHeight;out=result;reason.Clear();return true;
}
int Wolf2009_LiveSourcePlayers(){return livePlayers;}
