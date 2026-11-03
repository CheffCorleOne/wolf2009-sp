// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinnedModelData.h"
#include "SourceRigAnimation.h"
#include "AnimationWireV2.h"
#include "StreamMounts.h"
#include "RootLodProfileFixtures.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
namespace {
using namespace wolf2009;
using namespace wolf2009::idtech4a;
namespace wire=wolf2009::animation2;
std::size_t checks=0,poses=0,corners=0;
double maximumPositionError=0;
void Need(bool yes,const char* message) {++checks;if(!yes)throw std::runtime_error(message);}
void Reject(const std::function<void()>& operation) {
    bool refused=false;try {operation();}catch(const std::exception&) {refused=true;}
    Need(refused,"unsupported root-slot source was accepted");
}
SkinnedMeshRequestV2 OwnRequest(SkinnedLodPolicy policy=SkinnedLodPolicy::RootAuthoredMeshUnspecifiedSlotV1) {
    return MakeSkinnedMeshRequestV2("wolf2009/own/modl/models/own/body","wolf2009/skel/skel/maps/own/skel",
        "rig-A",0,SkinnedVertexProfile::WeightedOrRigid64,policy);
}
void ComparePose(const SkinnedMeshDataV2& prepared,const GreeterSkinGeometry& source,const GreeterPose& pose) {
    const auto expected=PoseWeightedModel(source,pose);++poses;
    Need(expected.surfaces.size()==prepared.surfaces.size(),"prepared draw count differs");
    for(std::size_t s=0;s<prepared.surfaces.size();++s) {
        const auto& draw=prepared.surfaces[s];const auto positions=SkinPreparedSurface(prepared,s,pose.model);
        Need(draw.geometry.material==expected.surfaces[s].material&&draw.geometry.indices.size()==expected.surfaces[s].indices.size(),
            "prepared material/corner count differs");
        for(std::size_t corner=0;corner<draw.geometry.indices.size();++corner) {
            const auto v=draw.geometry.indices[corner];Need(v<positions.size(),"prepared corner exceeds vertices");
            Need(draw.sourceVertices[v]==expected.surfaces[s].indices[corner],"prepared corner source identity differs");
            const auto& wanted=expected.vertices[draw.sourceVertices[v]];const auto& actual=draw.geometry.vertices[v];
            const float xyz[]={wanted.x,wanted.y,wanted.z};
            for(unsigned axis=0;axis<3;++axis) {
                const double error=std::abs(double(positions[v][axis])-xyz[axis]);
                maximumPositionError=(std::max)(maximumPositionError,error);
                Need(std::isfinite(positions[v][axis])&&error<0.0002,"MD5 joint-local reconstruction differs from source skinning");
            }
            Need(actual.u==wanted.u&&actual.v==wanted.v&&actual.diffuseColor==wanted.diffuseColor,
                "source UV/color changed in prepared geometry");++corners;
        }
    }
}
std::vector<GreeterJoint> Topology(const SkeletonData& skeleton) {
    std::vector<GreeterJoint> result;
    for(const auto& joint:skeleton.joints) {GreeterJoint j;j.name=joint.name;j.parent=joint.parent;result.push_back(j);}
    return result;
}
void Synthetic() {
    for(unsigned joints:{3u,7u})for(bool reverse:{false,true}) {
        meshv2fixtures::Skeletons fixture(joints);const auto skel=fixture.pack.Finish();
        auto model=rootlodfixtures::Model(joints,{-1,0},{"200"},true,reverse);
        unsigned reads=0;
        const ResourceReader read=[&](const std::string& path) {
            ++reads;const auto type=ParseResourcePath(path).type;
            if(type=="modl")return model;if(type=="skel")return skel;
            throw std::runtime_error("HKXA read trap");
        };
        const auto request=OwnRequest();const auto prepared=LoadSkinnedMeshV2(request,read);
        Need(reads==2&&prepared.joints.size()==joints&&prepared.surfaces.size()==2,"mixed weighted/rigid source did not load exactly two sources");
        Need(prepared.sourceHasLodRecords&&prepared.selectionUsesCompatibilitySlot&&prepared.sourceLodDistances==std::vector<double>{200}&&
            prepared.sourceMeshLods.size()==2&&!prepared.sourceMeshLods[0].hasLevelOfDetail&&
            prepared.sourceMeshLods[1].hasLevelOfDetail&&prepared.sourceMeshLods[1].levelOfDetail==0,
            "root/mesh LOD facts were conflated with the compatibility selection");
        const auto topology=Topology(prepared.skeleton);
        const auto source=DecodeWeightedModel1(model,"models/own/body",topology,0,
            request.geometryProfile,request.lodPolicy);
        GreeterPose bind;bind.model=source.bindModelJoints;ComparePose(prepared,source,bind);
        auto moved=bind;moved.model.back().translation[0]+=.75;moved.model.back().translation[2]-=.25;
        ComparePose(prepared,source,moved);
        const auto legacy=MakeSkinnedModelRequest(request.model,"wolf2009/own/hkxa/maps/own/clip",request.skeletonContainer,0);
        auto oversized=source;oversized.sourceLodDistances.resize(33,200);
        Reject([&] {PrepareSkinnedModel(legacy,oversized,topology);});
        oversized=source;oversized.sourceMeshLods.resize(4097);
        Reject([&] {PrepareSkinnedModel(legacy,oversized,topology);});
        Need(prepared.surfaces[0].weights[0][0].weight==(reverse?1.f:.25f)&&
            prepared.surfaces[1].weights[0][0].weight==(reverse?.25f:1.f),"weighted/rigid buffer selection changed");
        Reject([&] {LoadSkinnedMeshV2(OwnRequest(SkinnedLodPolicy::Authored),read);});
        Reject([&] {LoadSkinnedMeshV2(OwnRequest(SkinnedLodPolicy::SingleResourceWithoutLodRecords),read);});
        auto bad=request;bad.geometryProfile=SkinnedVertexProfile::Weighted64;Reject([&] {LoadSkinnedMeshV2(bad,read);});
        bad=request;bad.levelOfDetail=1;reads=0;Reject([&] {LoadSkinnedMeshV2(bad,read);});Need(!reads,"nonzero compatibility slot reached reader");
        bad=request;bad.lodPolicy=static_cast<SkinnedLodPolicy>(4);reads=0;
        Reject([&] {LoadSkinnedMeshV2(bad,read);});Need(!reads,"unknown LOD policy reached reader");
        for(const auto& distances:std::vector<std::vector<std::string>>{{},{"200","400"},{"0"},{"-1"},{"nan"},{"inf"}}) {
            model=rootlodfixtures::Model(joints,{-1,0},distances);Reject([&] {LoadSkinnedMeshV2(request,read);});
        }
        for(const auto& markers:std::vector<std::vector<int>>{{0,0},{-1,1},{-1,-2},{-1,100}}) {
            model=rootlodfixtures::Model(joints,markers);Reject([&] {LoadSkinnedMeshV2(request,read);});
        }
        model=rootlodfixtures::Model(joints,{-1,0},{"200"},false);Reject([&] {LoadSkinnedMeshV2(request,read);});
        model=rootlodfixtures::Model(joints,{-1,-1});
        const auto absent=LoadSkinnedMeshV2(request,read);
        Need(absent.sourceMeshLods.size()==2&&!absent.sourceMeshLods[0].hasLevelOfDetail&&!absent.sourceMeshLods[1].hasLevelOfDetail,
            "all missing mesh markers were invented as authored zero");
        for(const auto order:std::vector<std::vector<SkinnedLodPolicy>>{
            {SkinnedLodPolicy::Authored,SkinnedLodPolicy::SingleResourceWithoutLodRecords,request.lodPolicy},
            {request.lodPolicy,SkinnedLodPolicy::SingleResourceWithoutLodRecords,SkinnedLodPolicy::Authored}}) {
            SkinnedMeshRequestsV2 registry;std::set<std::string> aliases;
            for(const auto policy:order) {
                const auto wanted=OwnRequest(policy);const auto alias=registry.Register(wanted);SkinnedMeshRequestV2 got;
                Need(aliases.insert(alias).second&&registry.Find(alias,got)&&SameSkinnedMeshRequestV2(got,wanted),
                    "LOD profiles aliased or registry lost full immutable key");
            }
        }
    }
    std::cout<<"WOLF2009_ROOT_LOD_PROFILE_SYNTHETIC_OK checks="<<checks<<" poses="<<poses<<" corners="<<corners
        <<" max_position_error="<<maximumPositionError<<" native_runtime=0\n";
}
struct SourceFiles {
    std::map<std::string,std::string> files;
    StreamMounts mounts;
    explicit SourceFiles(const std::string& base):mounts([&](const std::string& label) {
        const auto found=files.find(label);if(found==files.end())return std::unique_ptr<std::istream>();
        return std::unique_ptr<std::istream>(new std::ifstream(found->second,std::ios::binary));
    }) {
        files["mp40detail"]=base+"/streampacks/mp40.spk";
        files["mp40global"]=base+"/maps/game/global/global.spk";
        mounts.Mount("mp40detail");mounts.Mount("mp40global");
    }
    Bytes Read(const std::string& path) {
        const auto wanted=ParseResourcePath(path);const auto found=mounts.Find(wanted.type,wanted.name);
        const StreamResource* match=nullptr;
        for(const auto& item:found.matches)if(item.mountId==wanted.mount) {
            Need(!match,"duplicate exact typed original resource");match=&item;
        }
        Need(match!=nullptr,"selected exact original resource absent");return mounts.Read(*match);
    }
};
void Source(const std::string& base) {
    SourceFiles files(base);
    const std::string rigName="models/weapons/mp40/mp40_skeleton.hkx";
    auto request=MakeSkinnedMeshRequestV2("wolf2009/mp40detail/modl/models/weapons/mp40/mp40_view",
        "wolf2009/mp40global/skel/maps/game/global/global",rigName,0,
        SkinnedVertexProfile::WeightedOrRigid64,SkinnedLodPolicy::RootAuthoredMeshUnspecifiedSlotV1);
    // Portable logical lease-context IDs only. Actual engine leases are a
    // separate native test; no live mount/epoch state is claimed here.
    request.sourceEpochs={{41,42}};
    const ResourceReader read=[&](const std::string& path) {return files.Read(path);};
    const auto prepared=LoadSkinnedMeshV2(request,read);
    const auto model=read(request.model),skel=read(request.skeletonContainer);
    const std::string animationPath="wolf2009/mp40global/hkxa/maps/game/global/global";
    const auto hkxa=read(animationPath);const auto topology=Topology(prepared.skeleton);
    const auto source=DecodeWeightedModel1(model,"models/weapons/mp40/mp40_view",topology,0,
        request.geometryProfile,request.lodPolicy);
    Need(prepared.joints.size()==49&&prepared.surfaces.size()==7&&prepared.sourceLodDistances==std::vector<double>{200}&&
        prepared.sourceMeshLods.size()==7&&prepared.sourceHasLodRecords&&prepared.selectionUsesCompatibilitySlot,
        "actual MP40 view topology/root LOD metadata differs");
    for(unsigned mesh=0;mesh<7;++mesh)
        Need(prepared.sourceMeshLods[mesh].hasLevelOfDetail==(mesh>=2)&&prepared.sourceMeshLods[mesh].levelOfDetail==0,
            "actual MP40 per-mesh LOD facts differ");
    GreeterPose bind;bind.model=source.bindModelJoints;ComparePose(prepared,source,bind);
    std::size_t vertices=0,triangles=0;
    for(const auto& surface:prepared.surfaces) {vertices+=surface.geometry.vertices.size();triangles+=surface.geometry.indices.size()/3;}
    for(const auto* logical:{"idle","fire","fire2","fire3","fire4","reload"}) {
        const std::string clipName=std::string("models/weapons/mp40/mp40_")+logical+".hkx";
        SourceRigRequest rigRequest;rigRequest.clipName=clipName;rigRequest.skeletonName=rigName;
        rigRequest.policy=SourceRigPolicy::CallerIdentityFullLocalPoseV1;
        const auto animation=SourceRigAnimation::Load(hkxa,skel,rigRequest);
        Need(animation.Skeleton().joints.size()==prepared.joints.size(),"source clip/model joint counts differ");
        for(std::size_t j=0;j<prepared.joints.size();++j)
            Need(animation.Skeleton().joints[j].name==prepared.joints[j].name&&animation.Skeleton().joints[j].parent==prepared.joints[j].parent,
                "source clip/model identity or hierarchy differs");
        for(double seconds:{0.,.017,animation.Duration()/2,animation.Duration()})
            ComparePose(prepared,source,animation.SamplePose(seconds));
        ComparePose(prepared,source,animation.SamplePoseFrame(.5));
        ComparePose(prepared,source,animation.SamplePoseFrame(double(animation.FrameCount()-1)));
        const auto clip=SplineClipData::Load(hkxa,clipName);wire::Envelope envelope;auto& b=envelope.binding;
        b.geometryProfile=wire::GeometryProfile::WeightedOrRigid64;b.lodPolicy=wire::LodPolicy::RootAuthoredMeshUnspecifiedSlotV1;
        b.paths={{request.model,request.skeletonContainer,animationPath,request.skeletonContainer}};
        b.modelAlias=SkinnedMeshCacheNameV2(request);b.meshSkeletonName=rigName;b.animationSkeletonName=rigName;b.clipName=clipName;
        b.bones=unsigned(prepared.joints.size());b.frames=unsigned(animation.FrameCount());b.blocks=unsigned(clip.input.blockOffsets.size());
        b.maxFramesPerBlock=clip.input.maxFramesPerBlock;b.duration=clip.input.duration;b.frameDuration=clip.input.frameDuration;
        b.blockDuration=clip.input.blockDuration;b.blockInverseDuration=clip.input.blockInverseDuration;
        const std::array<const Bytes*,4> sources{{&model,&skel,&hkxa,&skel}};
        for(unsigned role=0;role<4;++role) {
            b.sources[role].epoch=role==0?41:42;b.sources[role].bytes=sources[role]->size();
            b.sources[role].fingerprint=wire::Fingerprint(sources[role]->data(),sources[role]->size());
        }
        envelope.animation=hkxa;envelope.skeleton=skel;
        const auto decoded=wire::Parse(wire::Serialize(envelope));
        Need(wire::Same(b,decoded.binding)&&decoded.animation==hkxa&&decoded.skeleton==skel,
            "source MP40 four-role descriptor did not preserve metadata/payloads");
        std::cout<<"WOLF2009_MP40_SOURCE_POSE_ELIGIBLE logical="<<logical<<" bones="<<animation.Skeleton().joints.size()
            <<" frames="<<animation.FrameCount()<<" duration="<<animation.Duration()<<" lod_policy=3 actor_commands_qualified=0 native_runtime=0\n";
    }
    std::cout<<"WOLF2009_MP40_ROOT_LOD_SOURCE_OK checks="<<checks<<" poses="<<poses<<" corners="<<corners
        <<" joints="<<prepared.joints.size()<<" surfaces="<<prepared.surfaces.size()<<" vertices="<<vertices<<" triangles="<<triangles
        <<" explicit_mesh_lod=5 absent_mesh_lod=2 max_position_error="<<maximumPositionError
        <<" native_runtime=0 gpu=0 retail_missing_mesh_semantics=0\n";
}
}
int main(int argc,char** argv) {
    try {
        std::cout<<std::setprecision(17);
        if(argc==1)Synthetic();
        else if(argc==3&&std::string(argv[1])=="--source-base")Source(argv[2]);
        else throw std::runtime_error("usage [--source-base owner/SP/base]");
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"WOLF2009_ROOT_LOD_PROFILE_FAIL checks="<<checks<<" poses="<<poses<<" message="<<error.what()<<'\n';return 1;
    }
}
