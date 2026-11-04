// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceScriptedActor.h"
#include "AnimationWireV2.h"
#include "EngineAnimationSet.h"
#include "RenderData.h"
#include "SkinnedModelData.h"
#include "SkinCatalog.h"
#include "ZipArchive.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <exception>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceControlledEntity.h"
#include "SourceDeclarationSetRuntime.h"
#include "NativeSourceAnimatedDoor.h"

namespace {
namespace actor=wolf2009::actor;
namespace wire=wolf2009::animation2;
namespace render=wolf2009::idtech4a;
using render::Bytes;
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
// C++11 argument evaluation may precede an API which Clears/frees reason's
// old buffer. Pass the object, never reason.c_str(), across that API call.
void NeedReason(bool value,const idStr& reason,const char* fallback){if(!value)throw std::runtime_error(reason.IsEmpty()?fallback:reason.c_str());}
bool Refuse(idStr& reason,const char* why){reason=why;return false;}
std::string Fold(std::string value){for(char& c:value){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}return value;}
std::string Stem(std::string value){value=Fold(value);const auto dot=value.rfind('.');if(dot!=std::string::npos)value.resize(dot);return value;}
std::string Text(const std::string& value){Need(!value.empty()&&value.size()<=4096,"Scripted actor requires explicit bounded source text");
    for(unsigned char c:value)Need(c>=32&&c<127&&c!='"'&&c!='{'&&c!='}',"Unsafe scripted actor source text");return value;}
std::string Path(const std::string& value,const char* type){const auto parsed=render::ParseResourcePath(Text(value));Need(parsed.type==type,"Scripted actor source has the wrong FourCC");return parsed.Path(type,parsed.name);}
std::string RelativePath(const std::string& value){const auto path=Text(value);Need(path.front()!='/'&&path.front()!='\\'&&path.find(':')==std::string::npos,"Scripted actor needs a relative material archive");
    const auto normal=Fold(path);for(std::size_t at=0;at<normal.size();){const auto end=normal.find('/',at);const auto part=normal.substr(at,end-at);
        Need(!part.empty()&&part!="."&&part!="..","Unsafe scripted actor material archive component");if(end==std::string::npos)break;at=end+1;}
    Need(normal.back()!='/',"Scripted actor material archive has an empty final component");return path;}
std::string Field(const wolf2009::EntityRecord& entity,const char* key,bool required=true){const auto* value=entity.Find(key);
    if(!value){Need(!required,"Scripted actor instance string is absent");return std::string();}
    Need(value->kind==wolf2009::EntityProperty::Text,"Scripted actor instance field is not text");return required?Text(value->text):value->text;}
std::string Scalar(const wolf2009::ResolvedSourceDefinition& definition,const char* key,bool required=true){const auto* value=definition.Find(key);
    if(!value){Need(!required,"Scripted actor source string is absent");return std::string();}
    Need((value->encoding==wolf2009::SourceLiteralEncoding::PlainText||value->encoding==wolf2009::SourceLiteralEncoding::CompiledString)&&value->value.size()==1,
         "Scripted actor source field is not one qualified string");return required?Text(value->value[0].text):value->value[0].text;}
bool SameEntry(const wolf2009::CompiledCatalogEntry& a,const wolf2009::CompiledCatalogEntry& b){
    return a.IsUnmodified()&&b.IsUnmodified()&&a.document==b.document&&a.source==b.source&&a.catalogOrdinal==b.catalogOrdinal&&
        a.loadOrdinal==b.loadOrdinal&&a.documentOrdinal==b.documentOrdinal&&a.section==b.section&&a.group==b.group&&a.record==b.record&&a.type==b.type&&a.name==b.name&&
        a.typeSpan.begin==b.typeSpan.begin&&a.typeSpan.end==b.typeSpan.end&&a.nameSpan.begin==b.nameSpan.begin&&a.nameSpan.end==b.nameSpan.end&&
        a.bodySpan.begin==b.bodySpan.begin&&a.bodySpan.end==b.bodySpan.end&&a.envelopeSpan.begin==b.envelopeSpan.begin&&a.envelopeSpan.end==b.envelopeSpan.end;
}
void Selected(const wolf2009::ResolvedSourceDefinition& value,const wolf2009::CompiledDeclarationNamespace& names){
    Need(!value.declarations.empty(),"Scripted actor needs selected compiled declaration provenance");
    for(const auto& source:value.declarations){const auto& entry=source.member.compiledRecord;
        Need(entry&&entry->IsUnmodified(),"Scripted actor source declaration is not immutable compiled data");
        const auto found=names.Lookup(source.declarationKind,source.declarationName);
        Need(found.status==wolf2009::CompiledCatalogLookupStatus::Unique&&found.matches.size()==1&&SameEntry(found.matches[0],*entry),"Scripted actor source is not the exact selected namespace owner");}
}
void ZeroOffset(const wolf2009::ResolvedSourceDefinition& value){const auto* offset=value.Find("offset");if(!offset)return;
    Need(offset->encoding==wolf2009::SourceLiteralEncoding::PlainText,"Scripted actor model offset is not a literal");std::string text;
    for(const auto& token:offset->value){Need(!token.brace&&!token.quoted,"Block/quoted scalar in scripted actor model offset");text+=' ';text+=token.text;}
    std::istringstream input(text);input.imbue(std::locale::classic());input>>std::ws;const bool parentheses=input.peek()=='(';if(parentheses)input.get();
    double x=1,y=1,z=1;Need(bool(input>>x),"Scripted actor malformed model offset x");input>>std::ws;const bool commas=input.peek()==',';if(commas)input.get();
    Need(bool(input>>y),"Scripted actor malformed model offset y");input>>std::ws;Need((input.peek()==',')==commas,"Scripted actor model offset separators differ");if(commas)input.get();
    Need(bool(input>>z),"Scripted actor malformed model offset z");if(parentheses){input>>std::ws;Need(input.get()==')',"Scripted actor model offset parenthesis is missing");}
    char extra=0;Need(!(input>>extra)&&x==0&&y==0&&z==0,"Scripted actor requires the authored zero model offset");
}
struct Source {
    idFile* file=NULL;Bytes bytes;std::uint64_t fingerprint=0;
    ~Source(){if(file)fileSystem->CloseFile(file);}
};
typedef std::map<std::string,std::unique_ptr<Source>> Sources;
const Bytes& Read(Sources& sources,const std::string& path,std::size_t maximum){const auto previous=sources.find(path);if(previous!=sources.end())return previous->second->bytes;
    Need(sources.size()<128,"Scripted actor source-file budget");std::unique_ptr<Source> source(new Source);source->file=fileSystem->OpenFileRead(path.c_str(),false);
    Need(source->file&&source->file->Tell()==0,"Scripted actor exact source file is absent");const int size=source->file->Length();
    Need(size>0&&std::size_t(size)<=maximum,"Scripted actor source byte budget");std::size_t total=0;for(const auto& item:sources)total+=item.second->bytes.size();
    Need(total<=(256u<<20)&&std::size_t(size)<=(256u<<20)-total,"Scripted actor aggregate source byte budget");source->bytes.resize(std::size_t(size));
    std::size_t at=0;while(at<source->bytes.size()){const int n=source->file->Read(source->bytes.data()+at,int(source->bytes.size()-at));Need(n>0&&std::size_t(n)<=source->bytes.size()-at,"Short scripted actor source read");at+=std::size_t(n);}
    unsigned char extra=0;Need(source->file->Read(&extra,1)==0&&source->file->Tell()==size&&source->file->Length()==size,"Scripted actor source EOF/length changed");
    source->fingerprint=wire::Fingerprint(source->bytes.data(),source->bytes.size());return sources.emplace(path,std::move(source)).first->second->bytes;
}
std::string DeclText(const idDecl* value){Need(value&&value->GetState()!=DS_DEFAULTED,"Scripted actor native declaration is absent/defaulted");const int n=value->GetTextLength();
    Need(n>0&&n<=65536,"Scripted actor native declaration byte budget");std::vector<char> bytes(std::size_t(n)+1,0);value->GetText(bytes.data());return std::string(bytes.data(),std::size_t(n));}
const idDecl* Existing(declType_t type,const std::string& name){if(const auto* value=declManager->FindDeclWithoutParsing(type,name.c_str(),false))return value;
    for(int i=0;i<declManager->GetNumDecls(type);++i){const auto* value=declManager->DeclByIndex(type,i,false);if(value&&name==value->GetName())return value;}return NULL;}
std::map<std::string,std::string> declarations;
void Declare(declType_t type,const std::string& name,const std::string& text){const auto prior=declarations.find(name);const auto* existing=Existing(type,name);
    if(prior==declarations.end()){Need(!existing,"Scripted actor private declaration namespace is occupied");Need(declarations.size()<2048,"Scripted actor declaration budget");
        idDecl* value=declManager->CreateNewDecl(type,name.c_str(),"<wolf2009-scripted-actor>");Need(value!=NULL,"Scripted actor declaration allocation failed");value->SetText(text.c_str());value->Invalidate();declarations.emplace(name,text);
    }else Need(existing&&prior->second==text&&DeclText(existing)==text,"Scripted actor private declaration changed/hash collision");}
std::string Hex(std::uint64_t value){std::string out(16,'0');for(int i=15;i>=0;--i){out[std::size_t(i)]="0123456789abcdef"[value&15];value>>=4;}return out;}
template<class T>struct Aligned {T* data;explicit Aligned(int n):data(static_cast<T*>(Mem_Alloc16(std::size_t(n)*sizeof(T)))){Need(data!=NULL,"Scripted actor aligned allocation failed");}~Aligned(){Mem_Free16(data);}};
struct Snapshot {
    idRenderModel* model=NULL;
    explicit Snapshot(idRenderModel* source,const idDeclSkin* skin=NULL){const int n=source->NumJoints();Need(n>0&&n<=256&&source->GetDefaultPose()&&source->GetJoints(),"Scripted actor native default hierarchy absent");
        Aligned<idJointQuat> local(n);Aligned<idJointMat> global(n);std::vector<int> parents;
        for(int i=0;i<n;++i){local.data[i]=source->GetDefaultPose()[i];int parent=-1;if(source->GetJoints()[i].parent){for(int j=0;j<i;++j)if(source->GetJoints()[i].parent==&source->GetJoints()[j]){parent=j;break;}Need(parent>=0,"Scripted actor joint parent is not preceding");}parents.push_back(parent);}
        SIMDProcessor->ConvertJointQuatsToJointMats(global.data,local.data,n);if(n>1)SIMDProcessor->TransformJoints(global.data,parents.data(),1,n-1);
        renderEntity_t entity;std::memset(&entity,0,sizeof(entity));entity.hModel=source;entity.numJoints=n;entity.joints=global.data;entity.customSkin=skin;entity.axis.Identity();for(int i=0;i<4;++i)entity.shaderParms[i]=1;
        model=source->InstantiateDynamicModel(&entity,NULL,NULL);Need(model!=NULL,"Scripted actor native snapshot failed");}
    ~Snapshot(){if(model)renderModelManager->FreeModel(model);}
};
bool SameCommand(const wolf2009::TrainDoorCommand& a,const wolf2009::TrainDoorCommand& b){return a.kind==b.kind&&a.ownerEntityName==b.ownerEntityName&&a.resourceName==b.resourceName&&a.targetEntityName==b.targetEntityName&&a.levelGeneration==b.levelGeneration&&a.eventId==b.eventId&&a.sourceNodeId==b.sourceNodeId;}
struct SkinMaterial {const idMaterial* from=NULL;const idMaterial* to=NULL;std::string fromText,name,text;};
struct Busy {bool& value;explicit Busy(bool& flag):value(flag){Need(!flag,"Scripted actor operation reentry is unsupported");flag=true;}~Busy(){value=false;}};
wolf2009::EntityPlacement Placement(const wolf2009::EntityRecord& source,idVec3& origin,idMat3& axis){const auto value=wolfSourceScriptedActorPlacement::Qualify(source);
    for(unsigned i=0;i<3;++i){origin[i]=value.origin[i];for(unsigned j=0;j<3;++j)axis[i][j]=value.rotation[i*3+j];}return value;}
bool SamePlacement(const idVec3& a,const idMat3& r,const idVec3& b,const idMat3& s){for(int i=0;i<3;++i){if(!(std::abs(a[i]-b[i])<=.0002f))return false;
    for(int j=0;j<3;++j)if(!(std::abs(r[i][j]-s[i][j])<=.00002f))return false;}return true;}
unsigned Decimal(const std::string& value,unsigned maximum){Need(!value.empty(),"Scripted actor empty source frame token");unsigned n=0;
    for(char c:value){Need(c>='0'&&c<='9'&&unsigned(c-'0')<=maximum&&n<=(maximum-unsigned(c-'0'))/10,"Scripted actor source frame token domain");n=n*10+unsigned(c-'0');}return n;}
void RetainCommands(const wolf2009::SourceModelAnimationReference& reference,unsigned frames,WolfSourceScriptedActorState& state){
    Need(reference.hasLiteralAssetReference&&!reference.origins.empty(),"Scripted actor needs a literal authored clip with owning command facts");
    const auto& tokens=reference.origins[0].data.values;Need(tokens.size()>=2&&!tokens[0].brace&&!tokens[1].brace&&tokens[0].text==reference.logicalName.text&&tokens[1].text==reference.assetReference.text,"Scripted actor source animation header is truncated or differs");
    for(const auto& origin:reference.origins){Need(origin.data.values.size()==tokens.size(),"Scripted actor inherited animation commands conflict");
        for(std::size_t i=0;i<tokens.size();++i)Need(tokens[i].text==origin.data.values[i].text&&tokens[i].brace==origin.data.values[i].brace&&tokens[i].quoted==origin.data.values[i].quoted,"Scripted actor inherited animation command bytes conflict");}
    if(reference.shape==wolf2009::SourceAnimationReferenceShape::SingleLiteralNoTail){Need(tokens.size()==2,"Scripted actor pure animation has trailing tokens");return;}
    Need(reference.shape==wolf2009::SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock&&tokens.size()>=4&&tokens[2].brace=='{'&&tokens.back().brace=='}',"Scripted actor animation reference shape is unsupported");
    // Recognize only the retained option/action classes of this bounded slice.
    // None is copied into the native set or executed, including frame1 sounds.
    for(std::size_t i=3;i+1<tokens.size();){Need(!tokens[i].brace,"Scripted actor nested animation commands are unsupported");const auto key=Fold(tokens[i++].text);
        if(key=="no_delta_blend_out"||key=="anim_turn"||key=="anim_turn_force_positive"||key=="ai_pure_motion_extraction"){++state.retainedAnimationOptions;continue;}
        Need(key=="frame"&&i+1<tokens.size()-1,"Scripted actor unknown animation option is outside the no-commands profile");
        Need(!tokens[i].brace&&Decimal(tokens[i].text,frames)>0,"Scripted actor authored frame command is outside the clip");++i;
        Need(!tokens[i].brace,"Scripted actor malformed frame action");const auto action=Fold(tokens[i++].text);
        if(action=="sound"||action=="ai_set_eyefocus"){Need(i<tokens.size()-1&&!tokens[i].brace,"Scripted actor missing frame action argument");Text(tokens[i++].text);}
        else Need(action=="aiming_disable"||action=="aiming_enable","Scripted actor unknown frame action is outside the retained profile");
        ++state.retainedFrameCommands;
    }
}
double EvaluatePose(idAnimatedEntity& entity,const idMD5Anim& clip,int frame){auto* animator=entity.GetAnimator();auto* model=animator?animator->ModelHandle():NULL;
    Need(model&&clip.IsSourceAnimationV2()&&clip.SourceMatchesModel(model),"Scripted actor pose has no exact native source clip/model");
    const int n=model->NumJoints();Need(n>0&&n<=256&&clip.NumJoints()==n&&frame>=0&&frame<clip.NumFrames(),"Scripted actor source pose domain");
    // This evaluates the genuine channel. Merely sampling a source clip does
    // not satisfy the ACK boundary; both native results are compared below.
    animator->CreateFrame(gameLocal.time,true);int count=0;idJointMat* actual=NULL;animator->GetJoints(&count,&actual);
    Need(count==n&&actual,"Scripted actor actual animator palette is absent");Aligned<idJointQuat> local(n);Aligned<idJointMat> expected(n);std::vector<int> indices;
    for(int i=0;i<n;++i)indices.push_back(i);clip.GetSingleFrame(frame,local.data,indices.data(),n);
    const auto* hierarchy=model->GetJoints();Need(hierarchy!=NULL,"Scripted actor native model hierarchy is absent");
    double error=0;for(int i=0;i<n;++i){expected.data[i].SetRotation(local.data[i].q.ToMat3());expected.data[i].SetTranslation(local.data[i].t);
        if(hierarchy[i].parent){const auto parent=hierarchy[i].parent-hierarchy;Need(parent>=0&&parent<i,"Scripted actor native parent order changed");expected.data[i]*=expected.data[parent];}
        for(int c=0;c<12;++c){const double d=std::abs(double(actual[i].ToFloatPtr()[c])-expected.data[i].ToFloatPtr()[c]);Need(std::isfinite(d),"Scripted actor nonfinite native pose");
            Need(d<=(c%4==3?.0002:.00002),"Scripted actor actual channel palette differs from its exact source frame");error=std::max(error,d);}}
    return error;}
void DescriptorMatches(const actor::Capability& capability,const wire::Binding& binding){const auto& mesh=capability.request.resources;const auto& clip=capability.request.clips.at(0);const auto& returned=capability.clips.at(0);
    Need(wire::Alias(binding)==returned.animationAlias&&wire::Metadata(binding)==returned.animationMetadata,"Scripted actor actual V2 envelope differs from its capability");
    Need(binding.modelAlias==capability.modelAlias&&binding.meshSkeletonName==mesh.meshSkeletonName&&binding.clipName==clip.clipName&&binding.animationSkeletonName==clip.animationSkeletonName&&
         binding.levelOfDetail==mesh.levelOfDetail&&unsigned(binding.geometryProfile)==unsigned(mesh.geometryProfile)&&unsigned(binding.lodPolicy)==unsigned(mesh.lodPolicy)&&
         binding.visualPolicy==wire::VisualPolicy::OrdinaryVisualNoExtraction&&unsigned(binding.rigPolicy)==unsigned(clip.rigPolicy),"Scripted actor V2 selection/profile differs");
    const std::string paths[]={mesh.modelResource,mesh.meshSkeletonResource,clip.animationResource,clip.animationSkeletonResource};
    for(unsigned r=0;r<4;++r){const auto& a=returned.sources[r];const auto& b=binding.sources[r];Need(binding.paths[r]==paths[r]&&a.epoch==b.epoch&&a.bytes==b.bytes&&a.fingerprint==b.fingerprint,"Scripted actor V2 four-role identity differs");}
    Need(binding.frames==returned.frames&&binding.bones==returned.bones&&std::ceil(binding.duration*1000)==returned.lengthMs,"Scripted actor V2 count/ceil-end differs");}
int liveActors=0;
}

struct WolfSourceScriptedActor::Impl {
    unsigned refs=1;
    WolfSourceScriptedActorRequest request;
    WolfSourceScriptedActorState state;
    WolfSourceScriptedActorStartAck ack;
    Sources sources;
    wolf2009::EntityRecord instance;
    wolf2009::ResolvedSourceDefinition defaults;
    wolf2009::ResolvedSourceModelAnimations modelFacts;
    actor::Request wanted;
    actor::Capability capability;
    wire::Binding animationBinding;
    render::SkinnedMeshDataV2 mesh;
    const idMD5Anim* animation=NULL;
    idRenderModel* model=NULL;
    const idDeclModelDef* definition=NULL;
    const idDeclSkin* skin=NULL;
    std::string capabilityName,definitionText,skinText,failure;
    std::vector<SkinMaterial> materials;
    idVec3 origin;idMat3 axis;
    idEntityPtr<idAnimatedEntity> entity;
    unsigned initialSerial=0;
    int initialTime=-1;
    bool spawned=false,busy=false;
    Impl(){++liveActors;}
    ~Impl(){if(entity.GetEntity())delete entity.GetEntity();if(animation)animation->DecreaseRefs();--liveActors;}
    static void Retain(const void* raw) noexcept {auto* p=const_cast<Impl*>(static_cast<const Impl*>(raw));if(!p||!p->refs||p->refs==UINT_MAX)std::terminate();++p->refs;}
    static void Release(const void* raw) noexcept {auto* p=const_cast<Impl*>(static_cast<const Impl*>(raw));if(!p||!p->refs)std::terminate();if(!--p->refs)delete p;}
    static bool CheckAuthority(const void* raw,idStr& why){const auto* p=static_cast<const Impl*>(raw);return p&&!p->busy?p->Check(why):Refuse(why,"Scripted actor start commit is in progress");}
    static bool QueryAuthority(const void* raw,WolfSourceActorStartEvidence& out,idStr& why){const auto* p=static_cast<const Impl*>(raw);
        if(!p||p->busy)return Refuse(why,"Scripted actor start authority is unavailable");if(!p->Check(why))return false;
        WolfSourceActorStartEvidence value;value.sourceName=p->state.sourceName;value.levelGeneration=p->request.levelGeneration;
        value.nativeActor=p->entity.GetEntity();value.nativeSpawnId=p->spawned?p->state.nativeSpawnId:0;
        if(p->ack.committed){value.committed=true;value.actualPoseEvaluated=p->ack.actualPoseEvaluated;value.command=p->ack.command;value.commandSerial=p->ack.commandSerial;value.committedTimeMs=p->ack.committedTimeMs;}
        out=value;return true;}
    bool Check(idStr& reason)const{reason.Clear();try{
        Need(failure.empty(),failure.c_str());
        Need(definition&&Wolf2009_FindRegisteredSourceAnimationSet(state.nativeModelDefinition.c_str())==definition&&DeclText(definition)==definitionText,"Scripted actor native model definition changed");
        Need(model&&model->IsLoaded()&&!model->IsDefaultModel()&&model->IsDynamicModel()==DM_CACHED&&model->NumJoints()==int(state.joints)&&definition->ModelHandle()==model&&state.nativeModel==model->Name(),"Scripted actor native model/hierarchy changed");
        Need(skin&&Existing(DECL_SKIN,state.nativeSkin)==skin&&skin->GetState()==DS_PARSED&&DeclText(skin)==skinText&&definition->GetSkin()==skin,"Scripted actor original skin binding changed");
        const int number=definition->GetSpecificAnim(state.logicalClip.c_str());const auto* clip=number>0?definition->GetAnim(number):NULL;
        Need(clip&&clip->NumAnims()==1&&!clip->HasFrameCommands()&&clip->MD5Anim(0)==animation&&definition->NumAnims()==2&&animation&&animation->IsSourceAnimationV2()&&
             animation->SourceMatchesModel(model)&&state.animationAlias==animation->Name()&&animation->NumFrames()==int(state.frames)&&animation->Length()==int(state.lengthMs),"Scripted actor native clip identity/commands changed");
        for(const auto& value:materials)Need(value.from&&DeclText(value.from)==value.fromText&&value.to&&Existing(DECL_MATERIAL,value.name)==value.to&&DeclText(value.to)==value.text,"Scripted actor native material changed");
        for(const auto& source:sources)Need(source.second->file&&source.second->file->Length()==int(source.second->bytes.size())&&source.second->file->Tell()==int(source.second->bytes.size()),"Scripted actor retained source file changed");
        if(spawned){auto* native=entity.GetEntity();Need(native&&unsigned(entity.GetSpawnId())==state.nativeSpawnId&&Wolf2009_IsSourceControlledEntity(native)&&state.sourceName==native->name.c_str(),"Scripted actor original native entity lifetime/name changed");
            Need(!native->GetBindMaster()&&native->GetPhysics()&&SamePlacement(origin,axis,native->GetPhysics()->GetOrigin(),native->GetPhysics()->GetAxis()),"Scripted actor authored anchor/binding changed");
            auto* animator=native->GetAnimator();Need(animator&&animator->ModelDef()==definition&&animator->ModelHandle()==model&&!animator->RemoveOrigin()&&native->GetRenderEntity()->customSkin==skin,"Scripted actor native animator/model/skin changed");
            WolfSourceControlledState current;NeedReason(Wolf2009_SourceControlledState(native,current,reason),reason,"Scripted actor native controller state unavailable");
            const auto* blend=animator->CurrentAnim(ANIMCHANNEL_ALL);
            Need(blend&&blend->AnimNum()==number&&blend->GetCycleCount()==1&&blend->GetPlaybackRate()==1.f&&blend->GetWeight(gameLocal.time)==1.f&&current.clip==state.logicalClip.c_str(),"Scripted actor native channel/clip/rate/weight changed");
            if(!ack.committed)Need(current.phase==WolfSourceControlled_Held&&current.sourceFrame==request.initialSourceFrame&&current.commandSerial==initialSerial&&current.commandTimeMs==initialTime&&blend->GetFrameNumber(gameLocal.time)==request.initialSourceFrame+1,"Scripted actor inactive visual was externally commanded");
            else{
                Need(gameLocal.time>=ack.committedTimeMs&&current.commandSerial==ack.commandSerial&&current.commandTimeMs==ack.committedTimeMs,"Scripted actor start serial/clock changed");
                if(current.phase==WolfSourceControlled_Playing)Need(blend->GetStartTime()==ack.committedTimeMs&&blend->GetEndTime()==state.nativeEndTimeMs&&current.endTimeMs==state.nativeEndTimeMs,"Scripted actor one-shot clock changed");
                else Need(current.phase==WolfSourceControlled_Held&&current.sourceFrame==int(state.frames)-1&&gameLocal.time>=state.nativeEndTimeMs&&blend->GetFrameNumber(gameLocal.time)==int(state.frames),"Scripted actor lacks its actual final native hold");
            }
        }else Need(!entity.GetEntity()&&!ack.committed,"Prepared scripted actor acquired native playback");
        return true;
    }catch(const std::exception& error){reason=error.what();return false;}}
    void PrepareSkin();
};

void WolfSourceScriptedActor::Impl::PrepareSkin(){
    const auto& bytes=Read(sources,RelativePath(request.materialArchive),64u<<20);const auto catalog=render::ReadMaterialCatalog(bytes);
    std::istringstream input(std::string(bytes.begin(),bytes.end()),std::ios::in|std::ios::binary);const auto zip=wolf2009::ZipArchive::Scan(input);const auto skins=wolf2009::SkinCatalog::Read(input,zip);
    const auto& source=wanted.resources;render::SkinnedMeshRequestV2 meshRequest=render::MakeSkinnedMeshRequestV2(source.modelResource,source.meshSkeletonResource,source.meshSkeletonName,source.levelOfDetail,
        static_cast<wolf2009::SkinnedVertexProfile>(source.geometryProfile),static_cast<wolf2009::SkinnedLodPolicy>(source.lodPolicy));
    meshRequest.sourceEpochs={{capability.clips[0].sources[0].epoch,capability.clips[0].sources[1].epoch}};
    mesh=render::LoadSkinnedMeshV2(meshRequest,[&](const std::string& path){return Read(sources,path,64u<<20);});
    Need(mesh.modelSource.bytes==capability.clips[0].sources[0].bytes&&mesh.modelSource.fingerprint==capability.clips[0].sources[0].fingerprint&&
         mesh.skeletonSource.bytes==capability.clips[0].sources[1].bytes&&mesh.skeletonSource.fingerprint==capability.clips[0].sources[1].fingerprint&&mesh.joints.size()==state.joints,
         "Scripted actor source skin geometry differs from its qualified native capability");
    wolf2009::GeometryScene selected;selected.models=1;
    for(const auto& part:mesh.surfaces){wolf2009::GeometrySurface surface;surface.model=part.geometry.model;surface.material=part.geometry.material;const auto base=selected.vertices.size();
        Need(base<=UINT_MAX&&part.geometry.vertices.size()<=UINT_MAX-base,"Scripted actor skin geometry count overflow");selected.vertices.insert(selected.vertices.end(),part.geometry.vertices.begin(),part.geometry.vertices.end());
        for(auto index:part.geometry.indices)surface.indices.push_back(std::uint32_t(base)+index);selected.surfaces.push_back(std::move(surface));}
    const auto applied=skins.Apply(state.authoredSkin,selected);Need(selected.surfaces.size()==mesh.surfaces.size(),"Scripted actor source skin changed draw count");
    state.skinMatches=unsigned(applied.matchedSurfaces);state.skinChanges=unsigned(applied.changedSurfaces);state.authoredNoOpSkin=applied.changedSurfaces==0;
    Snapshot before(model);Need(before.model->NumSurfaces()==int(mesh.surfaces.size()),"Scripted actor native/source surface count differs");
    struct Pending {const idMaterial* from;render::DiffuseMaterial target;};std::vector<Pending> pending;std::map<const idMaterial*,unsigned> remaps;
    std::ostringstream identity;identity<<capabilityName<<'\n'<<state.authoredSkin<<'\n'<<wire::Fingerprint(bytes.data(),bytes.size())<<'\n';
    const auto modelSource=render::ParseResourcePath(source.modelResource);
    for(std::size_t i=0;i<mesh.surfaces.size();++i){const auto& draw=mesh.surfaces[i].geometry;const auto profile=render::BuildDiffuseMaterial(modelSource,draw,catalog.get());const auto* actual=before.model->Surface(int(i));
        Need(actual&&actual->id==int(i)&&actual->shader&&actual->geometry&&actual->geometry->numIndexes==int(draw.indices.size())&&profile.name==actual->shader->GetName()&&DeclText(actual->shader)==profile.declaration,
             "Scripted actor skin resident surface/material differs from original source");
        for(std::size_t corner=0;corner<draw.indices.size();++corner){const auto sourceIndex=draw.indices[corner];const auto nativeIndex=unsigned(actual->geometry->indexes[corner]);
            Need(sourceIndex<draw.vertices.size()&&nativeIndex<unsigned(actual->geometry->numVerts),"Scripted actor skin draw index outside vertices");const auto& vertex=draw.vertices[sourceIndex];const auto& native=actual->geometry->verts[nativeIndex];
            Need(native.st.x==vertex.u*(profile.supported?profile.binding.uvScale[0]:1.f)&&native.st.y==vertex.v*(profile.supported?profile.binding.uvScale[1]:1.f),"Scripted actor source/native UV encoding differs");
            const auto rgba=render::NativeDiffuseColor(profile,vertex.diffuseColor);for(unsigned c=0;c<4;++c)Need(native.color[c]==rgba[c],"Scripted actor source/native colour encoding differs");}
        if(draw.material==selected.surfaces[i].material)continue;
        auto replacement=draw;replacement.material=selected.surfaces[i].material;const auto target=render::BuildDiffuseMaterial(modelSource,replacement,catalog.get());
        Need(profile.supported&&target.supported,"Scripted actor authored skin needs an unsupported source shader");
        for(unsigned c=0;c<2;++c)Need(profile.binding.uvScale[c]==target.binding.uvScale[c],"Scripted actor skin would rebake shared UVs");
        for(const auto& vertex:draw.vertices)Need(render::NativeDiffuseColor(profile,vertex.diffuseColor)==render::NativeDiffuseColor(target,vertex.diffuseColor),"Scripted actor skin would re-encode shared colours");
        wolf2009::DecodeDdsMip0(Read(sources,target.image,64u<<20));const auto previous=remaps.find(actual->shader);
        if(previous!=remaps.end()){Need(pending[previous->second].target.declaration==target.declaration,"Scripted actor skin gives conflicting native remaps");continue;}
        remaps.emplace(actual->shader,unsigned(pending.size()));pending.push_back(Pending{actual->shader,target});identity<<profile.name<<'\n'<<target.name<<'\n'<<target.declaration;
    }
    const auto key=identity.str();const auto alias=std::string("wolf2009/scripted-actor/skin/")+Hex(wire::Fingerprint(reinterpret_cast<const unsigned char*>(key.data()),key.size()));state.nativeSkin=alias;
    std::ostringstream skinDeclaration;skinDeclaration<<"{\n";
    for(std::size_t i=0;i<pending.size();++i){SkinMaterial material;material.from=pending[i].from;material.fromText=DeclText(material.from);material.name=alias+"/material/"+std::to_string(i);material.text=pending[i].target.declaration;
        Declare(DECL_MATERIAL,material.name,material.text);material.to=declManager->FindMaterial(material.name.c_str(),false);Need(material.to&&material.to->GetState()!=DS_DEFAULTED&&material.to->IsDrawn(),"Scripted actor original diffuse material failed to parse");
        skinDeclaration<<" \""<<material.from->GetName()<<"\" \""<<material.name<<"\"\n";materials.push_back(std::move(material));}
    if(pending.empty()){std::set<const idMaterial*> seen;for(int i=0;i<before.model->NumSurfaces();++i){const auto* value=before.model->Surface(i)->shader;if(seen.insert(value).second)skinDeclaration<<" \""<<value->GetName()<<"\" \""<<value->GetName()<<"\"\n";}}
    skinDeclaration<<"}\n";skinText=skinDeclaration.str();Declare(DECL_SKIN,alias,skinText);skin=declManager->FindSkin(alias.c_str(),false);Need(skin&&skin->GetState()!=DS_DEFAULTED,"Scripted actor source skin failed to parse");
    Snapshot after(model,skin);Need(after.model->NumSurfaces()==before.model->NumSurfaces(),"Scripted actor original skin hid/added an unauthorised surface");
    for(int i=0;i<before.model->NumSurfaces();++i){const auto* a=before.model->Surface(i);const auto* b=after.model->Surface(i);Need(a&&b&&a->id==b->id&&a->geometry&&b->geometry&&a->geometry->numVerts==b->geometry->numVerts&&a->geometry->numIndexes==b->geometry->numIndexes,"Scripted actor skin changed shared mesh counts");
        for(int v=0;v<a->geometry->numVerts;++v){const auto& x=a->geometry->verts[v];const auto& y=b->geometry->verts[v];Need(x.xyz==y.xyz&&x.st==y.st&&std::equal(x.color,x.color+4,y.color),"Scripted actor skin changed original deformed XYZ/UV/RGBA");}
        for(int k=0;k<a->geometry->numIndexes;++k)Need(a->geometry->indexes[k]==b->geometry->indexes[k],"Scripted actor skin changed original indices");}
    state.sharedGeometryUnchanged=true;
}

std::unique_ptr<WolfSourceScriptedActor> Wolf2009_PrepareSourceScriptedActor(
    const WolfSourceDeclarationSet& set,const WolfSourceScriptedActorRequest& request,idStr& reason){reason.Clear();try{
    Need(request.policy==WolfSourceScriptedActorPolicy::NativeFullPoseNoExtractionNoFrameCommandsV1,"Scripted actor requires the explicit full-pose no-extraction/no-commands policy");
    Need(liveActors<32&&request.levelGeneration!=0,"Scripted actor owner budget or level generation is invalid");
    Need(request.expectedJoints>0&&request.expectedJoints<=256&&request.expectedFrames>=2&&request.expectedFrames<=1000000&&request.initialSourceFrame>=0&&unsigned(request.initialSourceFrame)<request.expectedFrames,
         "Scripted actor requires bounded explicit source joint/frame expectations");
    Need(request.resources.clips.size()==1,"Scripted actor slice needs exactly one explicitly selected original clip");
    Need(request.expectedEntitiesBytes||!request.expectedEntitiesFingerprint,"Scripted actor unknown ENTS identity has a nonzero fingerprint");
    const wolf2009::TrainDoorSignalRuntime checkedSignal(request.signal,request.levelGeneration);(void)checkedSignal;
    std::unique_ptr<WolfSourceScriptedActor::Impl> p(new WolfSourceScriptedActor::Impl);p->request=request;auto& state=p->state;
    const auto entsPath=Path(request.exactEntitiesPath,"ents");const auto& bytes=Read(p->sources,entsPath,64u<<20);const auto document=wolf2009::DecodeEntities4(bytes);
    state.entitiesBytes=bytes.size();state.entitiesFingerprint=wire::Fingerprint(bytes.data(),bytes.size());
    Need(!request.expectedEntitiesBytes||(request.expectedEntitiesBytes==state.entitiesBytes&&request.expectedEntitiesFingerprint==state.entitiesFingerprint),"Scripted actor ENTS bytes differ from the explicit request");
    Need(request.entityIndex<document.entities.size(),"Scripted actor selected ENTS index is absent");p->instance=document.entities[request.entityIndex];state.entityIndex=request.entityIndex;
    std::set<std::string> keys;for(const auto& field:p->instance.properties)Need(keys.insert(Fold(field.key)).second,"Scripted actor ambiguous source instance keys");
    state.sourceName=Field(p->instance,"name");state.sourceClass=Field(p->instance,"classname");state.authoredBrain=Field(p->instance,"brain");state.logicalClip=Text(request.signal.actorAnimation);
    unsigned matchingNames=0;for(const auto& entry:document.entities){const auto* name=entry.Find("name");if(name&&name->kind==wolf2009::EntityProperty::Text&&name->text==state.sourceName)++matchingNames;}
    Need(matchingNames==1,"Scripted actor source instance name is ambiguous");
    Need(state.sourceName==request.signal.ownerEntityName&&state.sourceClass==request.signal.ownerClassname&&Field(p->instance,"anim")==state.logicalClip&&
         Field(p->instance,"triggerStartMoveEnt",false)==request.signal.targetEntityName,"Scripted actor instance differs from its exact source signal owner/clip/target");
    auto brain=Fold(state.authoredBrain);if(brain.size()>=6&&brain.compare(brain.size()-6,6,".brain")==0)brain.resize(brain.size()-6);if(brain.compare(0,7,"brains/")!=0)brain="brains/"+brain;
    Need(brain==request.signal.brainResource,"Scripted actor source brain differs from its decoded signal");
    Need(Field(p->instance,"bind",false).empty(),"Scripted actor slice does not implement bound actor anchors");const auto placement=Placement(p->instance,p->origin,p->axis);
    state.hasAuthoredAngle=placement.hasAngle;state.authoredAngle=placement.angle;state.usesRedundantZeroAngleProfile=placement.hasAngle;
    if(const auto* frame=p->instance.Find("frame")){state.hasAuthoredFrame=true;
        if(frame->kind==wolf2009::EntityProperty::Integer)state.authoredFrame=frame->integer;
        else{Need(frame->kind==wolf2009::EntityProperty::Floats&&frame->numbers.size()==1,"Scripted actor authored frame is not one numeric fact");state.authoredFrame=frame->numbers[0];}
        Need(std::isfinite(state.authoredFrame),"Scripted actor authored frame is nonfinite");}
    p->defaults=set.Definitions().Resolve(wolf2009::SourceDefinitionKind::EntityDef,state.sourceClass,request.definitionContext);Selected(p->defaults,set.Namespace());
    state.sourceModelDefinition=p->instance.Find("model")?Field(p->instance,"model"):Scalar(p->defaults,"model");
    p->modelFacts=set.Definitions().ResolveModelAnimations(state.sourceModelDefinition,request.definitionContext);Selected(p->modelFacts.model,set.Namespace());ZeroOffset(p->modelFacts.model);
    for(const auto& fact:p->modelFacts.model.opaqueFacts){const auto key=Fold(fact.data.key.text);Need(key=="anim"||key=="channel","Scripted actor model has an unsupported non-animation/channel command");if(key=="channel")++state.retainedChannelFacts;}
    if(p->modelFacts.model.Find("skin"))Need(Scalar(p->modelFacts.model,"skin",false).empty(),"Scripted actor source model skin needs a separately qualified model/entity precedence rule");
    state.authoredSkin=p->instance.Find("skin")?Field(p->instance,"skin"):Scalar(p->defaults,"skin");
    for(const auto& field:p->defaults.literalFields)if(field.first=="def_head"||field.first.compare(0,10,"def_attach")==0)++state.retainedAttachmentFields;
    Need(request.signal.unexecutedNodeCount<=100000,"Scripted actor unsupported brain-node budget");state.unimplementedBrainNodes=unsigned(request.signal.unexecutedNodeCount);
    const auto& selection=request.resources.clips[0];const auto* sourceClip=p->modelFacts.Find(state.logicalClip);
    Need(sourceClip&&sourceClip->logicalName.text==state.logicalClip&&selection.logicalName==state.logicalClip,"Scripted actor exact source logical clip is absent");RetainCommands(*sourceClip,request.expectedFrames,state);
    const auto tree=set.Namespace().Lookup("animBlendTree",sourceClip->assetReference.text);
    Need(selection.treeProfile==actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1&&tree.status==wolf2009::CompiledCatalogLookupStatus::Missing&&tree.matches.empty(),"Scripted actor selected blend tree requires a different qualified evaluator");
    auto& wanted=p->wanted;wanted.entityDefinition=p->defaults.selected.declarationName;wanted.modelDefinition=p->modelFacts.model.selected.declarationName;
    wanted.meshReference=Scalar(p->modelFacts.model,"mesh");wanted.skeletonReference=Scalar(p->modelFacts.model,"skeleton");state.declaredSkeleton=wanted.skeletonReference;
    wanted.resources=request.resources;wanted.resources.clips.clear();wanted.resources.modelResource=Path(request.resources.modelResource,"modl");wanted.resources.meshSkeletonResource=Path(request.resources.meshSkeletonResource,"skel");
    wanted.resources.meshSkeletonName=Text(request.resources.meshSkeletonName);state.selectedMeshSkeleton=wanted.resources.meshSkeletonName;
    Need(state.selectedMeshSkeleton==state.declaredSkeleton,"Scripted actor must select the original declared mesh skeleton, not substitute a reference-different rig");
    Need(Stem(render::ParseResourcePath(wanted.resources.modelResource).name)==Stem(wanted.meshReference),"Scripted actor explicit MODL differs from the source mesh reference");
    actor::RequestClip clip;static_cast<actor::ClipSelection&>(clip)=selection;clip.authoredReference=Text(sourceClip->assetReference.text);clip.animationResource=Path(selection.animationResource,"hkxa");
    clip.animationSkeletonResource=Path(selection.animationSkeletonResource,"skel");Text(clip.animationSkeletonName);Text(clip.clipName);
    Need(Stem(clip.clipName)==Stem(clip.authoredReference),"Scripted actor selected HKXA key differs from the literal source animation reference");wanted.clips.push_back(clip);
    const auto path=actor::RequestPath(wanted);p->capability=actor::ParseCapability(Read(p->sources,path,actor::MaximumCapabilityBytes));
    Need(actor::Same(wanted,p->capability.request)&&p->capability.clips.size()==1,"Scripted actor readonly capability did not echo its complete request");p->capabilityName=actor::CapabilityName(p->capability);
    const auto& capabilityFile=p->sources.at(path)->file;Need(p->capabilityName==capabilityFile->GetName()&&p->capabilityName==capabilityFile->GetFullPath(),"Scripted actor capability file identity differs");
    state.levelGeneration=request.levelGeneration;state.nativeModel=p->capability.modelAlias;const auto& returned=p->capability.clips[0];state.joints=returned.bones;state.frames=returned.frames;state.lengthMs=returned.lengthMs;state.animationAlias=returned.animationAlias;
    Need(state.joints==request.expectedJoints&&state.frames==request.expectedFrames&&state.lengthMs>0&&state.lengthMs<=unsigned(INT_MAX),"Scripted actor native capability differs from the explicit source counts");
    p->model=renderModelManager->CheckModel(state.nativeModel.c_str());Need(p->model&&p->model->IsLoaded()&&!p->model->IsDefaultModel()&&p->model->IsDynamicModel()==DM_CACHED&&p->model->NumJoints()==int(state.joints)&&state.nativeModel==p->model->Name(),"Scripted actor original MODL did not produce its qualified native model");
    const auto envelope=wire::Parse(Read(p->sources,state.animationAlias,wire::MaximumEnvelopeBytes));DescriptorMatches(p->capability,envelope.binding);p->animationBinding=envelope.binding;
    auto* animation=animationLib.GetAnim(state.animationAlias.c_str());Need(animation&&animation->IsSourceAnimationV2()&&animation->SourceMatchesModel(p->model)&&animation->NumFrames()==int(state.frames)&&animation->Length()==int(state.lengthMs),"Scripted actor original clip did not produce its qualified native animation");
    animation->IncreaseRefs();p->animation=animation;p->PrepareSkin();
    WolfSourceAnimationSetEntry entry;entry.logicalName=state.logicalClip;entry.animationAlias=state.animationAlias;std::vector<WolfSourceAnimationSetEntry> entries(1,entry);idStr definition;
    NeedReason(Wolf2009_CreateSourceActorAnimationSet(entries,state.nativeSkin.c_str(),definition,reason),reason,"Scripted actor native no-commands modelDef creation failed");
    state.nativeModelDefinition=definition.c_str();p->definition=Wolf2009_FindRegisteredSourceAnimationSet(definition);p->definitionText=DeclText(p->definition);
    NeedReason(p->Check(reason),reason,"Scripted actor prepared owner preflight failed");
    return std::unique_ptr<WolfSourceScriptedActor>(new WolfSourceScriptedActor(std::move(p)));
}catch(const std::exception& error){reason=error.what();return std::unique_ptr<WolfSourceScriptedActor>();}}

WolfSourceScriptedActor::WolfSourceScriptedActor(std::unique_ptr<Impl> value):impl_(std::move(value)){}
WolfSourceScriptedActor::~WolfSourceScriptedActor(){Impl::Release(impl_.release());}
int Wolf2009_LiveSourceScriptedActors(){return liveActors;}
bool WolfSourceScriptedActor::Preflight(idStr& reason)const{return Impl::CheckAuthority(impl_.get(),reason);}
idAnimatedEntity* WolfSourceScriptedActor::NativeEntity()const{return impl_->entity.GetEntity();}
const wolf2009::EntityRecord& WolfSourceScriptedActor::SourceInstance()const{return impl_->instance;}
const wolf2009::ResolvedSourceDefinition& WolfSourceScriptedActor::SourceEntity()const{return impl_->defaults;}
const wolf2009::ResolvedSourceModelAnimations& WolfSourceScriptedActor::SourceModel()const{return impl_->modelFacts;}
bool WolfSourceScriptedActor::StartBinding(WolfSourceActorStartBinding& out,idStr& reason)const{if(!Preflight(reason))return false;
    WolfSourceActorStartBinding value;value.context=impl_.get();value.retain=Impl::Retain;value.release=Impl::Release;value.preflight=Impl::CheckAuthority;value.query=Impl::QueryAuthority;out=value;return true;}

bool WolfSourceScriptedActor::SpawnInactive(idStr& reason){reason.Clear();try{auto& p=*impl_;Busy guard(p.busy);NeedReason(p.Check(reason),reason,"Scripted actor pre-spawn preflight failed");
    Need(!p.spawned&&p.state.phase==WolfSourceScriptedActorPhase::Prepared,"Scripted actor cannot respawn or reset its original native identity");
    WolfSourceControlledRequest request;request.name=p.state.sourceName.c_str();request.modelDefinition=p.state.nativeModelDefinition.c_str();request.initialClip=p.state.logicalClip.c_str();
    request.initialSourceFrame=p.request.initialSourceFrame;request.origin=p.origin;request.axis=p.axis;request.policy=WolfSourceControlled_NativeMillisecondsHoldLastV2;
    idAnimatedEntity* native=NULL;NeedReason(Wolf2009_SpawnSourceControlled(request,native,reason)&&native,reason,"Scripted actor native inactive spawn failed");
    p.entity=native;p.spawned=true;p.state.nativeSpawnId=unsigned(p.entity.GetSpawnId());
    try{WolfSourceControlledState held;Need(Wolf2009_SourceControlledState(native,held,reason)&&held.phase==WolfSourceControlled_Held&&held.sourceFrame==p.request.initialSourceFrame&&held.commandSerial!=0,
            "Scripted actor native spawn did not hold the explicit source frame");p.initialSerial=held.commandSerial;p.initialTime=held.commandTimeMs;
        NeedReason(p.Check(reason),reason,"Scripted actor inactive native owner differs");p.state.phase=WolfSourceScriptedActorPhase::Held;return true;
    }catch(const std::exception& error){p.failure=error.what();if(p.entity.GetEntity())delete p.entity.GetEntity();throw;}
}catch(const std::exception& error){reason=error.what();return false;}}

bool WolfSourceScriptedActor::Start(const wolf2009::TrainDoorCommand& command,WolfSourceScriptedActorStartAck& out,idStr& reason){out=WolfSourceScriptedActorStartAck();reason.Clear();try{auto& p=*impl_;Busy guard(p.busy);
    NeedReason(p.Check(reason),reason,"Scripted actor start preflight failed");
    Need(command.kind==wolf2009::TrainDoorCommand::StartActorAnimation&&command.ownerEntityName==p.state.sourceName&&command.resourceName==p.state.logicalClip&&command.targetEntityName.empty()&&
         command.levelGeneration==p.request.levelGeneration&&command.eventId!=0&&command.sourceNodeId==p.request.signal.actorStartNodeId,"Scripted actor start command does not match its exact generation/event/node/owner/resource");
    if(p.ack.committed){Need(SameCommand(command,p.ack.command),"Scripted actor already committed another event");Need(p.state.duplicateStarts!=UINT_MAX,"Scripted actor duplicate counter overflow");
        auto acknowledged=p.ack;acknowledged.duplicate=true;out=acknowledged;++p.state.duplicateStarts;return true;}
    Need(p.spawned&&p.state.phase==WolfSourceScriptedActorPhase::Held,"Scripted actor Start requires its original inactive spawned actor");
    // Allocate/copy the immutable command before the genuine native mutation.
    WolfSourceScriptedActorStartAck accepted;accepted.command=command;accepted.levelGeneration=command.levelGeneration;accepted.nativeSpawnId=p.state.nativeSpawnId;
    NeedReason(Wolf2009_SourceControlledPlayOnce(p.entity.GetEntity(),p.state.logicalClip.c_str(),0,reason),reason,"Scripted actor native PlayOnce was refused");
    try{auto* native=p.entity.GetEntity();auto* animator=native?native->GetAnimator():NULL;const auto* blend=animator?animator->CurrentAnim(ANIMCHANNEL_ALL):NULL;WolfSourceControlledState playing;
        Need(native&&animator&&blend&&Wolf2009_SourceControlledState(native,playing,reason)&&playing.phase==WolfSourceControlled_Playing&&playing.clip==p.state.logicalClip.c_str()&&playing.sourceFrame==-1&&
             playing.commandTimeMs==gameLocal.time&&playing.commandSerial==p.initialSerial+1&&playing.endTimeMs>gameLocal.time&&playing.endTimeMs-gameLocal.time==int(p.state.lengthMs)&&playing.blendEndTimeMs==gameLocal.time,
             "Scripted actor native controller did not commit the requested one-shot");
        Need(blend->AnimNum()==p.definition->GetSpecificAnim(p.state.logicalClip.c_str())&&blend->GetStartTime()==gameLocal.time&&blend->GetEndTime()==playing.endTimeMs&&blend->GetCycleCount()==1&&blend->GetPlaybackRate()==1.f,
             "Scripted actor actual native channel did not start the exact source clip");
        const double paletteError=EvaluatePose(*native,*p.animation,0);
        accepted.commandSerial=playing.commandSerial;accepted.committedTimeMs=gameLocal.time;accepted.committed=true;accepted.actualPoseEvaluated=true;
        p.state.phase=WolfSourceScriptedActorPhase::Playing;p.state.nativeStartTimeMs=gameLocal.time;p.state.nativeEndTimeMs=playing.endTimeMs;p.state.commandSerial=playing.commandSerial;
        p.state.eventId=command.eventId;p.state.startCommits=1;p.state.actualStartPoseEvaluated=true;p.state.startPaletteError=paletteError;
        p.ack=std::move(accepted);NeedReason(p.Check(reason),reason,"Scripted actor committed native channel lost ownership");out=p.ack;return true;
    }catch(const std::exception& error){p.failure=error.what();if(p.entity.GetEntity())delete p.entity.GetEntity();throw;}
}catch(const std::exception& error){reason=error.what();return false;}}

bool WolfSourceScriptedActor::Query(WolfSourceScriptedActorState& out,idStr& reason){reason.Clear();try{auto& p=*impl_;Busy guard(p.busy);NeedReason(p.Check(reason),reason,"Scripted actor query preflight failed");
    if(p.ack.committed&&p.state.phase==WolfSourceScriptedActorPhase::Playing){WolfSourceControlledState actual;
        NeedReason(Wolf2009_SourceControlledState(p.entity.GetEntity(),actual,reason),reason,"Scripted actor native completion state is unavailable");
        if(actual.phase==WolfSourceControlled_Held){const double error=EvaluatePose(*p.entity.GetEntity(),*p.animation,int(p.state.frames)-1);
            p.state.phase=WolfSourceScriptedActorPhase::Completed;p.state.completions=1;p.state.completionObservedTimeMs=gameLocal.time;p.state.completionPaletteError=error;}}
    out=p.state;return true;
}catch(const std::exception& error){reason=error.what();return false;}}
