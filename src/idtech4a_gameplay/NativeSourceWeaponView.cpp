// SPDX-License-Identifier: GPL-3.0-or-later
// The original weapon command stream belongs to SourceWeaponController. This
// adapter supplies its native model/skin and delegates presentation commands;
// no native animation command is allowed to fire a second attack or sound.
#include "NativeSourceWeaponView.h"
#include "NativeSourceWeaponPresentation.h"
#include "SourceWeaponProfile.h"
#include "SourceActorBinding.h"
#include "AnimationWireV2.h"
#include "EngineAnimationSet.h"
#include "RenderData.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationSetRuntime.h"

namespace {
namespace actor=wolf2009::actor;
namespace wire=wolf2009::animation2;
namespace render=wolf2009::idtech4a;
void Need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
struct Close {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
typedef std::unique_ptr<idFile,Close> File;
std::string Text(const char* value){Need(value!=NULL,"explicit MP40 view request text is absent");
    std::size_t n=0;while(n<=4096&&value[n])++n;Need(n&&n<=4096,"MP40 view request string budget");
    const std::string result(value,n);for(unsigned char c:result)Need(c>=32&&c<127&&c!='\"'&&c!='{'&&c!='}',"unsafe MP40 view request string");return result;}
std::string Fold(std::string value){for(char& c:value){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}return value;}
std::string Name(std::string value){value=Fold(value);const auto dot=value.rfind('.');if(dot!=std::string::npos)value.resize(dot);return value;}
std::string Path(const char* raw,const char* type){const auto parsed=render::ParseResourcePath(Text(raw));Need(parsed.type==type,"MP40 view resource has the wrong explicit type");return parsed.Path(type,parsed.name);}
actor::Bytes Read(idFile* file,std::size_t maximum){Need(file!=NULL,"MP40 view exact source file is absent");const int size=file->Length();
    Need(size>0&&std::size_t(size)<=maximum&&file->Tell()==0,"MP40 view source byte/position budget");actor::Bytes result(static_cast<std::size_t>(size));
    std::size_t at=0;while(at<result.size()){const int n=file->Read(result.data()+at,int(result.size()-at));Need(n>0&&std::size_t(n)<=result.size()-at,"short MP40 source read");at+=std::size_t(n);}
    unsigned char extra=0;Need(file->Read(&extra,1)==0&&file->Tell()==size&&file->Length()==size,"MP40 source EOF/identity changed");return result;}
actor::Bytes Read(const std::string& path,std::size_t maximum){File file(fileSystem->OpenFileRead(path.c_str(),false));return Read(file.get(),maximum);}
std::string Scalar(const wolf2009::ResolvedSourceDefinition& definition,const char* key){const auto* value=definition.Find(key);
    Need(value&&(value->encoding==wolf2009::SourceLiteralEncoding::PlainText||value->encoding==wolf2009::SourceLiteralEncoding::CompiledString)&&value->value.size()==1,"MP40 model/weapon field is not one qualified source string");return value->value[0].text;}
void Selected(const wolf2009::ResolvedSourceDefinition& definition,const wolf2009::CompiledDeclarationNamespace& names){
    Need(!definition.declarations.empty(),"MP40 view requires owning selected declarations");
    for(const auto& origin:definition.declarations){const auto& entry=origin.member.compiledRecord;Need(entry&&entry->IsUnmodified(),"MP40 view requires immutable compiled declaration provenance");
        const auto found=names.Lookup(origin.declarationKind,origin.declarationName);Need(found.status==wolf2009::CompiledCatalogLookupStatus::Unique&&found.matches.size()==1,"MP40 view declaration is not uniquely selected");
        const auto& actual=found.matches[0];Need(actual.IsUnmodified()&&actual.document==entry->document&&actual.source==entry->source&&actual.catalogOrdinal==entry->catalogOrdinal&&
            actual.loadOrdinal==entry->loadOrdinal&&actual.documentOrdinal==entry->documentOrdinal&&actual.section==entry->section&&actual.group==entry->group&&actual.record==entry->record&&actual.type==entry->type&&actual.name==entry->name&&
            actual.typeSpan.begin==entry->typeSpan.begin&&actual.typeSpan.end==entry->typeSpan.end&&actual.nameSpan.begin==entry->nameSpan.begin&&actual.nameSpan.end==entry->nameSpan.end&&
            actual.bodySpan.begin==entry->bodySpan.begin&&actual.bodySpan.end==entry->bodySpan.end&&actual.envelopeSpan.begin==entry->envelopeSpan.begin&&actual.envelopeSpan.end==entry->envelopeSpan.end,
            "MP40 view declaration belongs to a different selected source");}}
void ZeroOffset(const wolf2009::ResolvedSourceDefinition& definition){const auto* value=definition.Find("offset");if(!value)return;
    Need(value->encoding==wolf2009::SourceLiteralEncoding::PlainText,"MP40 model offset is not a qualified literal");std::string text;
    for(const auto& token:value->value){Need(!token.brace,"MP40 model offset contains a block");if(!text.empty())text+=' ';text+=token.text;}
    unsigned parentheses=0,commas=0;for(char& c:text){if(c=='('||c==')'){++parentheses;c=' ';}else if(c==','){++commas;c=' ';}}
    Need((parentheses==0||parentheses==2)&&(commas==0||commas==2),"MP40 model offset grammar is unsupported");
    std::istringstream input(text);input.imbue(std::locale::classic());double a=1,b=1,c=1;char extra=0;
    Need(bool(input>>a>>b>>c)&&!(input>>extra)&&a==0&&b==0&&c==0,"MP40 view currently requires the authored zero model offset");}
std::string DeclarationText(const idDecl* definition){Need(definition&&definition->GetState()!=DS_DEFAULTED,"MP40 native declaration is absent/defaulted");const int n=definition->GetTextLength();
    Need(n>0&&n<=65536,"MP40 native declaration text budget");std::vector<char> bytes(std::size_t(n)+1,0);definition->GetText(bytes.data());return std::string(bytes.data(),std::size_t(n));}
const idDecl* WithoutParsing(declType_t type,const std::string& name){if(const auto* found=declManager->FindDeclWithoutParsing(type,name.c_str(),false))return found;
    for(int i=0;i<declManager->GetNumDecls(type);++i){const auto* d=declManager->DeclByIndex(type,i,false);if(d&&name==d->GetName())return d;}return NULL;}
std::string Hex(std::uint64_t value){std::string result(16,'0');for(int i=15;i>=0;--i){result[std::size_t(i)]="0123456789abcdef"[value&15];value>>=4;}return result;}
std::string HashName(const char* domain,const std::string& text){return std::string("wolf2009/weapon-view/")+domain+"/"+Hex(wire::Fingerprint(reinterpret_cast<const unsigned char*>(text.data()),text.size()));}
std::map<std::string,std::string> ownedDeclarations;
int liveViews=0;
void Declare(declType_t type,const std::string& name,const std::string& text){const auto old=ownedDeclarations.find(name);
    Need(old==ownedDeclarations.end()||old->second==text,"MP40 view native declaration fingerprint collision");
    const idDecl* existing=WithoutParsing(type,name);
    if(old==ownedDeclarations.end()){Need(!existing,"MP40 view native declaration namespace is already occupied");Need(ownedDeclarations.size()<4096,"MP40 view native declaration registry budget");
        idDecl* created=declManager->CreateNewDecl(type,name.c_str(),"<wolf2009-source-weapon-view>");Need(created!=NULL,"MP40 view native declaration allocation failed");created->SetText(text.c_str());created->Invalidate();ownedDeclarations.emplace(name,text);
    }else Need(existing&&DeclarationText(existing)==text,"MP40 view native declaration text changed");}
template<class T>struct Aligned {T* data;explicit Aligned(int count):data(static_cast<T*>(Mem_Alloc16(std::size_t(count)*sizeof(T)))){Need(data!=NULL,"MP40 native aligned pose allocation failed");}~Aligned(){Mem_Free16(data);}Aligned(const Aligned&)=delete;Aligned& operator=(const Aligned&)=delete;};
struct Snapshot {
    idRenderModel* value=NULL;
    explicit Snapshot(idRenderModel* model,const idDeclSkin* skin=NULL){const int count=model->NumJoints();Need(count>0&&count<=256&&model->GetDefaultPose()&&model->GetJoints(),"MP40 native model default hierarchy is absent");
        Aligned<idJointQuat> local(count);Aligned<idJointMat> global(count);std::vector<int> parents;parents.reserve(std::size_t(count));
        const auto* joints=model->GetJoints();for(int i=0;i<count;++i){local.data[i]=model->GetDefaultPose()[i];int parent=-1;
            if(joints[i].parent){for(int j=0;j<i;++j)if(joints[i].parent==&joints[j]){parent=j;break;}Need(parent>=0,"MP40 native joint parent is not preceding");}parents.push_back(parent);}
        SIMDProcessor->ConvertJointQuatsToJointMats(global.data,local.data,count);if(count>1)SIMDProcessor->TransformJoints(global.data,parents.data(),1,count-1);
        renderEntity_t entity;std::memset(&entity,0,sizeof(entity));entity.hModel=model;entity.numJoints=count;entity.joints=global.data;entity.customSkin=skin;entity.axis.Identity();
        for(int i=0;i<4;++i)entity.shaderParms[i]=1;value=model->InstantiateDynamicModel(&entity,NULL,NULL);Need(value!=NULL,"MP40 native MD5 snapshot failed");}
    ~Snapshot(){if(value)renderModelManager->FreeModel(value);}Snapshot(const Snapshot&)=delete;Snapshot& operator=(const Snapshot&)=delete;
};
void DescriptorMatches(const actor::Capability& capability,std::size_t index,const wire::Binding& binding){const auto& request=capability.request;const auto& selected=request.clips.at(index);const auto& returned=capability.clips.at(index);
    Need(wire::Alias(binding)==returned.animationAlias&&wire::Metadata(binding)==returned.animationMetadata,"MP40 actual V2 animation descriptor differs from its capability");
    Need(binding.modelAlias==capability.modelAlias&&binding.meshSkeletonName==request.resources.meshSkeletonName&&binding.clipName==selected.clipName&&binding.animationSkeletonName==selected.animationSkeletonName&&
        binding.levelOfDetail==0&&binding.geometryProfile==wire::GeometryProfile::WeightedOrRigid64&&binding.lodPolicy==wire::LodPolicy::RootAuthoredMeshUnspecifiedSlotV1&&
        binding.visualPolicy==wire::VisualPolicy::OrdinaryVisualNoExtraction&&binding.rigPolicy==wire::RigPolicy::CallerIdentityFullLocalPose,"MP40 descriptor selection/profile differs");
    const std::string paths[]={request.resources.modelResource,request.resources.meshSkeletonResource,selected.animationResource,selected.animationSkeletonResource};
    for(unsigned role=0;role<4;++role){const auto& a=returned.sources[role];const auto& b=binding.sources[role];Need(binding.paths[role]==paths[role]&&a.epoch==b.epoch&&a.bytes==b.bytes&&a.fingerprint==b.fingerprint,"MP40 descriptor source identity differs");}
    Need(binding.bones==49&&binding.frames==returned.frames&&std::ceil(binding.duration*1000)==returned.lengthMs,"MP40 descriptor frame/hierarchy/time domain differs");}
struct Texture {std::string path;File file;std::uint64_t bytes=0,fingerprint=0;unsigned width=0,height=0;};
struct Material {std::string source,nativeSource,name,text;wolf2009::MaterialResult facts;const idMaterial* from=NULL;const idMaterial* to=NULL;bool hidden=false;};
const char* const SourceMaterials[]={"models/characters/bj/bj_sleeve_new","models/characters/bj/bj_hand_new","models/weapons/mp40",
    "models/weapons/mp40/mp40_barrel","models/weapons/mp40/mp40_rifling","models/weapons/mp40/mp40_recoil","models/weapons/mp40/mp40_silencer"};
const char* const DiffuseNames[]={"models/characters/bj/bj_sleeve_new_d","models/characters/bj/bj_hand_new_d","models/weapons/mp40/mp40_d","models/weapons/mp40/mp40_upgrades_d"};
}

class WolfSourceWeaponView {
  public:
    mutable unsigned refs=1;
    File capabilityFile;
    actor::Capability capability;
    std::shared_ptr<const wolf2009::SourceWeaponProfile> profile;
    std::unique_ptr<WolfSourceWeaponPresentation> presentation;
    std::vector<const idMD5Anim*> animations;
    std::array<wire::Binding,3> bindings;
    std::array<Texture,4> textures;
    actor::Bytes catalogBytes;
    std::uint64_t catalogFingerprint=0;
    std::vector<Material> materials;
    std::string modelDefinition,modelName,skinName,flatSkinName,capabilityName,definitionText,skinText,flatSkinText;
    const idDeclModelDef* definition=NULL;
    const idDeclSkin *skin=NULL,*flatSkin=NULL;
    idRenderModel* model=NULL;
    unsigned vertices=0,triangles=0;
    WolfSourceWeaponView(){++liveViews;}
    ~WolfSourceWeaponView(){presentation.reset();for(const auto* value:animations)value->DecreaseRefs();--liveViews;}
    bool Matches(idStr& reason)const{try{
        Need(definition&&definition->GetState()==DS_PARSED&&DeclarationText(definition)==definitionText&&Wolf2009_FindRegisteredSourceAnimationSet(modelDefinition.c_str())==definition,"MP40 view modelDef changed");
        Need(model&&model->IsLoaded()&&!model->IsDefaultModel()&&model==definition->ModelHandle()&&modelName==model->Name()&&model->NumJoints()==49,"MP40 native model/hierarchy changed");
        Need(skin&&flatSkin&&DeclarationText(skin)==skinText&&DeclarationText(flatSkin)==flatSkinText&&definition->GetSkin()==skin,"MP40 view appearance declaration changed");
        Need(animations.size()==3&&definition->NumAnims()==4,"MP40 view native clip ownership/count changed");
        for(std::size_t i=0;i<3;++i){const auto& wanted=capability.request.clips[i];const int number=definition->GetSpecificAnim(wanted.logicalName.c_str());const auto* clip=number>0?definition->GetAnim(number):NULL;
            Need(clip&&clip->NumAnims()==1&&!clip->HasFrameCommands()&&clip->MD5Anim(0)==animations[i]&&animations[i]->IsSourceAnimationV2()&&animations[i]->SourceMatchesModel(model)&&
                animations[i]->NumFrames()==int(bindings[i].frames)&&animations[i]->Length()==int(capability.clips[i].lengthMs),"MP40 native clip identity/count changed");}
        for(const auto& material:materials)Need(DeclarationText(material.to)==material.text,"MP40 native diffuse material declaration changed");
        Need(presentation&&presentation->Preflight(reason),reason.IsEmpty()?"MP40 native presentation owner lost":reason.c_str());reason.Clear();return true;
    }catch(const std::exception& error){reason=error.what();return false;}}
};

namespace {
bool Preflight(const void* context,idStr& reason){return static_cast<const WolfSourceWeaponView*>(context)->Matches(reason);}
void Retain(const void* context){Wolf2009_RetainSourceWeaponView(static_cast<const WolfSourceWeaponView*>(context));}
void Release(const void* context){Wolf2009_ReleaseSourceWeaponView(static_cast<const WolfSourceWeaponView*>(context));}
bool Dispatch(const void* context,idAnimatedEntity* entity,const WolfSourceWeaponFrameCommand& command,int time,idStr& reason){
    return static_cast<const WolfSourceWeaponView*>(context)->presentation->Dispatch(entity,command,time,reason);}
bool Place(const void* context,idAnimatedEntity* entity,idPlayer* player,int time,idStr& reason){
    return static_cast<const WolfSourceWeaponView*>(context)->presentation->Place(entity,player,time,reason);}
void PrepareSkins(WolfSourceWeaponView& result,const WolfSourceWeaponViewRequest& request){
    result.catalogBytes=Read(Text(request.materialArchive),64u<<20);result.catalogFingerprint=wire::Fingerprint(result.catalogBytes.data(),result.catalogBytes.size());
    const auto catalog=render::ReadMaterialCatalog(result.catalogBytes);
    const std::string texturePaths[]={Path(request.sleeveTexture,"txtr"),Path(request.handTexture,"txtr"),Path(request.gunTexture,"txtr"),Path(request.upgradeTexture,"txtr")};
    for(unsigned i=0;i<4;++i){auto& texture=result.textures[i];texture.path=texturePaths[i];const auto path=render::ParseResourcePath(texture.path);
        Need(wolf2009::MaterialCatalog::NormalizeVirtualPath(path.name)==DiffuseNames[i],"MP40 texture routing differs from the explicit selected diffuse");
        texture.file.reset(fileSystem->OpenFileRead(texture.path.c_str(),false));const auto bytes=Read(texture.file.get(),64u<<20);const auto image=wolf2009::DecodeDdsMip0(bytes);
        texture.bytes=bytes.size();texture.fingerprint=wire::Fingerprint(bytes.data(),bytes.size());texture.width=image.width;texture.height=image.height;
        Need(texture.width&&texture.height,"MP40 original diffuse is empty");}
    std::set<std::string> hidden;for(unsigned i=1;i<=4;++i){const std::string key="hidesurface"+std::to_string(i);const auto value=Scalar(result.profile->Weapon(),key.c_str());Need(!value.empty()&&hidden.insert(value).second,"MP40 authored upgrade hides repeat or omit a surface");}
    // NativeBaseDiffuseV1 matches every retained source hide by exact material
    // name. An inherited hide for a material absent from this model is a no-op
    // (weapon_base also names the enemy in-view sleeve), not extra geometry or
    // grounds to discard the source field. The seven actual surfaces below
    // must still produce precisely the four admitted default upgrade hides.
    for(const auto& field:result.profile->Weapon().literalFields)if(field.first.compare(0,11,"hidesurface")==0){
        const auto value=Scalar(result.profile->Weapon(),field.first.c_str());if(!value.empty())hidden.insert(value);}
    Snapshot snapshot(result.model);Need(snapshot.value->NumSurfaces()==7,"MP40 native model must retain all seven source surfaces");
    const auto modelSource=render::ParseResourcePath(result.capability.request.resources.modelResource);std::set<std::string> seen;
    std::ostringstream skin,flat;skin<<"{\n";flat<<"{\n";
    // In this Doom3 parser a stage-free material is nondrawing. Quake3's
    // surfaceParm/nodraw grammar is deliberately not injected into it.
    const std::string nodrawText="{\n noShadows\n}\n";
    const std::string nodrawName=HashName("hidden-v1",nodrawText);Declare(DECL_MATERIAL,nodrawName,nodrawText);const auto* nodraw=declManager->FindMaterial(nodrawName.c_str(),false);
    Need(nodraw&&nodraw->GetState()!=DS_DEFAULTED&&!nodraw->IsDrawn()&&!nodraw->SurfaceCastsShadow(),"MP40 hidden upgrade material is not nondrawing");
    for(int i=0;i<7;++i){const auto* surface=snapshot.value->Surface(i);Need(surface&&surface->shader&&surface->geometry&&surface->geometry->numVerts>0&&surface->geometry->numIndexes>0,"MP40 native snapshot has an empty surface");
        Material material;material.source=SourceMaterials[i];material.from=surface->shader;material.nativeSource=modelSource.Prefix()+"materials/"+material.source;
        Need(material.nativeSource==surface->shader->GetName()&&seen.insert(material.source).second,"MP40 native surface order/material identity differs from its admitted profile");
        material.hidden=hidden.count(material.source)!=0;Need(material.hidden==(i>=3),"MP40 source default upgrade hiding differs from native selected material");
        material.facts=catalog->Resolve(material.source);const unsigned texture=i<3?unsigned(i):3u;
        Need(material.facts.resolved&&wolf2009::MaterialCatalog::NormalizeVirtualPath(material.facts.diffusePath)==DiffuseNames[texture],"MP40 source material does not resolve its explicit original diffuse");
        Need(!material.facts.hasAlphaTest&&material.facts.uvScale[0]==1&&material.facts.uvScale[1]==1,"MP40 NativeBaseDiffuseV1 does not admit alpha-test/UV rebaking");
        // NativeBaseDiffuseV1 deliberately displays only the retained diffuse.
        // The source shader technique/normal/specular/colour facts stay owned.
        if(material.hidden){material.name=nodrawName;material.text=nodrawText;material.to=nodraw;}
        else{material.text="{\n noShadows\n {\n map \""+texturePaths[texture]+"\"\n }\n}\n";material.name=HashName("base-diffuse-v1",material.text);
            Declare(DECL_MATERIAL,material.name,material.text);material.to=declManager->FindMaterial(material.name.c_str(),false);
            Need(material.to&&material.to->GetState()!=DS_DEFAULTED&&material.to->IsDrawn(),"MP40 native diffuse stage failed to parse");}
        skin<<" \""<<material.nativeSource<<"\" \""<<material.name<<"\"\n";
        if(material.hidden)flat<<" \""<<material.nativeSource<<"\" \""<<nodrawName<<"\"\n";
        else{const std::string flatText="{\n noShadows\n {\n map _white\n }\n}\n";const std::string name=HashName("flat-control-v1",flatText);
            Declare(DECL_MATERIAL,name,flatText);const auto* white=declManager->FindMaterial(name.c_str(),false);Need(white&&white->IsDrawn()&&white->GetState()!=DS_DEFAULTED,"MP40 flat control failed to parse");flat<<" \""<<material.nativeSource<<"\" \""<<name<<"\"\n";}
        result.vertices+=unsigned(surface->geometry->numVerts);result.triangles+=unsigned(surface->geometry->numIndexes/3);result.materials.push_back(std::move(material));}
    Need(seen.size()==7,"MP40 default surface policy is incomplete");skin<<"}\n";flat<<"}\n";result.skinText=skin.str();result.flatSkinText=flat.str();
    result.skinName=HashName("skin-base-diffuse-v1",result.skinText);result.flatSkinName=HashName("skin-flat-control-v1",result.flatSkinText);
    Declare(DECL_SKIN,result.skinName,result.skinText);Declare(DECL_SKIN,result.flatSkinName,result.flatSkinText);
    result.skin=declManager->FindSkin(result.skinName.c_str(),false);result.flatSkin=declManager->FindSkin(result.flatSkinName.c_str(),false);
    Need(result.skin&&result.flatSkin&&result.skin->GetState()!=DS_DEFAULTED&&result.flatSkin->GetState()!=DS_DEFAULTED,"MP40 native skin failed to parse");
    // RemapShaderBySkin is a nonvirtual core method in API8. Do not call that
    // symbol from the game DSO. Instantiate through the existing renderer
    // interface instead: it executes the real native remap/filter and proves
    // both skins retain exactly the same three base surface IDs.
    Snapshot visible(result.model,result.skin),control(result.model,result.flatSkin);
    Need(visible.value->NumSurfaces()==3&&control.value->NumSurfaces()==3,"MP40 custom skins did not hide exactly four upgrades");
    for(int i=0;i<3;++i){const auto* a=visible.value->Surface(i);const auto* b=control.value->Surface(i);
        Need(a&&b&&a->id==i&&b->id==i&&a->geometry&&b->geometry&&a->geometry->numVerts==b->geometry->numVerts&&a->geometry->numIndexes==b->geometry->numIndexes,
            "MP40 diffuse/flat skins changed base surface identity or geometry");}
}
}

void Wolf2009_SourceMP40FixtureViewRequest(WolfSourceWeaponViewRequest& out){out=WolfSourceWeaponViewRequest();out.policy=WolfSourceWeaponView_MP40NativeBaseDiffuseV1;
    out.modelResource="wolf2009/sp_mp40/modl/models/weapons/mp40/mp40_view";out.meshSkeletonResource="wolf2009/sp_global/skel/maps/game/global/global";
    out.meshSkeletonName="models/weapons/mp40/mp40_skeleton.hkx";out.animationResource="wolf2009/sp_global/hkxa/maps/game/global/global";
    out.animationSkeletonResource="wolf2009/sp_global/skel/maps/game/global/global";out.animationSkeletonName="models/weapons/mp40/mp40_skeleton.hkx";
    out.idleClip="models/weapons/mp40/mp40_idle.hkx";out.fireClip="models/weapons/mp40/mp40_fire.hkx";out.reloadClip="models/weapons/mp40/mp40_reload.hkx";
    out.materialArchive="fixtures/wolf-materials.bin";out.sleeveTexture="wolf2009/sp_global/txtr/models/characters/bj/bj_sleeve_new_d.dds";
    out.handTexture="wolf2009/sp_global/txtr/models/characters/bj/bj_hand_new_d.dds";out.gunTexture="wolf2009/sp_mp40/txtr/models/weapons/mp40/mp40_d.dds";
    out.upgradeTexture="wolf2009/sp_mp40/txtr/models/weapons/mp40/mp40_upgrades_d.dds";out.reloadSample="wolf2009/sp_global/SNDS/sound/weapons/mp40/mp40_reload_6.mp3";}
WolfSourceWeaponView* Wolf2009_CreateSourceWeaponView(const WolfSourceDeclarationSet& set,const WolfSourceWeaponViewRequest& request,idStr& reason){reason.Clear();try{
    Need(request.policy==WolfSourceWeaponView_MP40NativeBaseDiffuseV1,"explicit MP40 view NativeBaseDiffuseV1 policy required");
    Need(liveViews<16,"MP40 source view live-owner budget exceeded");
    std::unique_ptr<WolfSourceWeaponView> result(new WolfSourceWeaponView);result->profile=wolf2009::PrepareSourceWeaponProfile(set.Definitions(),"weapon_mp40",wolf2009::SourceWeaponPolicy::MP40PrimaryNativeV1);
    Selected(result->profile->Weapon(),set.Namespace());const auto& sourceModel=result->profile->ViewModel();Selected(sourceModel.model,set.Namespace());ZeroOffset(sourceModel.model);
    for(const auto& fact:sourceModel.model.opaqueFacts)Need(Fold(fact.data.key.text)=="anim","MP40 view has an unsupported non-animation model command");
    if(sourceModel.model.Find("skin"))Need(Scalar(sourceModel.model,"skin").empty(),"MP40 authored source skin needs another explicit appearance policy");
    actor::Request wanted;wanted.entityDefinition=result->profile->Configuration().name;wanted.modelDefinition=result->profile->Configuration().viewModel;
    wanted.meshReference=Scalar(sourceModel.model,"mesh");wanted.skeletonReference=Scalar(sourceModel.model,"skeleton");
    auto& resources=wanted.resources;resources.modelResource=Path(request.modelResource,"modl");resources.meshSkeletonResource=Path(request.meshSkeletonResource,"skel");resources.meshSkeletonName=Text(request.meshSkeletonName);
    resources.geometryProfile=actor::GeometryProfile::WeightedOrRigid64;resources.lodPolicy=actor::LodPolicy::RootAuthoredMeshUnspecifiedSlotV1;resources.levelOfDetail=0;
    const char* logical[]={"idle","fire","reload"};const char* clipNames[]={request.idleClip,request.fireClip,request.reloadClip};
    for(unsigned i=0;i<3;++i){const auto* source=sourceModel.Find(logical[i]);Need(source&&source->logicalName.text==logical[i]&&source->hasLiteralAssetReference,"MP40 exact source logical clip is absent");
        const auto tree=set.Namespace().Lookup("animBlendTree",source->assetReference.text);Need(tree.status==wolf2009::CompiledCatalogLookupStatus::Missing&&tree.matches.empty(),"MP40 selected explicit blend tree is outside this single-clip profile");
        actor::RequestClip clip;clip.logicalName=logical[i];clip.authoredReference=source->assetReference.text;clip.clipName=Text(clipNames[i]);
        Need(Name(clip.clipName)==Name(clip.authoredReference),"MP40 explicit animation key differs from its source declaration");
        clip.animationResource=Path(request.animationResource,"hkxa");clip.animationSkeletonResource=Path(request.animationSkeletonResource,"skel");clip.animationSkeletonName=Text(request.animationSkeletonName);
        clip.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;clip.rigPolicy=actor::RigPolicy::CallerIdentityFullLocalPose;wanted.clips.push_back(std::move(clip));}
    const auto path=actor::RequestPath(wanted);result->capabilityFile.reset(fileSystem->OpenFileRead(path.c_str(),false));result->capability=actor::ParseCapability(Read(result->capabilityFile.get(),actor::MaximumCapabilityBytes));
    Need(actor::Same(wanted,result->capability.request),"MP40 readonly source capability did not echo its full request");result->capabilityName=actor::CapabilityName(result->capability);
    Need(result->capabilityName==result->capabilityFile->GetName()&&result->capabilityName==result->capabilityFile->GetFullPath(),"MP40 source capability identity differs");
    result->modelName=result->capability.modelAlias;result->model=renderModelManager->CheckModel(result->modelName.c_str());Need(result->model&&result->model->IsLoaded()&&!result->model->IsDefaultModel()&&result->model->NumJoints()==49,"MP40 source model did not produce its native49-joint model");
    std::vector<WolfSourceAnimationSetEntry> entries;
    for(unsigned i=0;i<3;++i){const auto& response=result->capability.clips[i];const auto envelope=wire::Parse(Read(response.animationAlias,wire::MaximumEnvelopeBytes));DescriptorMatches(result->capability,i,envelope.binding);result->bindings[i]=envelope.binding;
        auto* animation=animationLib.GetAnim(response.animationAlias.c_str());Need(animation&&animation->IsSourceAnimationV2()&&animation->SourceMatchesModel(result->model)&&animation->NumFrames()==int(response.frames)&&animation->Length()==int(response.lengthMs),"MP40 native source animation differs from its capability");
        result->animations.push_back(animation);animation->IncreaseRefs();WolfSourceAnimationSetEntry entry;entry.logicalName=logical[i];entry.animationAlias=response.animationAlias;entries.push_back(std::move(entry));}
    PrepareSkins(*result,request);idStr definition;Need(Wolf2009_CreateSourceActorAnimationSet(entries,result->skinName.c_str(),definition,reason),reason.IsEmpty()?"MP40 native source animation set creation failed":reason.c_str());
    result->modelDefinition=definition.c_str();result->definition=Wolf2009_FindRegisteredSourceAnimationSet(definition);result->definitionText=DeclarationText(result->definition);
    WolfSourceWeaponPresentationRequest presentation;presentation.policy=WolfSourceWeaponPresentationPolicy::NativeWeaponLocalAimAndPointTrackingV1;presentation.nativeModelDefinition=result->modelDefinition;presentation.exactReloadSampleResource=Text(request.reloadSample);
    result->presentation=Wolf2009_PrepareSourceWeaponPresentation(set,*result->profile,presentation,reason);Need(bool(result->presentation),reason.IsEmpty()?"MP40 native presentation preparation failed":reason.c_str());
    Need(result->Matches(reason),reason.IsEmpty()?"MP40 native source view preflight failed":reason.c_str());return result.release();
}catch(const std::exception& error){reason=error.what();return NULL;}}
void Wolf2009_RetainSourceWeaponView(const WolfSourceWeaponView* view){if(view){Need(view->refs<UINT_MAX,"MP40 view reference count overflow");++view->refs;}}
void Wolf2009_ReleaseSourceWeaponView(const WolfSourceWeaponView* view){if(view){Need(view->refs>0,"MP40 view reference count underflow");if(!--view->refs)delete view;}}
bool Wolf2009_SourceWeaponViewDescriptor(const WolfSourceWeaponView* view,WolfSourceWeaponVisual& out,idStr& reason){if(!view){reason="MP40 view owner is absent";return false;}if(!view->Matches(reason))return false;
    WolfSourceWeaponVisual descriptor;descriptor.modelDefinition=view->modelDefinition.c_str();descriptor.fireFrames=view->bindings[1].frames;descriptor.reloadFrames=view->bindings[2].frames;
    descriptor.fireDurationSeconds=view->bindings[1].duration;descriptor.reloadDurationSeconds=view->bindings[2].duration;descriptor.context=view;descriptor.retain=Retain;descriptor.release=Release;
    descriptor.preflight=Preflight;descriptor.dispatch=Dispatch;descriptor.place=Place;out=descriptor;return true;}
bool Wolf2009_QuerySourceWeaponView(const WolfSourceWeaponView* view,WolfSourceWeaponViewState& out,idStr& reason){if(!view){reason="MP40 view owner is absent";return false;}if(!view->Matches(reason))return false;
    WolfSourceWeaponViewState state;state.modelDefinition=view->modelDefinition.c_str();state.nativeModel=view->modelName.c_str();state.skin=view->skinName.c_str();state.flatSkin=view->flatSkinName.c_str();state.capability=view->capabilityName.c_str();
    state.reloadSound=view->presentation->NativeReloadSound();state.reloadSample=view->presentation->ReloadSampleCapability();state.joints=unsigned(view->model->NumJoints());state.surfaces=unsigned(view->materials.size());
    for(const auto& material:view->materials){if(material.hidden)++state.hiddenSurfaces;else ++state.visibleSurfaces;}state.vertices=view->vertices;state.triangles=view->triangles;state.textures=4;state.baseDiffuseCompatibility=true;
    for(unsigned c=0;c<3;++c){state.frames[c]=view->bindings[c].frames;state.durationSeconds[c]=view->bindings[c].duration;
        for(unsigned r=0;r<4;++r){const auto& identity=view->bindings[c].sources[r];state.sourceEpochs[c][r]=identity.epoch;state.sourceBytes[c][r]=identity.bytes;state.sourceFingerprints[c][r]=identity.fingerprint;}}
    state.catalogBytes=view->catalogBytes.size();state.catalogFingerprint=view->catalogFingerprint;
    for(unsigned t=0;t<4;++t){const auto& texture=view->textures[t];state.textureBytes[t]=texture.bytes;state.textureFingerprints[t]=texture.fingerprint;state.textureWidths[t]=texture.width;state.textureHeights[t]=texture.height;}
    out=state;reason.Clear();return true;}
bool Wolf2009_QuerySourceWeaponViewPresentation(const WolfSourceWeaponView* view,WolfSourceWeaponPresentationState& out,idStr& reason){
    if(!view){reason="MP40 view owner is absent";return false;}return view->presentation&&view->presentation->Query(out,reason);}
int Wolf2009_LiveSourceWeaponViews(){return liveViews;}
