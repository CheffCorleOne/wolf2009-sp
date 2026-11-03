// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinnedModelData.h"
#include "StreamMounts.h"
#include "MeshV2Fixtures.h"
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
namespace {
using namespace wolf2009;using namespace wolf2009::idtech4a;unsigned checks=0;
void Need(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
void Reject(const std::function<void()>& f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}Need(refused,"unsupported mesh v2 input accepted");}
void Bind(const PreparedSkinnedGeometry& data){std::vector<GreeterJointPose> pose;for(const auto& j:data.joints)pose.push_back(j.bindModel);
    for(std::size_t s=0;s<data.surfaces.size();++s){const auto actual=SkinPreparedSurface(data,s,pose);for(std::size_t v=0;v<actual.size();++v){const auto& wanted=data.surfaces[s].geometry.vertices[v];
        for(unsigned a=0;a<3;++a)Need(std::abs(actual[v][a]-(a==0?wanted.x:a==1?wanted.y:wanted.z))<0.0002,"mesh v2 own MODL bind reconstruction differs");}}}
SkinnedMeshRequestV2 Request(const char* name="rig-A",SkinnedVertexProfile p=SkinnedVertexProfile::Weighted64,SkinnedLodPolicy policy=SkinnedLodPolicy::Authored){return MakeSkinnedMeshRequestV2("wolf2009/own/modl/models/own/body","wolf2009/skel/skel/maps/own/skel",name,0,p,policy);}
void LodPolicies(){
    Need(SkinnedMeshCacheNameV2(Request())=="wolf2009/own/md5r/v2/lod0/6a1c4bd19469f0ce/models/own/body","published authored v2 cache domain changed");
    for(unsigned n:{3u,7u})for(bool rigid:{false,true}){
        meshv2fixtures::Skeletons fixture(n);const auto skeleton=fixture.pack.Finish();
        auto model=meshv2fixtures::Model(n,rigid);unsigned reads=0;
        const ResourceReader read=[&](const std::string& p){++reads;return ParseResourcePath(p).type=="modl"?model:skeleton;};
        const auto profile=rigid?SkinnedVertexProfile::WeightedOrRigid64:SkinnedVertexProfile::Weighted64;
        const auto authored=Request("rig-A",profile),single=Request("rig-A",profile,SkinnedLodPolicy::SingleResourceWithoutLodRecords);
        const auto a=LoadSkinnedMeshV2(authored,read);Bind(a);
        Need(a.lodPolicy==SkinnedLodPolicy::Authored&&a.sourceHasLodRecords,"authored source LOD facts lost");
        model=meshv2fixtures::Model(n,rigid,false,false);
        const auto b=LoadSkinnedMeshV2(single,read);Bind(b);
        Need(!b.sourceHasLodRecords&&b.lodPolicy==SkinnedLodPolicy::SingleResourceWithoutLodRecords&&b.request.lodPolicy==single.lodPolicy&&b.levelOfDetail==0&&b.otherLodMeshes==0,"no-record source incorrectly claims authored LOD0");
        Need(b.surfaces.size()==a.surfaces.size()&&b.surfaces[0].geometry.indices==a.surfaces[0].geometry.indices,"LOD policy changed draw topology");
        for(std::size_t i=0;i<b.surfaces[0].geometry.vertices.size();++i){const auto& x=b.surfaces[0].geometry.vertices[i];const auto& y=a.surfaces[0].geometry.vertices[i];
            Need(x.x==y.x&&x.y==y.y&&x.z==y.z&&x.u==y.u&&x.v==y.v&&x.diffuseColor==y.diffuseColor,"no-record policy altered source position/UV/RGBA");}
        Reject([&]{LoadSkinnedMeshV2(authored,read);});
        std::vector<GreeterJoint> topology;for(const auto& j:b.joints){GreeterJoint joint;joint.name=j.name;joint.parent=j.parent;topology.push_back(joint);}
        Reject([&]{DecodeWeightedModel1(model,"models/own/body",topology,0,profile);});
        Reject([&]{DecodeWeightedModel1(model,"models/own/body",topology,1,profile,SkinnedLodPolicy::SingleResourceWithoutLodRecords);});
        for(unsigned badPolicy:{0u,3u,255u}){auto bad=single;bad.lodPolicy=static_cast<SkinnedLodPolicy>(badPolicy);reads=0;Reject([&]{LoadSkinnedMeshV2(bad,read);});Need(reads==0,"unknown LOD policy reached source provider");}
        auto bad=single;bad.levelOfDetail=1;reads=0;Reject([&]{LoadSkinnedMeshV2(bad,read);});Need(reads==0,"no-record nonzero slot reached source provider");
        for(unsigned mode=0;mode<4;++mode){
            // Root-only, mesh-only, absent root with mixed mesh records, and
            // authored root with a missing second mesh field all fail closed.
            model=meshv2fixtures::Model(n,rigid,mode==0||mode==3,mode==1||mode==3,mode>=2);
            Reject([&]{LoadSkinnedMeshV2(single,read);});Reject([&]{LoadSkinnedMeshV2(authored,read);});
        }
        model=meshv2fixtures::Model(n,rigid);Reject([&]{LoadSkinnedMeshV2(single,read);});
        const auto clean=meshv2fixtures::Model(n,rigid,false,false);
        const std::string marker="bin 192\n";const auto payload=std::search(clean.begin(),clean.end(),marker.begin(),marker.end());Need(payload!=clean.end(),"authored negative fixture payload missing");
        const auto offset=std::size_t(payload-clean.begin())+marker.size();
        for(unsigned fault=0;fault<4;++fault){model=clean;if(fault==0)meshv2fixtures::Put(model,offset,0x7fc00001);if(fault==1)meshv2fixtures::Put(model,offset+56,0x7fc00001);if(fault==2)model[offset+12]=255;
            if(fault==3){const std::string indices="bin 6\n";const auto at=std::search(model.begin(),model.end(),indices.begin(),indices.end());Need(at!=model.end(),"authored negative index payload missing");model[std::size_t(at-model.begin())+indices.size()]=3;}
            Reject([&]{LoadSkinnedMeshV2(single,read);});}
        Need(!SameSkinnedMeshRequestV2(authored,single)&&SkinnedMeshCacheNameV2(authored)!=SkinnedMeshCacheNameV2(single),"explicit LOD policy collapsed full request/cache identity");
        SkinnedMeshRequestsV2 registry;const auto alias=registry.Register(single);SkinnedMeshRequestV2 got;Need(registry.Find(alias,got)&&SameSkinnedMeshRequestV2(got,single)&&got.lodPolicy==single.lodPolicy,"registry lost explicit LOD policy");
    }
}
void Synthetic(){
    const auto legacy=MakeSkinnedModelRequest("wolf2009/own/modl/models/own/body","wolf2009/own/hkxa/maps/own/clip","wolf2009/skel/skel/maps/own/skel",0);
    Need(SkinnedModelCacheName(legacy)=="wolf2009/own/md5r/lod0/62681613f7503a87/models/own/body","published v1 cache alias changed");
    for(unsigned n:{3u,7u})for(bool rigid:{false,true}){
        meshv2fixtures::Skeletons fixture(n);const auto skel=fixture.pack.Finish(),model=meshv2fixtures::Model(n,rigid);unsigned reads=0;
        const ResourceReader reader=[&](const std::string& p){++reads;if(ParseResourcePath(p).type=="modl")return model;if(ParseResourcePath(p).type=="skel")return skel;throw std::runtime_error("HKXA read trap");};
        const auto profile=rigid?SkinnedVertexProfile::WeightedOrRigid64:SkinnedVertexProfile::Weighted64;
        const auto a=LoadSkinnedMeshV2(Request("rig-A",profile),reader),b=LoadSkinnedMeshV2(Request("rig-B",profile),reader);
        Need(reads==4&&a.joints.size()==n&&a.surfaces.size()==1&&a.surfaces[0].geometry.indices.size()==3,"generic3/7 geometry source/load count differs");Bind(a);Bind(b);
        Need(a.skeleton.referencePoseQualified&&a.skeleton.joints[0].rawReference[3]==0x89abcdef&&a.skeleton.joints[0].rawBoneWord==0x12345678,"named skeleton raw lanes/bone metadata lost");
        Need(a.skeleton.joints[0].referenceLocal.translation[0]!=b.skeleton.joints[0].referenceLocal.translation[0]&&a.joints[0].bindModel.translation==b.joints[0].bindModel.translation,"SKEL reference contaminated own MODL bind");
        Need(a.surfaces[0].geometry.vertices[0].diffuseColor==0x11223344&&a.surfaces[0].geometry.vertices[0].u==.25f&&a.surfaces[0].weights[0][0].weight==(rigid?1:.25f),"generic profile lost colors/UV/weights");
        Reject([&]{LoadSkinnedMeshV2(Request("missing",profile),reader);});Reject([&]{LoadSkinnedMeshV2(Request("rig-a",profile),reader);});
        if(rigid)Reject([&]{LoadSkinnedMeshV2(Request(),reader);});
        auto bad=fixture;bad.pack.data[bad.parents+2]=1;Reject([&]{ReadSkeletonData(bad.pack.Finish(),"rig-A");});
        bad=fixture;meshv2fixtures::Put(bad.pack.data,bad.reference+32,0x40000000);const auto unknown=ReadSkeletonData(bad.pack.Finish(),"rig-A");Need(!unknown.referencePoseQualified&&unknown.joints[0].rawReference[8]==0x40000000,"unknown reference scale was not retained independently");
        const auto unknownBytes=bad.pack.Finish();const auto topologyOnly=LoadSkinnedMeshV2(Request("rig-A",profile),[&](const std::string& p){return ParseResourcePath(p).type=="modl"?model:unknownBytes;});Bind(topologyOnly);
        SkeletonDataLimits limit;limit.joints=n-1;Reject([&]{ReadSkeletonData(skel,"rig-A",limit);});
        for(unsigned fault=0;fault<8;++fault){auto broken=fixture;
            if(fault==0)meshv2fixtures::Put(broken.pack.data,broken.object+16,n+1);
            if(fault==1)broken.pack.links.erase(broken.object+4);
            if(fault==2)broken.pack.data[broken.parents]=0;
            if(fault==3){const auto bone0=broken.pack.links.at(broken.boneArray),bone1=broken.pack.links.at(broken.boneArray+4);broken.pack.Link(bone1,broken.pack.links.at(bone0));}
            if(fault==4)broken.pack.types[broken.pack.links.at(broken.boneArray)]="unknownBone";
            if(fault==5){const auto container=broken.pack.links.at(broken.pack.links.at(0)+8),array=broken.pack.links.at(container);broken.pack.Link(broken.pack.links.at(array+4),broken.pack.String("rig-A"));}
            auto bytes=broken.pack.Finish();if(fault==6)bytes.pop_back();if(fault==7)bytes.resize(207);Reject([&]{ReadSkeletonData(bytes,"rig-A");});}
        limit=SkeletonDataLimits();limit.nameBytes=4;Reject([&]{ReadSkeletonData(skel,"rig-A",limit);});
        bad=fixture;meshv2fixtures::Put(bad.pack.data,bad.reference+16,0x7fc00001);const auto unknownRotation=ReadSkeletonData(bad.pack.Finish(),"rig-A");
        Need(!unknownRotation.referencePoseQualified&&!unknownRotation.joints[0].referencePoseQualified&&unknownRotation.joints[0].rawReference[4]==0x7fc00001,"nonfinite reference word was hidden or qualified");
    }
    const auto a=Request();SkinnedMeshRequestsV2 registry;const auto alias=registry.Register(a);Need(alias==registry.Register(a)&&alias!=SkinnedModelCacheName(legacy)&&alias.find("/md5r/v2/lod0/")!=std::string::npos,"mesh v2 namespace/idempotence failed");
    for(unsigned field=0;field<8;++field){auto b=a;if(field==0)b.model+="-other";if(field==1)b.skeletonContainer+="-other";if(field==2)b.skeletonName="rig-B";if(field==3)b.skeletonName="rig-a";if(field==4)b.levelOfDetail=1;if(field==5)b.geometryProfile=SkinnedVertexProfile::WeightedOrRigid64;if(field==6)++b.sourceEpochs[0];if(field==7)++b.sourceEpochs[1];Need(alias!=registry.Register(b)&&!SameSkinnedMeshRequestV2(a,b),"mesh v2 immutable key omits a declared field");}
    Reject([&]{auto b=a;b.geometryProfile=static_cast<SkinnedVertexProfile>(0);SkinnedMeshCacheNameV2(b);});
    Reject([&]{auto b=a;b.skeletonContainer="wolf2009/own/skel/maps/own/skel";b.sourceEpochs={{1,2}};SkinnedMeshCacheNameV2(b);});
    LodPolicies();
    std::cout<<"WOLF2009_SKINNED_MESH_V2_SYNTHETIC_OK checks="<<checks<<" joint_profiles=3,7 lod_policies=1,2 hkxa_reads=0 native_runtime=0\n";
}
void Source(const std::string& base){
    const std::map<std::string,std::string> packages{{"main","maps/game/trainyard/trainyard.mpk"},{"global","maps/game/global/global.spk"},{"character","streampacks/allies_kreisau_agt02.spk"}};
    StreamMounts mounts([&](const std::string& label){return std::unique_ptr<std::istream>(new std::ifstream(base+"/"+packages.at(label),std::ios::binary));});for(const auto& p:packages)mounts.Mount(p.first);
    unsigned hkxa=0;const ResourceReader read=[&](const std::string& name){const auto p=ParseResourcePath(name);if(p.type=="hkxa"){++hkxa;throw std::runtime_error("source HKXA read trap");}
        std::vector<StreamResource> found;for(const auto& x:mounts.Find(p.type,p.name).matches)if(x.mountId==p.mount)found.push_back(x);
        if(p.type=="modl"&&p.name.size()>5&&p.name.substr(p.name.size()-5)==".md5r")for(const auto& x:mounts.Find(p.type,p.name.substr(0,p.name.size()-5)).matches)if(x.mountId==p.mount)found.push_back(x);
        Need(found.size()==1,"selected source mesh/skeleton absent or ambiguous");return mounts.Read(found.front());};
    const std::string body="models/characters/civilians/civ_male_04/anims/body_cm04.md5r",global="wolf2009/global/skel/maps/game/global/global";
    const auto humanoid=LoadSkinnedMeshV2(MakeSkinnedMeshRequestV2("wolf2009/main/modl/"+body,global,"models/characters/_humanoid_male/humanoid_male.hkx",0,SkinnedVertexProfile::Weighted64),read);
    const auto empty=LoadSkinnedMeshV2(MakeSkinnedMeshRequestV2("wolf2009/main/modl/"+body,global,"models/characters/_humanoid_male/empty_male_skel.hkx",0,SkinnedVertexProfile::Weighted64),read);
    Bind(humanoid);Bind(empty);std::size_t rawDifferences=0;
    Need(humanoid.joints.size()==52&&empty.joints.size()==52,"actual named body skeleton counts differ");
    for(unsigned j=0;j<52;++j){Need(humanoid.joints[j].name==empty.joints[j].name&&humanoid.joints[j].parent==empty.joints[j].parent&&humanoid.joints[j].bindModel.translation==empty.joints[j].bindModel.translation&&humanoid.joints[j].bindModel.rotation==empty.joints[j].bindModel.rotation,"same MODL default bind differs for topology-identical named SKELs");for(unsigned c=0;c<12;++c)rawDifferences+=humanoid.skeleton.joints[j].rawReference[c]!=empty.skeleton.joints[j].rawReference[c];}
    Need(rawDifferences>0&&SkinnedMeshCacheNameV2(humanoid.request)!=SkinnedMeshCacheNameV2(empty.request),"different named reference profiles collapsed to one mesh identity");
    for(unsigned lod:{0u,1u}){const auto detail=LoadSkinnedMeshV2(MakeSkinnedMeshRequestV2("wolf2009/character/modl/"+body,global,"models/characters/_humanoid_male/empty_male_skel.hkx",lod,SkinnedVertexProfile::Weighted64),read);Bind(detail);std::cout<<"WOLF2009_MESH_V2_SOURCE role=detail lod="<<lod<<" joints="<<detail.joints.size()<<" vertices="<<detail.surfaces[0].geometry.vertices.size()<<" triangles="<<detail.surfaces[0].geometry.indices.size()/3<<" hkxa_reads="<<hkxa<<'\n';}
    const auto head=LoadSkinnedMeshV2(MakeSkinnedMeshRequestV2("wolf2009/main/modl/models/characters/civilians/head_male_03/anims/head_civilian_male_03_mesh.md5r","wolf2009/main/skel/maps/game/trainyard/trainyard","models/characters/civilians/head_male_03/anims/head_civilian_male_03_mesh_skel.hkx",0,SkinnedVertexProfile::WeightedOrRigid64),read);Bind(head);Need(head.joints.size()==27&&hkxa==0,"standalone head bind did not stay geometry-only");
    const auto lockerRequest=MakeSkinnedMeshRequestV2("wolf2009/main/modl/models/props/locker_foot_01_anim.md5r","wolf2009/main/skel/maps/game/trainyard/trainyard","models/props/locker_foot_01_anim_skel.hkx",0,SkinnedVertexProfile::WeightedOrRigid64,SkinnedLodPolicy::SingleResourceWithoutLodRecords);
    const auto locker=LoadSkinnedMeshV2(lockerRequest,read);Bind(locker);
    Need(locker.joints.size()==2&&locker.skeleton.joints.size()==2&&locker.joints[0].name=="origin"&&locker.joints[0].parent==-1&&locker.joints[1].name=="hinge_locker_foot_01"&&locker.joints[1].parent==0,"explicit locker MODL/SKEL topology differs");
    Need(locker.surfaces.size()==1&&locker.surfaces[0].geometry.vertices.size()==163&&locker.surfaces[0].geometry.indices.size()==336&&!locker.sourceHasLodRecords&&locker.lodPolicy==lockerRequest.lodPolicy&&hkxa==0,"explicit locker no-record geometry profile differs");
    auto wrongLockerPolicy=lockerRequest;wrongLockerPolicy.lodPolicy=SkinnedLodPolicy::Authored;Reject([&]{LoadSkinnedMeshV2(wrongLockerPolicy,read);});
    std::cout<<"WOLF2009_MESH_V2_LOCKER_SOURCE_OK joints="<<locker.joints.size()<<" vertices="<<locker.surfaces[0].geometry.vertices.size()<<" triangles="<<locker.surfaces[0].geometry.indices.size()/3<<" lod_policy=2 source_lod_records=0 hkxa_reads="<<hkxa<<" native_runtime=0\n";
    std::size_t headVertices=0,headTriangles=0;for(const auto& draw:head.surfaces){headVertices+=draw.geometry.vertices.size();headTriangles+=draw.geometry.indices.size()/3;}
    std::cout<<"WOLF2009_SKINNED_MESH_V2_SOURCE_OK checks="<<checks<<" body_joints=52 head_joints=27 reference_raw_differences="<<rawDifferences<<" head_draws="<<head.surfaces.size()<<" head_vertices="<<headVertices<<" head_triangles="<<headTriangles<<" hkxa_reads="<<hkxa<<" native_runtime=0\n";
}
}
int main(int argc,char** argv){try{if(argc==1)Synthetic();else if(argc==3&&std::string(argv[1])=="--source-base")Source(argv[2]);else throw std::runtime_error("usage [--source-base SP/base]");return 0;}catch(const std::exception& e){std::cerr<<"WOLF2009_SKINNED_MESH_V2_FAIL checks="<<checks<<" message="<<e.what()<<'\n';return 1;}}
