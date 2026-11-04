// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceMover.h"
#include "BrainStartup.h"
#include "Entities.h"
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"

namespace {
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
bool Refuse(idStr& reason,const char* text){reason=text;return false;}
void Close(idFile* file){if(file)fileSystem->CloseFile(file);}
typedef std::unique_ptr<idFile,void(*)(idFile*)> SourceFile;
typedef std::vector<unsigned char> Bytes;
std::string Text(const char* raw){Need(raw!=NULL,"Source mover explicit text is absent");std::size_t n=0;
    while(n<=4096&&raw[n])++n;Need(n>0&&n<=4096,"Source mover text byte budget");return std::string(raw,n);}
std::string Fold(std::string value){for(char& c:value){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}return value;}
bool SafeName(const std::string& name){if(name.empty()||name.size()>127)return false;
    for(std::size_t i=0;i<name.size();++i){const auto c=name[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(i&&c>='0'&&c<='9')))return false;}return true;}
std::string Resource(const char* raw){const std::string path=Text(raw);Need(path[0]!='/'&&path[0]!='\\',"Source mover needs a virtual resource path");
    for(std::size_t start=0;start<path.size();){const auto slash=path.find('/',start);const auto part=path.substr(start,slash==std::string::npos?slash:slash-start);
        Need(!part.empty()&&part!="."&&part!="..","Unsafe source mover resource segment");
        for(unsigned char c:part)Need((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.',"Unsafe source mover resource byte");
        if(slash==std::string::npos)break;start=slash+1;Need(start<path.size(),"Trailing source mover resource separator");}return path;}
std::string TypedName(const std::string& path,const char* type){Need(path.compare(0,9,"wolf2009/")==0,"Source mover requires typed source VFS resources");
    const auto slash=path.find('/',9);Need(slash!=std::string::npos&&slash>9,"Source mover typed mount is absent");
    const std::string rest=path.substr(slash+1),marker=std::string(type)+"/";Need(Fold(rest).compare(0,marker.size(),Fold(marker))==0,"Source mover resource type differs");
    Need(rest.size()>marker.size(),"Source mover typed resource name is absent");return Fold(rest.substr(marker.size()));}
std::string BrainKey(std::string value){value=Fold(value);const auto dot=value.rfind('.');
    if(dot!=std::string::npos){Need(value.substr(dot)==".brain","Source mover authored brain extension is unsupported");value.resize(dot);}
    if(value.compare(0,7,"brains/")!=0)value="brains/"+value;return value;}
std::uint64_t Fingerprint(const Bytes& bytes){std::uint64_t hash=14695981039346656037ull;for(unsigned char byte:bytes){hash^=byte;hash*=1099511628211ull;}return hash;}
Bytes Read(SourceFile& file,const std::string& path,std::size_t maximum){file.reset(fileSystem->OpenFileRead(path.c_str(),false));Need(bool(file),"Exact source mover input is absent");
    const int length=file->Length();Need(length>0&&std::size_t(length)<=maximum&&file->Tell()==0,"Source mover input byte/position budget");
    Bytes result(static_cast<std::size_t>(length));Need(file->Read(result.data(),length)==length&&file->Tell()==length&&file->Length()==length,"Short/changed source mover input");
    unsigned char extra=0;Need(file->Read(&extra,1)==0,"Source mover input exceeded its declared end");return result;}
const wolf2009::EntityProperty* Unique(const wolf2009::EntityRecord& entity,const char* key){const wolf2009::EntityProperty* result=NULL;
    for(const auto& field:entity.properties)if(Fold(field.key)==key){Need(!result,"Duplicate source mover instance field");result=&field;}return result;}
std::string SourceText(const wolf2009::EntityRecord& entity,const char* key,bool required=false){const auto* field=Unique(entity,key);
    if(!field){Need(!required,"Required source mover instance field is absent");return std::string();}
    Need(field->kind==wolf2009::EntityProperty::Text&&field->text.size()<=4096,"Source mover instance field is not bounded text");
    Need(!required||!field->text.empty(),"Required source mover instance text is empty");return field->text;}
void Finite(const idVec3& vector){for(int i=0;i<3;++i)Need(std::isfinite(vector[i])&&std::fabs(vector[i])<=10000000,"Source mover world vector is outside its finite profile");}
std::string Words(const float* values,unsigned count){std::ostringstream result;result.imbue(std::locale::classic());result<<std::setprecision(std::numeric_limits<float>::max_digits10);
    for(unsigned i=0;i<count;++i){if(i)result<<' ';result<<values[i];}return result.str();}
std::string SkinText(const idDeclSkin* skin){
    Need(skin&&skin->GetState()==DS_PARSED,"Source mover native skin is absent/defaulted");
    const int length=skin->GetTextLength();Need(length>0&&length<=(1<<20),"Source mover native skin text byte budget");
    std::vector<char> bytes(std::size_t(length)+1,0);skin->GetText(bytes.data());return std::string(bytes.data(),std::size_t(length));
}
struct CollisionLease {
    WolfSourceMoverCollisionBinding hooks;std::string model;bool retained=false;cmHandle_t handle=0;
    explicit CollisionLease(const WolfSourceMoverCollisionBinding& request):hooks(request),model(Resource(request.nativeModel)){
        Need(hooks.context&&hooks.retain&&hooks.release&&hooks.preflight,"Source mover collision needs a complete retained owner");
        Need(hooks.contents>0&&hooks.clipMask>0,"Source mover collision contents/mask must be explicitly qualified");
        idStr why;Need(hooks.preflight(hooks.context,why),why.IsEmpty()?"Source mover collision owner refused preparation":why.c_str());
        Need(collisionModelManager!=NULL,"Source mover native collision manager is absent");
        handle=idClipModel::CheckModel(model.c_str());Need(handle>0,"Source mover collision alias is not a non-world native CM model");
        idClipModel probe;Need(probe.LoadModel(model.c_str())&&probe.Handle()==handle&&!probe.IsRenderModel(),"Source mover collision alias did not load its native CM geometry");
        const auto& bounds=probe.GetBounds();Need(!bounds.IsCleared()&&bounds[0].x<bounds[1].x&&bounds[0].y<bounds[1].y,"Source mover collision model has empty planar extent");
        Finite(bounds[0]);Finite(bounds[1]);hooks.retain(hooks.context);retained=true;
    }
    ~CollisionLease(){if(retained)hooks.release(hooks.context);}
    bool Check(idStr& reason)const{if(!hooks.preflight(hooks.context,reason))return false;
        if(!collisionModelManager||idClipModel::CheckModel(model.c_str())!=handle)return Refuse(reason,"Source mover native collision identity changed");
        reason.Clear();return true;}
};
struct MoverData {
    SourceFile entitiesFile,brainFile;
    Bytes entitiesBytes,brainBytes;
    wolf2009::EntityDocument entities;
    wolf2009::EntityPlacement authored;
    wolf2009::BrainStartupPlan startup;
    wolf2009::LinearMoveDefinition move;
    std::shared_ptr<CollisionLease> collision;
    std::string entitiesPath,brainPath,name,sourceClass,sourceModel,sourceBrain,bind,authoredSkin,nativeModel,nativeSkin,skinText;
    unsigned index=0;std::uint64_t entitiesFingerprint=0,brainFingerprint=0;
    idVec3 origin,destination;idMat3 axis;
    const idRenderModel* renderModel=NULL;
    const idDeclSkin* skin=NULL;
    bool callerWorld=false,diagnosticVisual=false,typed=false;
    MoverData():entitiesFile(NULL,Close),brainFile(NULL,Close){}
    bool Check(idStr& reason)const{
        if(!collision||!collision->Check(reason))return false;
        if(renderModel&&(!renderModel->IsLoaded()||renderModel->IsDefaultModel()||nativeModel!=renderModel->Name()))return Refuse(reason,"Source mover native visual model changed");
        if(skin){try{if(declManager->FindSkin(nativeSkin.c_str(),false)!=skin||SkinText(skin)!=skinText)return Refuse(reason,"Source mover native skin declaration changed");}
            catch(const std::exception& error){return Refuse(reason,error.what());}}
        reason.Clear();return true;
    }
};
int liveMovers=0;const int MaximumMovers=256;
std::shared_ptr<const MoverData> spawning;
struct SpawnContext {explicit SpawnContext(std::shared_ptr<const MoverData> value){Need(!spawning,"Source mover Spawn reentry refused");spawning=std::move(value);}~SpawnContext(){spawning.reset();}};
std::shared_ptr<const MoverData> Prepare(const WolfSourceMoverRequest& request){
    Need(request.policy==WolfSourceMover_NativeDirectLinearPusherV1,"Explicit native source linear-pusher policy required");
    Need(request.placementPolicy==WolfSourceMoverPlacement_AuthoredUnboundWorldV1||request.placementPolicy==WolfSourceMoverPlacement_CallerEstablishedWorldV1,"Explicit source mover world-placement policy required");
    Need(request.visualPolicy==WolfSourceMoverVisual_QualifiedModelV1||request.visualPolicy==WolfSourceMoverVisual_NoneDiagnosticV1,"Explicit source mover visual policy required");
    std::shared_ptr<MoverData> data(new MoverData);data->entitiesPath=Resource(request.exactEntitiesPath);data->brainPath=Resource(request.exactBrainPath);
    if(request.dataPolicy==WolfSourceMoverData_TypedSourceVfsV1){TypedName(data->entitiesPath,"ENTS");TypedName(data->brainPath,"BRAI");data->typed=true;}
    else {
#ifdef WOLF2009_ANIMATION_V2_QA
        Need(request.dataPolicy==WolfSourceMoverData_OwnLooseFixtureV1&&data->entitiesPath.compare(0,22,"fixtures/source-mover/")==0&&data->brainPath.compare(0,22,"fixtures/source-mover/")==0,"Source mover loose fixture policy/path refused");
#else
        Need(false,"Source mover loose fixture data is unavailable in production");
#endif
    }
    data->entitiesBytes=Read(data->entitiesFile,data->entitiesPath,64u<<20);data->brainBytes=Read(data->brainFile,data->brainPath,4u<<20);
    data->entitiesFingerprint=Fingerprint(data->entitiesBytes);data->brainFingerprint=Fingerprint(data->brainBytes);
    Need(!request.expectedEntitiesBytes||request.expectedEntitiesBytes==data->entitiesBytes.size(),"Source mover ENTS byte identity differs");
    Need(!request.expectedEntitiesFingerprint||request.expectedEntitiesFingerprint==data->entitiesFingerprint,"Source mover ENTS fingerprint differs");
    Need(!request.expectedBrainBytes||request.expectedBrainBytes==data->brainBytes.size(),"Source mover BRAI byte identity differs");
    Need(!request.expectedBrainFingerprint||request.expectedBrainFingerprint==data->brainFingerprint,"Source mover BRAI fingerprint differs");
    data->entities=wolf2009::DecodeEntities4(data->entitiesBytes);Need(request.entityIndex<data->entities.entities.size(),"Source mover ENTS index exceeds its owning document");
    data->index=request.entityIndex;const auto& entity=data->entities.entities[data->index];
    const std::set<std::string> fields={"name","classname","model","origin","rotation","angle","skin","bind","brain"};
    for(const auto& field:entity.properties)Need(fields.count(Fold(field.key))!=0,"Source mover has an unimplemented instance field");
    data->name=SourceText(entity,"name",true);Need(SafeName(data->name),"Source mover name is unsafe for native registration");
    data->sourceClass=SourceText(entity,"classname",true);Need(Fold(data->sourceClass)=="func_mover","Source mover explicit class profile requires func_mover");
    data->sourceModel=SourceText(entity,"model",true);data->sourceBrain=SourceText(entity,"brain",true);data->bind=SourceText(entity,"bind");
    data->authoredSkin=SourceText(entity,"skin");
    if(request.nativeSkin&&*request.nativeSkin)data->nativeSkin=Resource(request.nativeSkin);
    Need(data->authoredSkin.empty()==data->nativeSkin.empty(),"Source mover authored skin requires its explicit qualified native skin");
    if(!data->nativeSkin.empty()){
        data->skin=declManager->FindSkin(data->nativeSkin.c_str(),false);data->skinText=SkinText(data->skin);
    }
    for(unsigned i=0;i<data->entities.entities.size();++i)if(i!=data->index){const auto candidate=SourceText(data->entities.entities[i],"name");
        Need(candidate.empty()||Fold(candidate)!=Fold(data->name),"Source mover name is ambiguous for native entity registration");}
    if(data->typed)Need(TypedName(data->brainPath,"BRAI")==BrainKey(data->sourceBrain),"Source mover exact BRAI key differs from its authored reference");
    data->authored=wolf2009::ReadEntityPlacement(entity);Need(data->authored.hasOrigin,"Source mover requires an explicit authored origin");
    Need(!data->authored.hasAngle||data->authored.angle==0,"Source mover nonzero angle-only placement is unqualified");
    data->callerWorld=request.placementPolicy==WolfSourceMoverPlacement_CallerEstablishedWorldV1;
    Need(data->bind.empty()||data->callerWorld,"Bound source mover requires an independently established world pose");
    for(unsigned i=0;i<3;++i)data->origin[i]=request.worldOrigin[i];Finite(data->origin);
    for(unsigned i=0;i<9;++i)data->axis[i/3][i%3]=request.worldAxis[i];
    for(unsigned i=0;i<3;++i){Finite(data->axis[i]);Need(std::fabs(data->axis[i].LengthSqr()-1)<.0001,"Source mover axis must be a unit rigid frame");}
    Need(std::fabs(data->axis[0]*data->axis[1])<.0001&&std::fabs(data->axis[0]*data->axis[2])<.0001&&std::fabs(data->axis[1]*data->axis[2])<.0001&&
        std::fabs(data->axis[0]*data->axis[1].Cross(data->axis[2])-1)<.0001,"Source mover axis is not a proper orthogonal frame");
    if(!data->callerWorld){for(unsigned i=0;i<3;++i)Need(data->origin[i]==data->authored.origin[i],"Source mover world origin differs from its unbound authored pose");
        for(unsigned i=0;i<9;++i)Need(request.worldAxis[i]==data->authored.rotation[i],"Source mover world axis differs from its unbound authored pose");}
    data->startup=wolf2009::DecodeDirectBrainStartupH1023(data->brainBytes);
    Need(data->startup.unexecutedNodeCount<=UINT_MAX&&data->startup.moveOutputRecords<=UINT_MAX,"Source mover graph count exceeds native reporting limits");
    data->move=wolf2009::ResolveSelfLinearMove(data->startup.move,data->entities,{data->origin.x,data->origin.y,data->origin.z});
    data->destination.Set(float(data->move.destination.x),float(data->move.destination.y),float(data->move.destination.z));Finite(data->destination);
    const double dx=double(data->destination.x)-data->origin.x,dy=double(data->destination.y)-data->origin.y,dz=double(data->destination.z)-data->origin.z;
    const double duration=std::sqrt(dx*dx+dy*dy+dz*dz)*1000.0/data->move.speed;
    Need(std::isfinite(duration)&&duration<=3600000,"Source mover exceeds its explicit one-hour native motion horizon");
    const float nativeSpeed=float(data->move.speed),nativeDistance=(data->destination-data->origin).Length();
    Need(std::isfinite(nativeSpeed)&&nativeSpeed>0&&std::isfinite(nativeDistance),"Source mover speed/distance exceeds native float arithmetic");
    if(dx!=0||dy!=0||dz!=0)Need(nativeDistance>0&&1000.0f*nativeDistance/nativeSpeed>=1.0f,
        "Source mover nonzero path underflows the native millisecond movement clock");
    data->collision=std::make_shared<CollisionLease>(request.collision);
    data->diagnosticVisual=request.visualPolicy==WolfSourceMoverVisual_NoneDiagnosticV1;
    if(!data->diagnosticVisual){data->nativeModel=Resource(request.nativeVisualModel);data->renderModel=renderModelManager->CheckModel(data->nativeModel.c_str());
        Need(data->renderModel&&data->renderModel->IsLoaded()&&!data->renderModel->IsDefaultModel()&&data->nativeModel==data->renderModel->Name(),"Source mover qualified visual model is unavailable");}
    else Need(!request.nativeVisualModel||!*request.nativeVisualModel,"Source mover no-visual diagnostic cannot carry a model");
    idStr reason;Need(data->Check(reason),reason.IsEmpty()?"Source mover preparation preflight failed":reason.c_str());return data;
}
}

struct WolfSourceMoverPrepared::Impl {std::shared_ptr<const MoverData> data;};
WolfSourceMoverPrepared::WolfSourceMoverPrepared(Impl* value):impl(value){}
WolfSourceMoverPrepared::~WolfSourceMoverPrepared(){delete impl;}

class idWolfSourceMover : public idMover {
  public:
    CLASS_PROTOTYPE(idWolfSourceMover);
    idWolfSourceMover():data(spawning),phase(WolfSourceMover_Prepared),spawnDeliveries(0),duplicateDeliveries(0),arrivals(0),consumed(0),blockedFrames(0),startTime(-1),lastBlockingEntity(-1){++liveMovers;}
    ~idWolfSourceMover(){
        // Unpublish the native clip before releasing the caller's source CM
        // lease. The base parametric destructor later destroys this now
        // unlinked clip; it never queries the released CM handle.
        physicsObj.UnlinkClip();data.reset();--liveMovers;
    }
    void Spawn(){
        Need(bool(data),"Source mover allocated without immutable prepared data");
        Need(GetBindMaster()==NULL&&physicsObj.GetClipModel()&&physicsObj.GetClipModel()->Handle()==data->collision->handle,"Source mover native Spawn changed its collision or binding");
        Need(GetPhysics()==&physicsObj&&physicsObj.IsPusher(),"Source mover did not use native parametric pusher physics");
        physicsObj.SetContents(data->collision->hooks.contents);physicsObj.SetClipMask(data->collision->hooks.clipMask);fl.networkSync=false;fl.neverDormant=true;
        Need(GetPhysics()->GetOrigin().Compare(data->origin,.0001f)&&GetPhysics()->GetAxis().Compare(data->axis,.0001f),"Source mover native placement differs after Spawn");
        if(data->renderModel)Need(GetRenderEntity()->hModel==data->renderModel,"Source mover native visual differs after Spawn");
        Need(GetRenderEntity()->customSkin==data->skin,"Source mover native skin differs after Spawn");
        if(data->diagnosticVisual)Need(GetRenderEntity()->hModel==NULL,"Source mover diagnostic unexpectedly acquired a visual model");
    }
    void Save(idSaveGame*)const{gameLocal.Error("Source mover save-state serialization is unimplemented");}
    void Restore(idRestoreGame*){gameLocal.Error("Source mover restore is unimplemented");}
    void Think() override {
        if(phase==WolfSourceMover_Unsupported)return;
        idStr reason;if(!Check(reason)){Fail(reason.c_str());return;}
        idMover::Think();
        if(auto* blocker=physicsObj.GetBlockingEntity()){++blockedFrames;lastBlockingEntity=blocker->entityNumber;}
    }
    bool Check(idStr& reason)const{
        if(!data||!data->Check(reason))return false;
        if(GetBindMaster()!=NULL||GetPhysics()!=&physicsObj||!physicsObj.IsPusher()||!physicsObj.GetClipModel()||
            physicsObj.GetClipModel()->Handle()!=data->collision->handle||physicsObj.GetContents()!=data->collision->hooks.contents||
            physicsObj.GetClipMask()!=data->collision->hooks.clipMask)return Refuse(reason,"Source mover native physics/collision capability changed");
        if(renderEntity.hModel!=data->renderModel)return Refuse(reason,"Source mover native entity visual model changed");
        if(renderEntity.customSkin!=data->skin)return Refuse(reason,"Source mover native entity skin changed");
        return true;
    }
    void Fail(const char* why){failure=why;phase=WolfSourceMover_Unsupported;
        const idVec3 current=physicsObj.GetOrigin();physicsObj.SetLinearExtrapolation(EXTRAPOLATION_NONE,gameLocal.time,0,current,vec3_origin,vec3_origin);BecomeInactive(TH_PHYSICS);}
    bool DeliverSpawn(idStr& reason){
        if(phase==WolfSourceMover_Unsupported)return Refuse(reason,failure.c_str());
        if(!Check(reason)){Fail(reason.c_str());return false;}
        if(spawnDeliveries){++duplicateDeliveries;reason.Clear();return true;}
        const double duration=double((data->destination-data->origin).Length())*1000.0/data->move.speed;
        if(gameLocal.time<0||duration>double(INT_MAX-gameLocal.time)-32)return Refuse(reason,"Source mover startup exceeds native simulation clock horizon");
        spawnDeliveries=1;startTime=gameLocal.time;phase=WolfSourceMover_Moving;MoveToPos(data->destination);reason.Clear();return true;
    }
    bool Query(WolfSourceMoverState& out,idStr& reason)const{
        if(!data)return Refuse(reason,"Source mover has no owning prepared data");
        if(phase!=WolfSourceMover_Unsupported&&!Check(reason))return false;
        WolfSourceMoverState state;state.phase=phase;state.sourceName=data->name.c_str();state.sourceClass=data->sourceClass.c_str();state.sourceModel=data->sourceModel.c_str();
        state.sourceBrain=data->sourceBrain.c_str();state.authoredBind=data->bind.c_str();state.authoredSkin=data->authoredSkin.c_str();state.destinationName=data->move.destinationName.c_str();state.nativeModel=data->nativeModel.c_str();state.nativeSkin=data->nativeSkin.c_str();state.nativeCollisionModel=data->collision->model.c_str();state.failure=failure.c_str();
        state.entityIndex=data->index;state.onSpawnNode=data->startup.onSpawnNodeId;state.moveNode=data->startup.moveNodeId;state.unexecutedNodes=unsigned(data->startup.unexecutedNodeCount);state.moveOutputRecords=unsigned(data->startup.moveOutputRecords);
        state.spawnDeliveries=spawnDeliveries;state.duplicateSpawnDeliveries=duplicateDeliveries;state.arrivals=arrivals;state.consumedArrivals=consumed;state.blockedFrames=blockedFrames;
        state.entitiesBytes=data->entitiesBytes.size();state.entitiesFingerprint=data->entitiesFingerprint;state.brainBytes=data->brainBytes.size();state.brainFingerprint=data->brainFingerprint;
        for(unsigned i=0;i<3;++i){state.origin[i]=physicsObj.GetOrigin()[i];state.start[i]=data->origin[i];state.destination[i]=data->destination[i];state.velocity[i]=physicsObj.GetLinearVelocity()[i];}
        state.sourceSpeed=float(data->move.speed);state.startTime=startTime;state.nativeEndTime=startTime<0?-1:physicsObj.GetLinearEndTime();state.lastBlockingEntity=lastBlockingEntity;
        state.nativePusher=physicsObj.IsPusher();state.collisionIsTraceModel=physicsObj.GetClipModel()->IsTraceModel();state.callerEstablishedWorld=data->callerWorld;state.visualDiagnostic=data->diagnosticVisual;state.typedSourceResources=data->typed;
        out=state;reason.Clear();return true;
    }
    bool TakeArrival(bool& pending,unsigned& node,idStr& reason){
        pending=false;node=0;if(phase==WolfSourceMover_Unsupported)return Refuse(reason,failure.c_str());if(!Check(reason))return false;
        if(arrivals>consumed){++consumed;pending=true;node=data->startup.moveNodeId;}reason.Clear();return true;
    }
  protected:
    void Event_SourceActivate(idEntity*){
        // Stock idMover Activate starts a spline on itself. This source
        // profile is admitted only by the level's explicit OnSpawn route.
        Fail("Generic Activate is unimplemented for the source direct-linear mover");
    }
    void DoneMoving() override {
        idMover::DoneMoving();
        if(phase==WolfSourceMover_Moving&&spawnDeliveries){
            if(!physicsObj.GetOrigin().Compare(data->destination,.01f)){Fail("Source mover native completion did not reach its source destination");return;}
            // The GPL completion resets the extrapolation/local origin to
            // dest_position, but its absolute current.origin can still be
            // the final float sample until the next physics tick. Publish
            // that same admitted endpoint snap now, before exposing arrival
            // to the source continuation. No per-frame/rider correction runs.
            physicsObj.SetOrigin(data->destination);UpdateVisuals();
            phase=WolfSourceMover_Arrived;++arrivals;
        }
    }
  private:
    std::shared_ptr<const MoverData> data;
    WolfSourceMoverPhase phase;
    unsigned spawnDeliveries,duplicateDeliveries,arrivals,consumed,blockedFrames;
    int startTime,lastBlockingEntity;std::string failure;
};
CLASS_DECLARATION(idMover,idWolfSourceMover)
    EVENT(EV_Activate,idWolfSourceMover::Event_SourceActivate)
END_CLASS
namespace {
idWolfSourceMover* Mover(idEntity* entity){return entity&&entity->IsType(idWolfSourceMover::Type)?static_cast<idWolfSourceMover*>(entity):NULL;}
const idWolfSourceMover* Mover(const idEntity* entity){return entity&&entity->IsType(idWolfSourceMover::Type)?static_cast<const idWolfSourceMover*>(entity):NULL;}
}
WolfSourceMoverPrepared* Wolf2009_PrepareSourceMover(const WolfSourceMoverRequest& request,idStr& reason){reason.Clear();try{
    std::unique_ptr<WolfSourceMoverPrepared::Impl> value(new WolfSourceMoverPrepared::Impl);value->data=Prepare(request);
    std::unique_ptr<WolfSourceMoverPrepared> out(new WolfSourceMoverPrepared(value.release()));return out.release();
}catch(const std::exception& error){reason=error.what();return NULL;}}
bool Wolf2009_SpawnPreparedSourceMover(const WolfSourceMoverPrepared& prepared,idEntity*& out,idStr& reason){out=NULL;reason.Clear();try{
    Need(prepared.impl&&prepared.impl->data,"Source mover preparation is empty");const auto& data=prepared.impl->data;
    Need(!gameLocal.isClient&&!gameLocal.isMultiplayer&&(gameLocal.GameState()==GAMESTATE_STARTUP||gameLocal.GameState()==GAMESTATE_ACTIVE),"Source mover requires initialized local SP map state");
    Need(liveMovers<MaximumMovers&&!gameLocal.FindEntity(data->name.c_str()),"Source mover live budget or native name is occupied");
    Need(data->Check(reason),reason.IsEmpty()?"Source mover prepared capability expired":reason.c_str());
    idDict args;args.Set("classname","wolf2009_source_mover");args.Set("spawnclass","idWolfSourceMover");args.Set("name",data->name.c_str());
    args.Set("origin",Words(data->origin.ToFloatPtr(),3).c_str());args.Set("rotation",Words(data->axis.ToFloatPtr(),9).c_str());
    args.Set("clipmodel",data->collision->model.c_str());args.Set("noclipmodel","1");
    const float speed=float(data->move.speed);args.Set("move_speed",Words(&speed,1).c_str());args.Set("move_time","0");args.Set("accel_time","0");args.Set("decel_time","0");
    args.Set("damage","0");args.Set("solid","1");args.Set("nopush","0");args.Set("networkSync","0");args.Set("neverDormant","1");
    if(!data->diagnosticVisual)args.Set("model",data->nativeModel.c_str());
    if(data->skin)args.Set("skin",data->nativeSkin.c_str());
    SpawnContext context(data);std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idWolfSourceMover::Type,&args));
    Need(entity&&Mover(entity.get())&&entity->entityNumber>=MAX_CLIENTS&&gameLocal.entities[entity->entityNumber]==entity.get(),"Source mover native entity allocation failed");
    Need(Mover(entity.get())->Check(reason),reason.IsEmpty()?"Source mover native post-Spawn capability changed":reason.c_str());out=entity.release();return true;
}catch(const std::exception& error){reason=error.what();return false;}}
bool Wolf2009_SpawnSourceMover(const WolfSourceMoverRequest& request,idEntity*& out,idStr& reason){
    std::unique_ptr<WolfSourceMoverPrepared> prepared(Wolf2009_PrepareSourceMover(request,reason));if(!prepared){out=NULL;return false;}return Wolf2009_SpawnPreparedSourceMover(*prepared,out,reason);}
bool Wolf2009_DeliverSourceMoverOnSpawn(idEntity* entity,idStr& reason){auto* mover=Mover(entity);return mover?mover->DeliverSpawn(reason):Refuse(reason,"Entity is not a native source mover");}
bool Wolf2009_QuerySourceMover(const idEntity* entity,WolfSourceMoverState& out,idStr& reason){const auto* mover=Mover(entity);return mover?mover->Query(out,reason):Refuse(reason,"Entity is not a native source mover");}
bool Wolf2009_TakeSourceMoverArrival(idEntity* entity,bool& pending,unsigned& node,idStr& reason){auto* mover=Mover(entity);if(!mover){pending=false;node=0;return Refuse(reason,"Entity is not a native source mover");}return mover->TakeArrival(pending,node,reason);}
bool Wolf2009_IsSourceMover(const idEntity* entity){return Mover(entity)!=NULL;}
int Wolf2009_LiveSourceMovers(){return liveMovers;}
