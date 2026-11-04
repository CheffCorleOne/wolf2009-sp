// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceMap.h"
#include "NativeSourceMover.h"
#include "NativeSourceStaticSkin.h"
#include "NativeSourceDirector.h"
#include "NativeSourceRegistry.h"
#include "SourceMapBodies.h"
#include "SourceCollisionBinding.h"
#include "SourceDefinitions.h"
#include "TrainArrival.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationSetRuntime.h"
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void NeedReason(bool ok,const idStr& reason,const char* fallback){Need(ok,reason.IsEmpty()?fallback:reason.c_str());}
struct Close {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
typedef std::unique_ptr<idFile,Close> File;
std::vector<unsigned char> Read(idFile* file,unsigned maximum){Need(file&&file->Length()>0&&unsigned(file->Length())<=maximum,"Source map input byte budget");std::vector<unsigned char> bytes(std::size_t(file->Length()));Need(file->Read(bytes.data(),int(bytes.size()))==int(bytes.size()),"Short source map read");return bytes;}
std::string Text(const wolf2009::EntityRecord& entity,const char* key){const auto* value=entity.Find(key);Need(value&&value->kind==wolf2009::EntityProperty::Text,"Source map entity text is absent/unqualified");return value->text;}
std::string Prefix(const std::string& path,const char* type){const auto end=path.find('/',9);Need(path.compare(0,9,"wolf2009/")==0&&end!=std::string::npos&&path.compare(end+1,std::strlen(type),type)==0&&path[end+1+std::strlen(type)]=='/',"Source map typed path differs");return path.substr(0,end+1);}
void ClassProfile(const WolfSourceDeclarationSet& set){
    const auto source=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"func_mover");
    Need(source.opaqueFacts.empty()&&source.literalFields.size()==4,"Source func_mover has unadmitted fields/commands");
    const auto* spawn=source.Find("spawnclass");const auto* noPhysics=source.Find("noDefaultPhysics");const auto* forever=source.Find("rotateForever");const auto* name=source.Find("classname");
    Need(spawn&&spawn->encoding==wolf2009::SourceLiteralEncoding::CompiledString&&spawn->value.size()==1&&spawn->value[0].text=="idMover"&&
        name&&name->encoding==wolf2009::SourceLiteralEncoding::CompiledString&&name->value.size()==1&&name->value[0].text=="func_mover"&&
        noPhysics&&noPhysics->encoding==wolf2009::SourceLiteralEncoding::CompiledSignedInteger&&noPhysics->numericWords.size()==1&&noPhysics->numericWords[0]==1&&
        forever&&forever->encoding==wolf2009::SourceLiteralEncoding::CompiledSignedInteger&&forever->numericWords.size()==1&&forever->numericWords[0]==0,
        "Source mover class differs from the selected native parametric profile");
}
void LiteralText(const wolf2009::ResolvedSourceDefinition& source,const char* key,const char* value){const auto* field=source.Find(key);
    Need(field&&field->encoding==wolf2009::SourceLiteralEncoding::CompiledString&&field->value.size()==1&&field->value[0].text==value,"Arrival source class text/default differs");}
void LiteralInt(const wolf2009::ResolvedSourceDefinition& source,const char* key,unsigned value){const auto* field=source.Find(key);
    Need(field&&field->encoding==wolf2009::SourceLiteralEncoding::CompiledSignedInteger&&field->numericWords.size()==1&&field->numericWords[0]==value,"Arrival source class integer/default differs");}
void ArrivalClasses(const WolfSourceDeclarationSet& set){
    const auto source=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"func_static");
    Need(source.opaqueFacts.empty()&&source.literalFields.size()==11,"Arrival func_static has unadmitted effective fields");
    LiteralText(source,"spawnclass","idStaticEntity");LiteralText(source,"classname","func_static");
    for(const auto* key:{"keepContents","noclipmodel","alwaysOutside","globalLightShadowsOnly","isStreamed","alwaysInside"})LiteralInt(source,key,0);
    for(const auto* key:{"solid","renderSideOnly","noflood"})LiteralInt(source,key,1);
    const auto relay=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,"target_brainuse");
    Need(relay.opaqueFacts.empty()&&relay.literalFields.size()==3,"Arrival target_brainuse has unadmitted effective fields");
    LiteralText(relay,"spawnclass","rvTarget_BRAINUse");LiteralText(relay,"classname","target_brainuse");LiteralInt(relay,"noModel",1);
}
void InstanceKeys(const wolf2009::EntityRecord& source,const std::set<std::string>& supported){std::set<std::string> seen;
    for(const auto& property:source.properties){std::string key=property.key;for(char& c:key)if(c>='A'&&c<='Z')c+=32;
        Need(supported.count(key)&&seen.insert(key).second,"Arrival source instance has unadmitted/duplicate fields");}}
void InstanceInt(const wolf2009::EntityRecord& source,const char* key,int value){const auto* p=source.Find(key);Need(p&&p->kind==wolf2009::EntityProperty::Integer&&p->integer==value,"Arrival source instance integer differs");}
unsigned Index(const wolf2009::EntityDocument& document,const char* name){unsigned out=0,count=0;for(unsigned i=0;i<document.entities.size();++i){const auto* p=document.entities[i].Find("name");if(p&&p->kind==wolf2009::EntityProperty::Text&&p->text==name){out=i;++count;}}Need(count==1,"Arrival source name absent/ambiguous");return out;}
std::string Words(const float* values,unsigned count){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);for(unsigned i=0;i<count;++i){if(i)out<<' ';out<<values[i];}return out.str();}
struct BodyOwner {
    unsigned refs=1;File descriptor;std::string text,model;wolf2009::SourceCollisionBodyBinding binding;cmHandle_t handle=-1;
    bool Check(idStr& reason)const{try{
        Need(descriptor&&descriptor->Length()==int(text.size()),"Source body descriptor lifetime changed");
        Need(idClipModel::CheckModel(model.c_str())==handle&&handle>0,"Source body native handle changed");
        idBounds bounds;int contents=0;Need(collisionModelManager->GetModelBounds(handle,bounds)&&collisionModelManager->GetModelContents(handle,contents)&&std::uint32_t(contents)==binding.nativeContents,"Source body native metadata changed");
        for(unsigned i=0;i<3;++i)Need(std::isfinite(bounds[0][i])&&std::isfinite(bounds[1][i])&&bounds[0][i]<bounds[1][i],"Source body native bounds changed");reason.Clear();return true;
    }catch(const std::exception& error){reason=error.what();return false;}}
};
void Retain(const void* raw){auto* owner=const_cast<BodyOwner*>(static_cast<const BodyOwner*>(raw));Need(owner->refs<(std::numeric_limits<unsigned>::max)(),"Source body ref overflow");++owner->refs;}
void Release(const void* raw){auto* owner=const_cast<BodyOwner*>(static_cast<const BodyOwner*>(raw));Need(owner->refs>0,"Source body ref underflow");if(!--owner->refs)delete owner;}
bool Check(const void* raw,idStr& reason){return static_cast<const BodyOwner*>(raw)->Check(reason);}
struct ReleaseBody {void operator()(BodyOwner* owner)const{if(owner)Release(owner);}};
typedef std::unique_ptr<BodyOwner,ReleaseBody> Body;
Body PrepareBody(const WolfSourceMapRequest& map,const wolf2009::SourceMapBodyPlan& plan,const char* name){
    wolf2009::SourceCollisionBodyRequest request;request.policy=wolf2009::SourceCollisionBodyPolicy::NativeSolidEntityCylinderEnvelopeV1;
    request.resourcePath=map.exactCollision;for(char& c:request.resourcePath)if(c>='A'&&c<='Z')c+=32;
    request.bodyName=name;request.expectedBytes=plan.inventory.sourceBytes;request.expectedFingerprint=plan.inventory.sourceFingerprint;
    const auto path=wolf2009::SourceCollisionBodyRequestPath(request);Body body(new BodyOwner);body->descriptor.reset(fileSystem->OpenFileRead(path.c_str(),false));const auto bytes=Read(body->descriptor.get(),4096);body->text.assign(bytes.begin(),bytes.end());
    body->binding=wolf2009::ParseSourceCollisionBodyBinding(body->text);Need(wolf2009::SameSourceCollisionBodyRequest(body->binding.request,request),"Source map collision response echoed different selection");
    body->model=wolf2009::SourceCollisionBodyModelName(body->binding);body->handle=idClipModel::CheckModel(body->model.c_str());idStr reason;NeedReason(body->Check(reason),reason,"Native source body preflight failed");return body;
}
class StaticReceiver : public WolfSourceUseReceiver {
  public:
    Body body;std::unique_ptr<WolfSourceStaticSkin> skin;idRenderModel* model=NULL;const idDeclSkin* nativeSkin=NULL;
    std::string nativeModel,name;idVec3 origin;idMat3 axis;
    bool Validate(idEntity* entity,bool initiallyHidden,idStr& reason)const{
        try{
            Need(entity&&entity->GetType()==&idStaticEntity::Type&&name==entity->GetName(),"Unqualified native source static");
            NeedReason(body&&body->Check(reason)&&(!skin||skin->Preflight(reason)),reason,"Source static capability expired");
            Need(renderModelManager->CheckModel(nativeModel.c_str())==model&&entity->GetRenderEntity()->hModel==model&&entity->GetRenderEntity()->customSkin==nativeSkin,"Source static visual binding changed");
            Need(!entity->GetBindMaster()&&(!initiallyHidden||entity->IsHidden())&&entity->GetPhysics()->GetContents()==(entity->IsHidden()?0:CONTENTS_SOLID)&&entity->GetPhysics()->GetClipModel()&&
                entity->GetPhysics()->GetClipModel()->Handle()==body->handle&&entity->GetPhysics()->GetOrigin()==origin&&entity->GetPhysics()->GetAxis()==axis,
                "Source static native placement/collision/initial hide changed");reason.Clear();return true;
        }catch(const std::exception& error){reason=error.what();return false;}
    }
    bool Check(idEntity* entity,idEntity*,const WolfSourceEvent& event,idStr& reason)const override{
        if(event.targetName!=name||!event.useKey.empty()){reason="Source static Use name/key differs";return false;}return Validate(entity,true,reason);
    }
    bool Use(idEntity* entity,idEntity* source,const WolfSourceEvent& event,idStr& reason)override{
        if(!Check(entity,source,event,reason))return false;
        // Only the measured show transition is admitted, not stock Activate's
        // toggle, shader time/mode writes or arbitrary func_static behavior.
        entity->Show();
        if(entity->IsHidden()||entity->GetPhysics()->GetContents()!=CONTENTS_SOLID){reason="Native static Show did not commit visibility/solid response";return false;}return true;
    }
};
std::uint64_t nextMapGeneration=1;
}
struct WolfSourceMapRuntime::Impl {
    WolfSourceMapRequest request;wolf2009::SourceMapBodyPlan plan;File entities,collision;
    unsigned wagon=0;idEntityPtr<idEntity> mover;Body body;std::unique_ptr<WolfSourceStaticSkin> skin;
    std::string sourceModel,sourceSkin,sourceBrain,nativeModel,brainPath;
    unsigned staticIndices[3]={0,0,0},relayIndex=0;File arrivalBrain;std::vector<unsigned char> arrivalBrainBytes;
    std::vector<idEntityPtr<idEntity>> statics;idEntityPtr<idEntity> relay;
    std::vector<std::shared_ptr<StaticReceiver>> staticOwners;
    std::shared_ptr<WolfNativeSourceRegistry> registry;std::unique_ptr<WolfSourceDirector> director;
    WolfSourceMapState state;
    void Clear(){director.reset();if(relay.GetEntity())delete relay.GetEntity();for(auto& entity:statics)if(entity.GetEntity())delete entity.GetEntity();statics.clear();
        if(mover.GetEntity())delete mover.GetEntity();mover=NULL;relay=NULL;registry.reset();staticOwners.clear();skin.reset();body.reset();state.spawned=false;state.sourceSkin=false;
        state.sourceMovers=state.sourceStatics=state.sourceRelays=state.sourceDirectors=state.collisionShapes=0;state.unimplementedEntities=state.entities>3?state.entities-3:0;}
    ~Impl(){Clear();}
};
WolfSourceMapRuntime::WolfSourceMapRuntime(std::unique_ptr<Impl> value):impl(std::move(value)){}
WolfSourceMapRuntime::~WolfSourceMapRuntime(){}
std::unique_ptr<WolfSourceMapRuntime> Wolf2009_PrepareSourceMapRuntime(const WolfSourceDeclarationSet& set,const WolfSourceMapRequest& request,idStr& reason){reason.Clear();try{
    Need(request.profile=="MeasuredTrainyardIntroV1"&&request.sourceMapName=="maps/game/trainyard/trainyard","Explicit measured source map profile required");ClassProfile(set);
    Need(request.arrivalProfile.empty()||request.arrivalProfile=="MeasuredTrainyardArrivalV1","Unknown source arrival profile");
    std::unique_ptr<WolfSourceMapRuntime::Impl> out(new WolfSourceMapRuntime::Impl);out->request=request;
    const auto mount=Prefix(request.exactEntities,"ENTS");Need(mount==Prefix(request.exactCollision,"HKXS"),"Source map ENTS/HKXS must share a mount");
    out->entities.reset(fileSystem->OpenFileRead(request.exactEntities.c_str(),false));out->collision.reset(fileSystem->OpenFileRead(request.exactCollision.c_str(),false));
    const auto entities=Read(out->entities.get(),64u<<20),collision=Read(out->collision.get(),64u<<20);
    out->plan=wolf2009::DecodeSourceMapBodies(collision,entities,request.sourceMapName);
    Need(out->plan.otherUnboundBodies==0,"Source map has bodies without ownership classifications");unsigned matches=0;
    for(unsigned i=0;i<out->plan.entities.entities.size();++i){const auto* name=out->plan.entities.entities[i].Find("name");if(name&&name->kind==wolf2009::EntityProperty::Text&&name->text=="starting_boxcar_model"){out->wagon=i;++matches;}}
    Need(matches==1,"Source intro wagon is absent/ambiguous");const auto& wagon=out->plan.entities.entities[out->wagon];const auto placement=wolf2009::ReadEntityPlacement(wagon);
    Need(Text(wagon,"classname")=="func_mover"&&Text(wagon,"bind")=="starting_boxcar"&&placement.hasOrigin&&placement.hasRotation&&
        std::fabs(placement.origin[0]-256.38f)<.001f&&std::fabs(placement.origin[1]+80.02f)<.001f&&placement.origin[2]==256,"Measured wagon world placement differs");
    for(unsigned i=0;i<9;++i)Need(placement.rotation[i]==(i%4==0?1.f:0.f),"Measured wagon axis differs");
    out->sourceModel=Text(wagon,"model");out->sourceSkin=Text(wagon,"skin");out->sourceBrain=Text(wagon,"brain");
    if(out->sourceModel!="models/vehicles/box_car_a.md5r"||out->sourceSkin!="models/vehicles/box_car_full_in"||out->sourceBrain!="trainyard/movetrain.brain")
        throw std::runtime_error("Measured source intro asset selection differs: model="+out->sourceModel+" skin="+out->sourceSkin+" brain="+out->sourceBrain);
    unsigned templates=0;for(unsigned i=0;i<out->plan.inventory.bodies.size();++i)if(out->plan.inventory.bodies[i].rawName=="models/vehicles/box_car_a"){
        Need(out->plan.bindings[i].role==wolf2009::SourceMapBodyRole::ResourceNameUnbound&&out->plan.inventory.bodies[i].rawFilterInfo==0,"Wagon collision is not an unbound zero-filter source template");++templates;}
    Need(templates==1,"Wagon collision template absent/ambiguous");
    out->nativeModel=mount+"MODL/"+out->sourceModel;out->brainPath=mount+"BRAI/brains/"+out->sourceBrain.substr(0,out->sourceBrain.size()-6);
    if(!request.arrivalProfile.empty()){
        ArrivalClasses(set);
        const char* names[]={"layer12_func_static_2506","layer12_func_static_2233","layer12_func_static_2224"};const float y[]={-1088,-648,-204};
        const std::set<std::string> keys={"classname","name","model","origin","skin","hide","rendersideonly"};
        for(unsigned i=0;i<3;++i){out->staticIndices[i]=Index(out->plan.entities,names[i]);const auto& source=out->plan.entities.entities[out->staticIndices[i]];InstanceKeys(source,keys);
            Need(Text(source,"classname")=="func_static"&&Text(source,"model")=="models/vehicles/box_car.md5r"&&source.properties.size()==(i==2?7u:6u),"Measured arrival static selection differs");
            InstanceInt(source,"Hide",1);InstanceInt(source,"renderSideOnly",0);const auto pose=wolf2009::ReadEntityPlacement(source);
            Need(pose.hasOrigin&&!pose.hasRotation&&!pose.hasAngle&&pose.origin[0]==256&&pose.origin[1]==y[i]&&pose.origin[2]==256,"Measured arrival static placement differs");
            Need(i==2?Text(source,"skin")=="models/vehicles/box_car_in":!source.Find("skin"),"Measured arrival static skin differs");
        }
        unsigned templates=0;for(unsigned i=0;i<out->plan.inventory.bodies.size();++i)if(out->plan.inventory.bodies[i].rawName=="models/vehicles/box_car"){
            Need(out->plan.bindings[i].role==wolf2009::SourceMapBodyRole::ResourceNameUnbound&&out->plan.inventory.bodies[i].rawFilterInfo==0,"Arrival static source collision template differs");++templates;}
        Need(templates==1,"Arrival static source collision template absent/ambiguous");
        out->relayIndex=Index(out->plan.entities,"train_greeter_brainuse_1");Need(out->relayIndex==2031,"Measured source relay ENTS index differs");const auto& relay=out->plan.entities.entities[out->relayIndex];
        InstanceKeys(relay,{"classname","name","origin","rotation","target","usekey"});
        Need(relay.properties.size()==6&&Text(relay,"classname")=="target_brainuse"&&Text(relay,"target")=="train_greeter"&&Text(relay,"useKey")=="train_stop","Measured arrival relay selection differs");
        out->arrivalBrain.reset(fileSystem->OpenFileRead(out->brainPath.c_str(),false));out->arrivalBrainBytes=Read(out->arrivalBrain.get(),65536);
        // Data preparation must finish before any native entities can appear.
        wolf2009::LevelEntityRegistry sourceRegistry(out->plan.entities);
        wolf2009::DecodeTrainArrivalH1023(out->arrivalBrainBytes,"brains/trainyard/movetrain",sourceRegistry);
    }
    out->state.bodies=unsigned(out->plan.inventory.bodies.size());out->state.entities=unsigned(out->plan.entities.entities.size());out->state.instanceBodies=unsigned(out->plan.entityBodies);out->state.resourceBodies=unsigned(out->plan.resourceNameBodies);
    out->state.unimplementedEntities=out->state.entities>3?out->state.entities-3:0;
    return std::unique_ptr<WolfSourceMapRuntime>(new WolfSourceMapRuntime(std::move(out)));
}catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceMapRuntime>();}}
bool WolfSourceMapRuntime::Spawn(idStr& reason){reason.Clear();
    if(impl->state.spawned||impl->mover.GetEntity()){reason="Source map entities already spawned";return false;}
    try{
    Body body=PrepareBody(impl->request,impl->plan,"models/vehicles/box_car_a");
    const std::string exactModel=Prefix(impl->request.exactEntities,"ENTS")+"MODL/"+impl->sourceModel;auto* nativeModel=renderModelManager->CheckModel(exactModel.c_str());
    Need(nativeModel&&nativeModel->IsLoaded()&&!nativeModel->IsDefaultModel(),"Source map visual model is not resident");impl->nativeModel=nativeModel->Name();
    WolfSourceStaticSkinRequest skin;skin.exactModelResource=exactModel;skin.nativeModelName=impl->nativeModel;skin.materialArchive=impl->request.materialArchive;skin.authoredSkin=impl->sourceSkin;
    skin.visualPolicy=WolfSourceStaticSkinVisualPolicy::BaseDiffuseWithOmittedVerifiedModulationV1;
    auto preparedSkin=Wolf2009_PrepareSourceStaticSkin(skin,reason);NeedReason(bool(preparedSkin),reason,"Source wagon skin preparation failed");
    WolfSourceStaticSkinState appearance;NeedReason(preparedSkin->Query(appearance,reason),reason,"Source wagon appearance metadata unavailable");
    Need(appearance.authoredNoOp&&appearance.surfaces==10&&appearance.omittedModulationSurfaces==3&&appearance.omittedModulationTriangles==100,
        "Measured wagon appearance policy changed its admitted source surfaces");
    if(!impl->request.arrivalProfile.empty()){
        Need(nextMapGeneration&&(nextMapGeneration!=(std::numeric_limits<std::uint64_t>::max)()),"Native source map generation exhausted");
        impl->registry.reset(new WolfNativeSourceRegistry(impl->plan.entities,nextMapGeneration++));
        impl->statics.reserve(3);impl->staticOwners.reserve(3);
        const auto mount=Prefix(impl->request.exactEntities,"ENTS");
        for(unsigned i=0;i<3;++i){const auto& source=impl->plan.entities.entities[impl->staticIndices[i]];const auto pose=wolf2009::ReadEntityPlacement(source);
            std::shared_ptr<StaticReceiver> owner(new StaticReceiver);owner->name=Text(source,"name");owner->body=PrepareBody(impl->request,impl->plan,"models/vehicles/box_car");
            Need(!gameLocal.FindEntity(owner->name.c_str()),"Arrival static native name is occupied");
            const auto exactModel=mount+"MODL/"+Text(source,"model");owner->model=renderModelManager->CheckModel(exactModel.c_str());
            Need(owner->model&&owner->model->IsLoaded()&&!owner->model->IsDefaultModel(),"Arrival static original render model absent");owner->nativeModel=owner->model->Name();
            if(const auto* authoredSkin=source.Find("skin")){WolfSourceStaticSkinRequest skin;skin.exactModelResource=exactModel;skin.nativeModelName=owner->nativeModel;
                skin.materialArchive=impl->request.materialArchive;skin.authoredSkin=authoredSkin->text;skin.visualPolicy=WolfSourceStaticSkinVisualPolicy::BaseDiffuseWithOmittedVerifiedModulationV1;
                owner->skin=Wolf2009_PrepareSourceStaticSkin(skin,reason);NeedReason(bool(owner->skin),reason,"Arrival static authored skin unavailable");
                owner->nativeSkin=declManager->FindSkin(owner->skin->NativeAlias(),false);Need(owner->nativeSkin&&owner->nativeSkin->GetState()==DS_PARSED,"Arrival static skin is not parsed");}
            owner->origin=idVec3(pose.origin[0],pose.origin[1],pose.origin[2]);owner->axis=mat3_identity;
            idDict args;args.Set("classname","wolf2009_source_arrival_static");args.Set("name",owner->name.c_str());args.Set("model",owner->nativeModel.c_str());
            args.Set("origin",Words(pose.origin,3).c_str());args.Set("rotation",Words(pose.rotation,9).c_str());args.Set("clipmodel",owner->body->model.c_str());
            args.Set("noclipmodel","1");args.Set("hide","1");args.Set("solid","1");args.Set("networkSync","0");
            if(owner->skin)args.Set("skin",owner->skin->NativeAlias());
            std::unique_ptr<idEntity> entity(gameLocal.SpawnEntityType(idStaticEntity::Type,&args));Need(bool(entity),"Native arrival static allocation failed");
            WolfSourceEvent check;check.targetName=owner->name;NeedReason(owner->Check(entity.get(),NULL,check,reason),reason,"Native arrival static preflight failed");
            impl->registry->Bind(owner->name,"func_static",entity.get(),owner);idEntityPtr<idEntity> retained;retained=entity.release();impl->statics.push_back(retained);impl->staticOwners.push_back(std::move(owner));
        }
        const auto& source=impl->plan.entities.entities[impl->relayIndex];const auto pose=wolf2009::ReadEntityPlacement(source);const auto name=Text(source,"name");
        Need(!gameLocal.FindEntity(name.c_str()),"Arrival relay native name is occupied");idDict args;args.Set("classname","wolf2009_source_arrival_relay");args.Set("name",name.c_str());
        args.Set("noclipmodel","1");args.Set("networkSync","0");args.Set("origin",Words(pose.origin,3).c_str());args.Set("rotation",Words(pose.rotation,9).c_str());
        std::unique_ptr<idEntity> relay(gameLocal.SpawnEntityType(idTarget::Type,&args));Need(bool(relay),"Native arrival relay allocation failed");impl->relay=relay.release();
        WolfSourceDirectorRequest director;director.profile=impl->request.arrivalProfile;director.brainResource="brains/trainyard/movetrain";director.ownerName="starting_boxcar_model";
        director.entitiesBytes=impl->plan.entsBytes;director.entitiesFingerprint=impl->plan.entsFingerprint;director.brainBytes=impl->arrivalBrainBytes;
        impl->director=Wolf2009_PrepareSourceDirector(director,impl->plan.entities,impl->registry,reason);NeedReason(bool(impl->director),reason,"Native source director preparation failed");
        NeedReason(impl->director->BindRelay(impl->relay.GetEntity(),reason),reason,"Native source relay binding failed");
    }
    WolfSourceMoverRequest mover;mover.policy=WolfSourceMover_NativeDirectLinearPusherV1;mover.dataPolicy=WolfSourceMoverData_TypedSourceVfsV1;mover.placementPolicy=WolfSourceMoverPlacement_CallerEstablishedWorldV1;mover.visualPolicy=WolfSourceMoverVisual_QualifiedModelV1;
    mover.exactEntitiesPath=impl->request.exactEntities.c_str();mover.exactBrainPath=impl->brainPath.c_str();mover.nativeVisualModel=impl->nativeModel.c_str();mover.nativeSkin=preparedSkin->NativeAlias();mover.entityIndex=impl->wagon;
    const auto place=wolf2009::ReadEntityPlacement(impl->plan.entities.entities[impl->wagon]);for(unsigned i=0;i<3;++i)mover.worldOrigin[i]=place.origin[i];for(unsigned i=0;i<9;++i)mover.worldAxis[i]=place.rotation[i];
    mover.expectedEntitiesBytes=impl->plan.entsBytes;mover.expectedEntitiesFingerprint=impl->plan.entsFingerprint;
    mover.collision.nativeModel=body->model.c_str();mover.collision.context=body.get();mover.collision.retain=Retain;mover.collision.release=Release;mover.collision.preflight=Check;mover.collision.contents=3;mover.collision.clipMask=MASK_SOLID|CONTENTS_MOVEABLECLIP;
    idEntity* entity=NULL;NeedReason(Wolf2009_SpawnSourceMover(mover,entity,reason),reason,"Native source wagon spawn failed");impl->mover=entity;impl->body=std::move(body);impl->skin=std::move(preparedSkin);
    if(impl->registry)impl->registry->Bind("starting_boxcar_model","func_mover",entity,std::shared_ptr<WolfSourceUseReceiver>());
    NeedReason(Wolf2009_DeliverSourceMoverOnSpawn(entity,reason),reason,"Native source wagon OnSpawn failed");
    if(impl->director){NeedReason(impl->director->Start(entity,reason),reason,"Native source director start failed");impl->state.sourceStatics=3;impl->state.sourceRelays=1;impl->state.sourceDirectors=1;impl->state.unimplementedEntities-=4;}
    impl->state.spawned=true;impl->state.sourceMovers=1;impl->state.collisionShapes=unsigned(impl->body->binding.shapes);impl->state.sourceSkin=true;
    impl->state.omittedModulationSurfaces=appearance.omittedModulationSurfaces;impl->state.omittedModulationTriangles=appearance.omittedModulationTriangles;
    gameLocal.Printf("WOLF2009_SOURCE_MAP_ENTITIES_READY bodies=%u instance_bodies=%u resources=%u native_movers=1 source_statics=%u source_relays=%u source_directors=%u original_skin=1 source_body_shapes=%u unimplemented_ents=%u omitted_modulation_surfaces=%u omitted_modulation_triangles=%u retail_shader_parity=0 full_campaign=0\n",impl->state.bodies,impl->state.instanceBodies,impl->state.resourceBodies,impl->state.sourceStatics,impl->state.sourceRelays,impl->state.sourceDirectors,impl->state.collisionShapes,impl->state.unimplementedEntities,appearance.omittedModulationSurfaces,appearance.omittedModulationTriangles);return true;
}catch(const std::exception& error){reason=error.what();impl->Clear();return false;}}
bool WolfSourceMapRuntime::Query(WolfSourceMapState& state,idStr& reason)const{reason.Clear();try{if(impl->state.spawned){Need(impl->mover.GetEntity()&&impl->body&&impl->skin,"Source map entity lifetime expired");NeedReason(impl->body->Check(reason)&&impl->skin->Preflight(reason),reason,"Source map capability changed");
    if(impl->director){Need(impl->statics.size()==3&&impl->staticOwners.size()==3&&impl->relay.GetEntity(),"Source arrival entity lifetime expired");
        for(unsigned i=0;i<3;++i)NeedReason(impl->staticOwners[i]->Validate(impl->statics[i].GetEntity(),false,reason),reason,"Source arrival static capability changed");
        WolfSourceDirectorState director;NeedReason(impl->director->Query(director,reason),reason,"Source director lifetime expired");}}
    state=impl->state;return true;}catch(const std::exception& error){reason=error.what();return false;}}
idEntity* WolfSourceMapRuntime::PrimaryMover()const{return impl->mover.GetEntity();}
WolfSourceDirector* WolfSourceMapRuntime::Director()const{return impl->director.get();}
