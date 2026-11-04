// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceMap.h"
#include "NativeSourceMover.h"
#include "NativeSourceStaticSkin.h"
#include "SourceMapBodies.h"
#include "SourceCollisionBinding.h"
#include "SourceDefinitions.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationSetRuntime.h"
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
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
}
struct WolfSourceMapRuntime::Impl {
    WolfSourceMapRequest request;wolf2009::SourceMapBodyPlan plan;File entities,collision;
    unsigned wagon=0;idEntityPtr<idEntity> mover;Body body;std::unique_ptr<WolfSourceStaticSkin> skin;
    std::string sourceModel,sourceSkin,sourceBrain,nativeModel,brainPath;
    WolfSourceMapState state;
    ~Impl(){if(mover.GetEntity())delete mover.GetEntity();}
};
WolfSourceMapRuntime::WolfSourceMapRuntime(std::unique_ptr<Impl> value):impl(std::move(value)){}
WolfSourceMapRuntime::~WolfSourceMapRuntime(){}
std::unique_ptr<WolfSourceMapRuntime> Wolf2009_PrepareSourceMapRuntime(const WolfSourceDeclarationSet& set,const WolfSourceMapRequest& request,idStr& reason){reason.Clear();try{
    Need(request.profile=="MeasuredTrainyardIntroV1"&&request.sourceMapName=="maps/game/trainyard/trainyard","Explicit measured source map profile required");ClassProfile(set);
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
    out->state.bodies=unsigned(out->plan.inventory.bodies.size());out->state.entities=unsigned(out->plan.entities.entities.size());out->state.instanceBodies=unsigned(out->plan.entityBodies);out->state.resourceBodies=unsigned(out->plan.resourceNameBodies);
    out->state.unimplementedEntities=out->state.entities>3?out->state.entities-3:0;
    return std::unique_ptr<WolfSourceMapRuntime>(new WolfSourceMapRuntime(std::move(out)));
}catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceMapRuntime>();}}
bool WolfSourceMapRuntime::Spawn(idStr& reason){reason.Clear();try{
    Need(!impl->state.spawned&&!impl->mover.GetEntity(),"Source map entities already spawned");
    wolf2009::SourceCollisionBodyRequest request;request.policy=wolf2009::SourceCollisionBodyPolicy::NativeSolidEntityCylinderEnvelopeV1;
    request.resourcePath=impl->request.exactCollision;for(char& c:request.resourcePath)if(c>='A'&&c<='Z')c+=32;
    request.bodyName="models/vehicles/box_car_a";request.expectedBytes=impl->plan.inventory.sourceBytes;request.expectedFingerprint=impl->plan.inventory.sourceFingerprint;
    const auto path=wolf2009::SourceCollisionBodyRequestPath(request);Body body(new BodyOwner);body->descriptor.reset(fileSystem->OpenFileRead(path.c_str(),false));const auto bytes=Read(body->descriptor.get(),4096);body->text.assign(bytes.begin(),bytes.end());
    body->binding=wolf2009::ParseSourceCollisionBodyBinding(body->text);Need(wolf2009::SameSourceCollisionBodyRequest(body->binding.request,request),"Source map collision response echoed different selection");
    body->model=wolf2009::SourceCollisionBodyModelName(body->binding);body->handle=idClipModel::CheckModel(body->model.c_str());Need(body->Check(reason),reason.IsEmpty()?"Native source body preflight failed":reason.c_str());
    const std::string exactModel=impl->nativeModel;auto* nativeModel=renderModelManager->CheckModel(exactModel.c_str());
    Need(nativeModel&&nativeModel->IsLoaded()&&!nativeModel->IsDefaultModel(),"Source map visual model is not resident");impl->nativeModel=nativeModel->Name();
    WolfSourceStaticSkinRequest skin;skin.exactModelResource=exactModel;skin.nativeModelName=impl->nativeModel;skin.materialArchive=impl->request.materialArchive;skin.authoredSkin=impl->sourceSkin;
    skin.visualPolicy=WolfSourceStaticSkinVisualPolicy::BaseDiffuseWithOmittedVerifiedModulationV1;
    auto preparedSkin=Wolf2009_PrepareSourceStaticSkin(skin,reason);Need(bool(preparedSkin),reason.IsEmpty()?"Source wagon skin preparation failed":reason.c_str());
    WolfSourceStaticSkinState appearance;Need(preparedSkin->Query(appearance,reason),reason.IsEmpty()?"Source wagon appearance metadata unavailable":reason.c_str());
    Need(appearance.authoredNoOp&&appearance.surfaces==10&&appearance.omittedModulationSurfaces==3&&appearance.omittedModulationTriangles==100,
        "Measured wagon appearance policy changed its admitted source surfaces");
    WolfSourceMoverRequest mover;mover.policy=WolfSourceMover_NativeDirectLinearPusherV1;mover.dataPolicy=WolfSourceMoverData_TypedSourceVfsV1;mover.placementPolicy=WolfSourceMoverPlacement_CallerEstablishedWorldV1;mover.visualPolicy=WolfSourceMoverVisual_QualifiedModelV1;
    mover.exactEntitiesPath=impl->request.exactEntities.c_str();mover.exactBrainPath=impl->brainPath.c_str();mover.nativeVisualModel=impl->nativeModel.c_str();mover.nativeSkin=preparedSkin->NativeAlias();mover.entityIndex=impl->wagon;
    const auto place=wolf2009::ReadEntityPlacement(impl->plan.entities.entities[impl->wagon]);for(unsigned i=0;i<3;++i)mover.worldOrigin[i]=place.origin[i];for(unsigned i=0;i<9;++i)mover.worldAxis[i]=place.rotation[i];
    mover.expectedEntitiesBytes=impl->plan.entsBytes;mover.expectedEntitiesFingerprint=impl->plan.entsFingerprint;
    mover.collision.nativeModel=body->model.c_str();mover.collision.context=body.get();mover.collision.retain=Retain;mover.collision.release=Release;mover.collision.preflight=Check;mover.collision.contents=3;mover.collision.clipMask=MASK_SOLID|CONTENTS_MOVEABLECLIP;
    idEntity* entity=NULL;Need(Wolf2009_SpawnSourceMover(mover,entity,reason),reason.IsEmpty()?"Native source wagon spawn failed":reason.c_str());impl->mover=entity;impl->body=std::move(body);impl->skin=std::move(preparedSkin);
    Need(Wolf2009_DeliverSourceMoverOnSpawn(entity,reason),reason.IsEmpty()?"Native source wagon OnSpawn failed":reason.c_str());
    impl->state.spawned=true;impl->state.sourceMovers=1;impl->state.collisionShapes=unsigned(impl->body->binding.shapes);impl->state.sourceSkin=true;
    impl->state.omittedModulationSurfaces=appearance.omittedModulationSurfaces;impl->state.omittedModulationTriangles=appearance.omittedModulationTriangles;
    gameLocal.Printf("WOLF2009_SOURCE_MAP_ENTITIES_READY bodies=%u instance_bodies=%u resources=%u native_movers=1 original_skin=1 source_body_shapes=%u unimplemented_ents=%u omitted_modulation_surfaces=%u omitted_modulation_triangles=%u retail_shader_parity=0 full_campaign=0\n",impl->state.bodies,impl->state.instanceBodies,impl->state.resourceBodies,impl->state.collisionShapes,impl->state.unimplementedEntities,appearance.omittedModulationSurfaces,appearance.omittedModulationTriangles);return true;
}catch(const std::exception& error){reason=error.what();return false;}}
bool WolfSourceMapRuntime::Query(WolfSourceMapState& state,idStr& reason)const{reason.Clear();try{if(impl->state.spawned){Need(impl->mover.GetEntity()&&impl->body&&impl->skin,"Source map entity lifetime expired");Need(impl->body->Check(reason)&&impl->skin->Preflight(reason),reason.IsEmpty()?"Source map capability changed":reason.c_str());}state=impl->state;return true;}catch(const std::exception& error){reason=error.what();return false;}}
idEntity* WolfSourceMapRuntime::PrimaryMover()const{return impl->mover.GetEntity();}
