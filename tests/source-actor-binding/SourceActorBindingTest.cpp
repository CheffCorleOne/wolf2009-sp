// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceActorBinding.h"
#include "Archive.h"
#include "ZipArchive.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
using namespace wolf2009;
namespace actor=wolf2009::actor;
typedef actor::Bytes Bytes;
std::size_t checks=0,groups=0;
void Need(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void Reject(F body,const char* message){bool refused=false;try{body();}catch(const std::exception&){refused=true;}Need(refused,message);}
void U32(Bytes& out,std::uint32_t n){for(unsigned i=0;i<4;++i)out.push_back(static_cast<unsigned char>(n>>(i*8)));}
void Text(Bytes& out,const std::string& text){out.insert(out.end(),text.begin(),text.end());}
void SetU32(Bytes& out,std::size_t at,std::uint32_t n){for(unsigned i=0;i<4;++i)out.at(at+i)=static_cast<unsigned char>(n>>(i*8));}
Bytes Packet(const std::vector<std::string>& records){Bytes bytes;U32(bytes,0);U32(bytes,0);Text(bytes,"1\nown/actor.def\n3\n"+std::to_string(records.size())+"\n");
    for(std::size_t i=0;i<records.size();++i)Text(bytes,std::to_string(i)+"\n"+std::to_string(records[i].size())+"\n"+records[i]);Text(bytes,"0\n");return bytes;}
struct Fixtures {
    CompiledDeclarationCatalog catalog;
    CompiledDeclarationNamespace names{CompiledNamespacePolicy::OrdinaryTextOwnersV1};
    SourceDefinitions definitions{SourceDefinitionLimits(),SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1};
    explicit Fixtures(const std::vector<std::string>& records){const auto doc=CompiledDeclarations::Read(Packet(records),CompiledDeclFormat::TwoSectionLfTextV1);
        CompiledCatalogSource source;source.role=CompiledCatalogRole::Explicit;source.loadIdentity="own.actor";source.path="own/actor.decls";catalog.AddDocument(doc,source);
        names.ApplySection(catalog,0,0,CompiledNamespaceReaderMode::Ordinary);for(const auto& entry:names.SelectedEntries())if(entry.type=="model"||entry.type=="entityDef")definitions.AddCompiledRecord(entry);}
};
std::vector<std::string> Records(const std::string& block="{frame 1 sound snd_open}"){
    return {"entityDef actor {\n\"spawnclass\" 0 0 \"rvAnimatedContainer\"\n\"model\" 0 0 \"model_actor\"\n\"start_anim\" 0 0 \"idle\"\n\"anim1\" 0 0 \"open\"\n\"num_anims\" 4 1 1\n\"ragdoll\" 0 0 \"own_af\"\n\"snd_open\" 0 0 \"own_open\"\n\"snd_close\" 0 0 \"own_close\"\n}",
        "model model_actor {\nmesh models/own/actor.md5r\nskeleton models/own/actor_skel.hkx\nanim idle models/own/actor_idle.md5anim\nanim open models/own/actor_open.md5anim "+block+"\nanim idle_open models/own/actor_idle_open.md5anim\nanim close models/own/actor_close.md5anim {frame 1 sound snd_close}\nanim ragdoll models/own/actor_idle.md5anim\nanim af_pose models/own/actor_idle.md5anim\n}",
        "sfx own_open { dataOnlySelectedSound }","sfx own_close { dataOnlySelectedSound }"};
}
actor::ResourceSelection Selection(){
    actor::ResourceSelection selection;selection.modelResource="wolf2009/mesh/MODL/models/own/actor";selection.meshSkeletonResource="wolf2009/mesh/SKEL/maps/own/scene";
    selection.meshSkeletonName="models/own/actor_skel.hkx";selection.geometryProfile=actor::GeometryProfile::WeightedOrRigid64;selection.lodPolicy=actor::LodPolicy::SingleResourceWithoutLodRecords;
    for(const auto* key:{"idle","open","idle_open","close"}){actor::ClipSelection clip;clip.logicalName=key;clip.animationResource="wolf2009/anim/HKXA/maps/own/scene";clip.animationSkeletonResource="wolf2009/rig/SKEL/maps/own/scene";
        clip.clipName=std::string("models/own/actor_")+key+".hkx";clip.animationSkeletonName="models/own/actor_skel.hkx";clip.treeProfile=actor::TreeProfile::SelectedMissingExplicitDefaultSingleKeyframeV1;selection.clips.push_back(clip);}return selection;
}
actor::Plan Prepare(Fixtures& data,const actor::ResourceSelection& selection=Selection()){return actor::Prepare(data.definitions,data.names,"actor","",SourceDefinitionContext(),selection);}
actor::Capability Capability(const actor::Request& request){
    actor::Capability value;value.request=request;value.modelAlias="wolf2009/mesh/md5r/v2/lod0/1234567890abcdef/models/own/actor";
    for(std::size_t i=0;i<request.clips.size();++i){actor::CapabilityClip clip;clip.animationAlias="wolf2009/_animation/anim/v2/"+std::string(15,'0')+char('0'+i)+".md5anim";
        clip.animationMetadata={1,2,3,static_cast<unsigned char>(i)};clip.frames=unsigned(2+i);clip.bones=2;clip.lengthMs=unsigned(42+i);
        for(unsigned role=0;role<4;++role){clip.sources[role].epoch=role==0||role==1?7:role==2?8:9;clip.sources[role].bytes=100+role;clip.sources[role].fingerprint=1000+role;}value.clips.push_back(clip);}return value;
}
void Synthetic(){
    ++groups;actor::Plan owned;
    {Fixtures data(Records());owned=Prepare(data);Need(owned.Valid()&&owned.ClipCount()==4&&owned.Model().references.size()==6,"model selected clips/AF facts were lost");
        Need(owned.FindClip("open")&&owned.FindClip("open")->request.frameSounds.size()==1&&owned.FindClip("open")->request.frameSounds[0].soundKey=="snd_open"&&
            owned.FindClip("open")->request.frameSounds[0].sourceSound=="own_open","authored frame1 sound was not retained/resolved");
        Need(!owned.FindClip("OPEN")&&owned.Model().Find("af_pose")&&owned.Entity().Find("ragdoll"),"exact logical identity or data-only AF provenance lost");
        Need(owned.MatchesEntityModel("ACTOR","MODEL_ACTOR",SourceDefinitionContext())&&!owned.MatchesEntityModel("actor","other",SourceDefinitionContext()),"actor/model name matching failed");
        auto context=SourceDefinitionContext();context.mapModelDefinition="different";Need(!owned.MatchesEntityModel("actor","model_actor",context),"actor context mismatch accepted");
    }
    Need(owned.Entity().selected.member.compiledRecord->IsUnmodified()&&owned.Model().model.selected.member.compiledRecord->IsUnmodified()&&owned.SelectedInputs().size()==1&&
        owned.Clip(1).commandOrigins[0].origin.member.compiledRecord->IsUnmodified()&&owned.Clip(1).soundDeclarations.size()==1&&
        owned.Clip(1).soundDeclarations[0].IsUnmodified()&&owned.Clip(1).soundDeclarations[0].name=="own_open","actor plan lost owning provenance after inputs died");
    Reject([&]{owned.Clip(4);},"actor out-of-range clip accepted");Reject([]{actor::Plan().Entity();},"empty actor plan used");

    ++groups;const auto request=owned.ResourceRequest();const auto bytes=actor::SerializeRequest(request);const auto path=actor::RequestPath(request);
    Need(actor::Same(request,actor::ParseRequest(bytes))&&actor::Same(request,actor::ParseRequestPath(path))&&Wolf2009_IsSourceActorRequest(path.c_str()),"actor request binary/path did not roundtrip");
    Need(!Wolf2009_IsSourceActorRequest(NULL)&&!Wolf2009_IsSourceActorRequest("wolf2009/_actor/other"),"actor router claimed unrelated path");
    for(std::size_t n=0;n<bytes.size();++n)Reject([&]{actor::ParseRequest(Bytes(bytes.begin(),bytes.begin()+n));},"truncated actor request accepted");
    auto bad=bytes;bad.push_back(0);Reject([&]{actor::ParseRequest(bad);},"actor request trailing bytes accepted");bad=bytes;bad[0]^=1;Reject([&]{actor::ParseRequest(bad);},"unknown actor schema accepted");
    for(const auto offset:{8u,12u,16u,24u,28u}){bad=bytes;SetU32(bad,offset,0xffffffffu);Reject([&]{actor::ParseRequest(bad);},"malformed request profile/count/string accepted");}
    Reject([&]{actor::ParseRequestPath(path+"0");},"odd actor request hex accepted");bad=bytes;std::string upper=path;upper.back()='a'&&upper.back()<='f'?upper.back()-=32:upper.back()='G';Reject([&]{actor::ParseRequestPath(upper);},"noncanonical actor request hex accepted");
    Reject([&]{actor::ParseRequestPath(std::string(actor::MaximumRequestPathBytes()+1,'0'));},"oversized actor request path accepted");
    const auto changed=[&](actor::Request value){Need(!actor::Same(request,value),"actor full-key comparer ignored a declared field");};
    auto alternate=request;alternate.entityDefinition="other";changed(alternate);alternate=request;alternate.modelDefinition="other";changed(alternate);
    alternate=request;alternate.resources.expected[0].epoch=17;changed(alternate);alternate=request;alternate.clips[1].expected[0].epoch=18;changed(alternate);
    alternate=request;alternate.clips[1].frameSounds[0].sourceSound="another_source";changed(alternate);alternate=request;alternate.clips[0].animationSkeletonName="different_skeleton";changed(alternate);
    alternate=request;alternate.resources.lodPolicy=actor::LodPolicy::Authored;changed(alternate);alternate=request;alternate.resources.geometryProfile=actor::GeometryProfile::Weighted64;changed(alternate);

    ++groups;
    for(const auto* block:{"{frame 2 sound snd_open}","{frame 1 event other}","{random_cycle_start}","{frame 1 sound snd_missing}","{frame 1 sound snd_open frame 1 sound snd_close}"}){
        Fixtures data(Records(block));Reject([&]{Prepare(data);},"unknown frame command/options became playable");}
    {auto records=Records();records.push_back("animBlendTree models/own/actor_open { explicit override }");Fixtures data(records);Reject([&]{Prepare(data);},"selected explicit tree was silently replaced by default");}
    {auto records=Records();records.erase(records.begin()+2);Fixtures data(records);Reject([&]{Prepare(data);},"actor accepted a frame sound absent from its selected namespace");}
    {Fixtures data(Records());auto selection=Selection();selection.clips[0].treeProfile=actor::TreeProfile::Unspecified;Reject([&]{Prepare(data,selection);},"missing selected tree inferred default permission");
        selection=Selection();selection.clips[1].clipName="models/other.hkx";Reject([&]{Prepare(data,selection);},"arbitrary HKXA extension rewrite/mapping accepted");
        selection=Selection();selection.clips[0].logicalName="Idle";Reject([&]{Prepare(data,selection);},"inexact logical source name selected");
        selection=Selection();selection.modelResource="wolf2009/mesh/MODL/models/other";Reject([&]{Prepare(data,selection);},"wrong MODL selected by shape/name count");
        selection=Selection();selection.meshSkeletonName="guessed_skeleton";Reject([&]{Prepare(data,selection);},"guessed mesh skeleton accepted");
        selection=Selection();selection.meshSkeletonName="models/own/actor_skel.HKX";Reject([&]{Prepare(data,selection);},"named mesh skeleton was silently case/extension normalized");
        selection=Selection();selection.clips.push_back(selection.clips[0]);Reject([&]{Prepare(data,selection);},"duplicate selected clip accepted");
        selection=Selection();selection.levelOfDetail=1;Reject([&]{Prepare(data,selection);},"no-LOD compatibility profile accepted a nonzero slot");
        selection=Selection();selection.expected[0].epoch=1;selection.expected[1].epoch=2;Reject([&]{Prepare(data,selection);},"shared mount conflicting epochs accepted");
        selection=Selection();selection.clips[0].animationResource="wolf2009/anim/hkxa/../escape";Reject([&]{Prepare(data,selection);},"typed resource traversal accepted");
        CompiledDeclarationNamespace other(CompiledNamespacePolicy::OrdinaryTextOwnersV1);Reject([&]{actor::Prepare(data.definitions,other,"actor","",SourceDefinitionContext(),Selection());},"unselected compiled definitions became an actor");}
    {auto records=Records();records[1].insert(records[1].size()-1,"channel head ( origin )\n");Fixtures data(records);Reject([&]{Prepare(data);},"unknown model command was discarded");}

    ++groups;const auto capability=Capability(request);const auto response=actor::SerializeCapability(capability);
    Need(actor::SerializeCapability(actor::ParseCapability(response))==response&&actor::CapabilityName(capability)==actor::CapabilityName(actor::ParseCapability(response)),"actor capability/full-key name did not roundtrip");
    for(std::size_t n=0;n<response.size();++n)Reject([&]{actor::ParseCapability(Bytes(response.begin(),response.begin()+n));},"truncated actor capability accepted");
    bad=response;bad.push_back(1);Reject([&]{actor::ParseCapability(bad);},"trailing actor capability bytes accepted");
    auto altered=capability;altered.clips[1].sources[0].fingerprint^=1;Reject([&]{actor::SerializeCapability(altered);},"same model path had inconsistent capability bytes");
    altered=capability;altered.clips[1].sources[2].epoch++;Reject([&]{actor::SerializeCapability(altered);},"shared animation mount had inconsistent epochs");
    altered=capability;altered.clips[0].frames=1;Reject([&]{actor::SerializeCapability(altered);},"invalid V2 frame count accepted");
    altered=capability;altered.clips[0].animationMetadata.clear();Reject([&]{actor::SerializeCapability(altered);},"missing canonical V2 metadata accepted");
    altered=capability;altered.request.resources.expected[0].epoch=100;Reject([&]{actor::SerializeCapability(altered);},"capability ignored explicitly requested epoch");
    altered=capability;altered.request.resources.expected[0].byteIdentityKnown=true;altered.request.resources.expected[0].bytes=100;altered.request.resources.expected[0].fingerprint=999;
    Reject([&]{actor::SerializeCapability(altered);},"capability ignored explicitly requested byte fingerprint");
    altered=capability;altered.clips[1].animationMetadata[0]^=1;Need(actor::CapabilityName(altered)!=actor::CapabilityName(capability),"capability identity omitted full V2 metadata");

    ++groups;const double period=double(float(1.0/24.0));
    auto clock=actor::CycleAtMilliseconds(42,1,period);Need(clock.finished&&clock.completedCycles==0&&clock.seconds==period,"one-shot ceil end did not preserve source endpoint");
    clock=actor::CycleAtMilliseconds(42,1073741824,period);Need(!clock.finished&&clock.completedCycles==1&&clock.seconds>0&&clock.seconds<.001,"retail startup finite count was truncated/ceil-ms period used");
    clock=actor::CycleAtMilliseconds(125,3,period);Need(!clock.finished&&clock.completedCycles==2,"finite source cycle completed before original float duration");
    clock=actor::CycleAtMilliseconds(126,3,period);Need(clock.finished&&clock.completedCycles==2&&clock.seconds==period,"finite source cycle final endpoint is not exact");
    for(int time:{-100,0,1,42,137,1001,123456789,INT_MAX}){const auto point=actor::CycleAtMilliseconds(time,-1,period);
        Need(!point.finished&&point.seconds>=0&&point.seconds<period&&point.completedCycles>=0,"infinite source cycle coordinate invalid");
        if(time>0)Need(std::abs((point.completedCycles*period+point.seconds)-double(time)*.001)<.0000001,"source cycle lost elapsed time");}
    Reject([&]{actor::CycleAtMilliseconds(1,0,period);},"zero unsupported cycle count accepted");Reject([&]{actor::CycleAtMilliseconds(1,-2,period);},"unknown negative cycle count accepted");
    for(double duration:{0.0,-1.0,(std::numeric_limits<double>::infinity)(),(std::numeric_limits<double>::quiet_NaN)()})Reject([&]{actor::CycleAtMilliseconds(1,1,duration);},"invalid source duration accepted");
    Reject([]{actor::CycleAtMilliseconds(INT_MAX,-1,1e-20);},"overflowing source completed-cycle index accepted");
}
Bytes Decl(const std::string& path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("source selected package absent");const auto archive=Archive::Scan(file);std::size_t selected=0,count=0;
    for(std::size_t i=0;i<archive.Chunks().size();++i)if(archive.Chunks()[i].type=="DECL"){selected=i;++count;}Need(count==1&&archive.Chunks()[selected].entries.size()==1,"selected DECL is not unique");return archive.ReadEntry(file,selected,0);}
void Original(const std::string& base){
    ++groups;CompiledDeclarationCatalog catalog;std::ifstream assetsFile(base+"/assets.pk4",std::ios::binary);if(!assetsFile)throw std::runtime_error("source assets absent");const auto assets=ZipArchive::Scan(assetsFile);
    const std::string paths[]={"maps/game/global/global_vo_english.decls","maps/game/global/global.decls","maps/game/trainyard/trainyard_vo_english.spk","maps/game/trainyard/trainyard.mpk"};
    for(unsigned i=0;i<4;++i){Bytes bytes;if(i<2){bool found=false;for(std::size_t n=0;n<assets.Entries().size();++n)if(assets.Entries()[n].name==paths[i]){Need(!found,"duplicate selected assets member");bytes=assets.ReadEntry(assetsFile,n);found=true;}Need(found,"selected global decl member absent");}else bytes=Decl(base+"/"+paths[i]);
        const auto doc=CompiledDeclarations::Read(bytes,CompiledDeclFormat::TwoSectionLfTextV1);CompiledCatalogSource source;source.loadIdentity="actor.original."+std::to_string(i);source.path=paths[i];source.role=i<2?CompiledCatalogRole::Global:CompiledCatalogRole::Map;catalog.AddDocument(doc,source);}
    CompiledDeclarationNamespace names(CompiledNamespacePolicy::OrdinaryTextOwnersV1);for(const auto& step:std::vector<std::pair<unsigned,unsigned>>{{0,0},{1,0},{1,1},{2,0},{2,1},{3,0},{3,1}})names.ApplySection(catalog,step.first,step.second,step.first<2?CompiledNamespaceReaderMode::ProtectAccepted:CompiledNamespaceReaderMode::Ordinary);
    SourceDefinitionLimits limits;limits.members=20000;limits.totalSourceBytes=128u<<20;
    SourceDefinitions definitions(limits,SourceDefinitionNamePolicy::AsciiCaseSlashLastDotV1);
    for(const auto& entry:names.SelectedEntries())if(entry.type=="model"||entry.type=="entityDef")definitions.AddCompiledRecord(entry);
    auto selection=Selection();selection.modelResource="wolf2009/actor/MODL/models/props/locker_foot_01_anim";selection.meshSkeletonResource="wolf2009/actor/SKEL/maps/game/trainyard/trainyard";selection.meshSkeletonName="models/props/locker_foot_01_anim_skel.hkx";
    for(auto& clip:selection.clips){clip.animationResource="wolf2009/actor/HKXA/maps/game/trainyard/trainyard";clip.animationSkeletonResource=selection.meshSkeletonResource;clip.clipName="models/props/locker_foot_01_anim_"+clip.logicalName+".hkx";clip.animationSkeletonName=selection.meshSkeletonName;}
    const auto plan=actor::Prepare(definitions,names,"container_locker_foot_01","",SourceDefinitionContext(),selection);
    Need(plan.ClipCount()==4&&plan.Model().references.size()==6&&plan.SelectedInputs().size()==7,"original actor selected model/clip/startup facts differ");
    Need(plan.FindClip("open")->request.frameSounds[0].sourceSound=="footlocker_open"&&plan.FindClip("close")->request.frameSounds[0].sourceSound=="footlocker_close","actual container authored sounds differ");
    const auto* initial=plan.Entity().Find("start_anim");Need(initial&&initial->value.size()==1&&initial->value[0].text=="idle","actual container initial animation differs");
    Need(plan.Entity().Find("ragdoll")&&plan.Entity().Find("def_afcore")&&plan.Entity().Find("giveDelay"),"original actor physical/loot obligations were discarded");
    const auto encoded=actor::RequestPath(plan.ResourceRequest());Need(actor::Same(actor::ParseRequestPath(encoded),plan.ResourceRequest()),"original source plan wire changed declared identities");
    std::cout<<"SOURCE_ACTOR_ORIGINAL selected="<<names.SelectedCount()<<" declarations="<<definitions.DefinitionCount()<<" model_refs="<<plan.Model().references.size()<<" clips="<<plan.ClipCount()<<" commands=2 request_bytes="<<actor::SerializeRequest(plan.ResourceRequest()).size()<<" selected_missing_trees=4 no_native_claim=1\n";
}
}
int main(int argc,char** argv){try{if(argc==2&&std::string(argv[1])=="--self-test")Synthetic();else if(argc==3&&std::string(argv[1])=="--source-base")Original(argv[2]);else throw std::runtime_error("usage: --self-test | --source-base <SP/base>");
    std::cout<<"SOURCE_ACTOR_BINDING_OK checks="<<checks<<" groups="<<groups<<'\n';return 0;}catch(const std::exception& error){std::cerr<<"SOURCE_ACTOR_BINDING_FAIL checks="<<checks<<" groups="<<groups<<" reason="<<error.what()<<'\n';return 1;}}
