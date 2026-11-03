// SPDX-License-Identifier: GPL-3.0-or-later
// Actual normal model-manager/MD5 checks. No GL, clip/actor control or head attachment.
#include "EngineSkinnedModelData.h"
#include "SkinCatalog.h"
#include "ZipArchive.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "Wolf2009Render.h"
#ifdef WOLF2009_ANIMATION
#include "../../src/idtech4a_anim/EngineAnimationBinding.h"
#endif
namespace {
using namespace wolf2009;using namespace wolf2009::idtech4a;
const char* Body="models/characters/civilians/civ_male_04/anims/body_cm04.md5r";
const char* Humanoid="models/characters/_humanoid_male/humanoid_male.hkx";
const char* Empty="models/characters/_humanoid_male/empty_male_skel.hkx";
const char* Global="wolf2009/qav2global/SKEL/maps/game/global/global";
struct Stats{std::size_t checks=0,poses=0,corners=0;double positionError=0,boundsError=0;};
void Need(Stats& s,bool b,const char* m){++s.checks;if(!b)throw std::runtime_error(m);}
void Near(Stats& s,double a,double b,double eps,double& maximum,const char* m){const double d=std::abs(a-b);maximum=(std::max)(maximum,d);Need(s,std::isfinite(a)&&std::isfinite(b)&&d<=eps,m);}
Bytes FileBytes(const std::string& path){
    idFile* f=fileSystem->OpenFileRead(path.c_str(),false);if(!f)throw std::runtime_error("mesh v2 original source absent");
    struct Close{void operator()(idFile* v)const{fileSystem->CloseFile(v);}};std::unique_ptr<idFile,Close> file(f);
    const int n=f->Length();if(n<=0||n>64*1024*1024)throw std::runtime_error("mesh v2 source budget");Bytes out(static_cast<std::size_t>(n));if(f->Read(out.data(),n)!=n)throw std::runtime_error("short mesh v2 source read");return out;
}
Bytes Read(const std::string& path){
    if(ParseResourcePath(path).type=="hkxa")throw std::runtime_error("mesh v2 QA HKXA read trap");
    return FileBytes(path);
}
class Joints{ idJointMat* values;public:explicit Joints(std::size_t n):values(static_cast<idJointMat*>(Mem_Alloc16(n*sizeof(idJointMat)))){}~Joints(){Mem_Free16(values);}idJointMat* Get()const{return values;} };
struct Resources{
    std::vector<idRenderModel*> models;idRenderModel* a=NULL;idRenderModel* b=NULL;
    bool main=false,global=false,character=false,catalog=false;bool cached=r_useCachedDynamicModels.GetBool();int show=r_showSkel.GetInteger();
    Resources(){r_useCachedDynamicModels.SetBool(true);r_showSkel.SetInteger(0);}
    void DeleteSnapshots(){delete a;a=NULL;delete b;b=NULL;}
    void DeleteModels(){DeleteSnapshots();for(auto* m:models){renderModelManager->RemoveModel(m);renderModelManager->FreeModel(m);}models.clear();}
    ~Resources(){DeleteModels();if(main&&!Wolf2009_Unmount("qav2"))fprintf(stderr,"WOLF2009_MESH_V2_CLEANUP_FAIL qav2\n");if(global&&!Wolf2009_Unmount("qav2global"))fprintf(stderr,"WOLF2009_MESH_V2_CLEANUP_FAIL qav2global\n");if(character&&!Wolf2009_Unmount("qav2char"))fprintf(stderr,"WOLF2009_MESH_V2_CLEANUP_FAIL qav2char\n");if(catalog)Wolf2009_ClearRenderCatalog();r_useCachedDynamicModels.SetBool(cached);r_showSkel.SetInteger(show);}
};
bool Identity(const SkinnedSourceIdentity& a,const SkinnedSourceIdentity& b){return a.path==b.path&&a.bytes==b.bytes&&a.fingerprint==b.fingerprint;}
idRenderModelMD5* Load(Stats& stats,Resources& resources,const SkinnedMeshRequestV2& requested,idStr& alias,SkinnedMeshDataV2& data){
    Need(stats,Wolf2009_RegisterSkinnedMeshV2(requested.model.c_str(),requested.skeletonContainer.c_str(),requested.skeletonName.c_str(),int(requested.levelOfDetail),int(requested.geometryProfile),alias),"native mesh v2 registration failed");
    SkinnedMeshRequestV2 request;Need(stats,Wolf2009_GetSkinnedMeshRequestV2(alias,request),"native mesh v2 request absent");data=LoadSkinnedMeshV2(request,Read);
    auto* model=renderModelManager->FindModel(alias);
    if(model&&model!=renderModelManager->DefaultModel()&&std::find(resources.models.begin(),resources.models.end(),model)==resources.models.end())resources.models.push_back(model);
    auto* md5=dynamic_cast<idRenderModelMD5*>(model);Need(stats,md5&&model->IsLoaded()&&!model->IsDefaultModel()&&model->IsDynamicModel()==DM_CACHED&&model->NumJoints()==int(data.joints.size()),"normal mesh v2 did not become actual native MD5");
    SkinnedMeshSourceInfoV2 source;Need(stats,Wolf2009_GetSkinnedMeshSourcesV2(md5,source)&&SameSkinnedMeshRequestV2(source.request,request)&&Identity(source.model,data.modelSource)&&Identity(source.skeleton,data.skeletonSource),"mesh v2 source-info lost its complete qualified identity");
    SkinnedModelRequest oldRequest;SkinnedModelSourceInfo oldSources;Need(stats,!Wolf2009_GetSkinnedModelRequest(alias,oldRequest)&&!Wolf2009_GetSkinnedModelSources(md5,oldSources),"mesh v2 fabricated a v1 HKXA identity");return md5;
}
void Hierarchy(Stats& stats,idRenderModelMD5* model,const SkinnedMeshDataV2& data){
    const int count=model->NumJoints();Joints actual(count),expected(count);std::vector<int> parents;std::vector<GreeterJointPose> bind;
    for(int i=0;i<count;++i){const auto& joint=data.joints[std::size_t(i)];parents.push_back(joint.parent);bind.push_back(joint.bindModel);
        Need(stats,model->GetJoints()[i].name==joint.name.c_str()&&(i?model->GetJoints()[i].parent==&model->GetJoints()[joint.parent]:model->GetJoints()[i].parent==NULL),"mesh v2 native named hierarchy differs");}
    SIMDProcessor->ConvertJointQuatsToJointMats(actual.Get(),model->GetDefaultPose(),count);SIMDProcessor->TransformJoints(actual.Get(),parents.data(),1,count-1);Wolf2009_ToNativeModelJoints(bind,expected.Get(),count);
    for(int i=0;i<count;++i)for(int c=0;c<12;++c)Near(stats,actual.Get()[i].ToFloatPtr()[c],expected.Get()[i].ToFloatPtr()[c],c%4==3?0.0002:0.00002,stats.positionError,"native mesh v2 default hierarchy differs from own MODL model bind");
}
std::uint64_t Hash(const idRenderModel* model){std::uint64_t hash=UINT64_C(14695981039346656037);for(int s=0;s<model->NumSurfaces();++s){const auto* tri=model->Surface(s)->geometry;for(int v=0;v<tri->numVerts;++v){const auto* bytes=reinterpret_cast<const unsigned char*>(tri->verts[v].xyz.ToFloatPtr());for(unsigned b=0;b<3*sizeof(float);++b)hash=(hash^bytes[b])*UINT64_C(1099511628211);}}return hash;}
void Pose(Stats& stats,idRenderModelMD5* model,idRenderModel*& cached,const SkinnedMeshDataV2& data,const std::vector<GreeterJointPose>& poses,const idDeclSkin* skin=NULL){
    Joints palette(poses.size());Wolf2009_ToNativeModelJoints(poses,palette.Get(),int(poses.size()));renderEntity_t ent;memset(&ent,0,sizeof(ent));ent.hModel=model;ent.axis.Identity();ent.numJoints=int(poses.size());ent.joints=palette.Get();ent.customSkin=skin;for(int c=0;c<4;++c)ent.shaderParms[c]=1;
    Need(stats,model->Wolf2009FrameBounds(palette.Get(),ent.numJoints,ent.bounds),"mesh v2 current supplied-pose bounds failed");idRenderModel* previous=cached;cached=NULL;cached=model->InstantiateDynamicModel(&ent,NULL,previous);
    Need(stats,cached&&!cached->IsDefaultModel()&&cached->NumSurfaces()==int(data.surfaces.size())&&(!previous||previous==cached),"mesh v2 dynamic snapshot/cache failed");
    for(std::size_t surface=0;surface<data.surfaces.size();++surface){const auto& draw=data.surfaces[surface];const auto& expected=SkinPreparedSurface(data,surface,poses);const auto* actual=cached->Surface(int(surface));const auto* tri=actual->geometry;
        Need(stats,tri&&tri->numIndexes==int(draw.geometry.indices.size()),"mesh v2 native corner topology differs");const auto& material=Wolf2009_ResolveSkinnedMaterial(ParseResourcePath(data.modelPath),draw.geometry);const auto* shader=declManager->FindMaterial(material.name.c_str());
        // MD5 snapshots retain the shared base material. The renderer applies
        // ent.customSkin later; storing its effective shader here would mutate
        // the shared snapshot contract (and differ from ordinary native MD5).
        Need(stats,actual->shader==shader,"mesh v2 snapshot changed its shared base material");
        for(int corner=0;corner<tri->numIndexes;++corner){const auto index=draw.geometry.indices[std::size_t(corner)];const auto& vertex=tri->verts[tri->indexes[corner]];const auto& original=draw.geometry.vertices[index];const auto rgba=NativeDiffuseColor(material,original.diffuseColor);
            for(unsigned axis=0;axis<3;++axis)Near(stats,vertex.xyz[axis],expected[index][axis],0.0002,stats.positionError,"mesh v2 actual skinned corner differs from canonical source weights");
            for(unsigned c=0;c<4;++c)Need(stats,vertex.color[c]==rgba[c],"mesh v2 native RGBA differs");
            Need(stats,vertex.st.x==original.u*(material.supported?material.binding.uvScale[0]:1)&&vertex.st.y==original.v*(material.supported?material.binding.uvScale[1]:1),"mesh v2 native UV differs");++stats.corners;}}
    const idBounds bound=cached->Bounds();for(unsigned side=0;side<2;++side)for(unsigned axis=0;axis<3;++axis)Near(stats,bound[side][axis],ent.bounds[side][axis],0.0002,stats.boundsError,"mesh v2 snapshot bounds differ from actual posed bounds");++stats.poses;
}
std::vector<GreeterJointPose> Bind(const SkinnedMeshDataV2& data){std::vector<GreeterJointPose> out;for(const auto& j:data.joints)out.push_back(j.bindModel);return out;}
SkinnedMeshRequestV2 Request(const std::string& mount,const char* name,unsigned lod=0){return MakeSkinnedMeshRequestV2("wolf2009/"+mount+"/MODL/"+Body,Global,name,lod,SkinnedVertexProfile::Weighted64);}
void CheckAppearance(Stats& stats,const SkinnedMeshDataV2& data,const idRenderModel* a,const idRenderModel* b,const idDeclSkin* skin,const char* skinAlias){
    const Bytes bytes=FileBytes("fixtures/wolf-materials.bin");
    std::istringstream stream(std::string(bytes.begin(),bytes.end()),std::ios::in|std::ios::binary);
    const ZipArchive archive=ZipArchive::Scan(stream);const SkinCatalog skins=SkinCatalog::Read(stream,archive);
    GeometryScene names;for(const auto& draw:data.surfaces){GeometrySurface surface;surface.material=draw.geometry.material;names.surfaces.push_back(std::move(surface));}
    const auto applied=skins.Apply("resist_fighter_B",names);
    Need(stats,applied.matchedSurfaces==data.surfaces.size()&&applied.changedSurfaces==data.surfaces.size(),"authored mesh v2 appearance did not remap each source draw");
    const auto catalog=ReadMaterialCatalog(bytes);const auto source=ParseResourcePath(data.modelPath);
    for(std::size_t s=0;s<data.surfaces.size();++s){
        RenderSurface target=data.surfaces[s].geometry;target.material=names.surfaces[s].material;
        const auto expected=BuildDiffuseMaterial(source,target,catalog.get());
        const idMaterial* base=a->Surface(int(s))->shader;const idMaterial* stored=b->Surface(int(s))->shader;
        const idMaterial* effective=R_RemapShaderBySkin(stored,skin,NULL);
        Need(stats,stored==base&&R_RemapShaderBySkin(base,NULL,NULL)==base&&effective&&effective!=base&&
             skin->RemapShaderBySkin(base)==effective&&effective->GetState()!=DS_DEFAULTED&&effective->Coverage()==MC_OPAQUE&&
             std::string(effective->GetName()).find(std::string(skinAlias)+"/material/")==0&&expected.supported,
             "actual native per-entity remap differs from immutable authored appearance selection");
        int sourceStages=0;
        for(int stage=0;stage<effective->GetNumStages();++stage){
            const auto* value=effective->GetStage(stage);const auto* image=value->texture.image;
            if(!image||!HasResourceType(image->imgName.c_str(),"TXTR"))continue;
            Need(stats,image->imgName.Icmp(expected.image.c_str())==0&&ParseResourcePath(image->imgName.c_str()).mount=="qav2char"&&
                 value->vertexColor==(expected.nativeVertexColor?SVC_MODULATE:SVC_IGNORE),
                 "mesh v2 effective diffuse stage differs from source-catalog material oracle");++sourceStages;
        }
        Need(stats,sourceStages>0,"mesh v2 appearance contains no qualified original diffuse stage");
    }
}
}
bool Wolf2009_RunSkinnedMeshV2Checks(){
    Stats stats;try{
        Resources resources;Need(stats,Wolf2009_Mount("qav2","maps/game/trainyard/trainyard.mpk"),"mesh v2 source mount failed");resources.main=true;Need(stats,Wolf2009_Mount("qav2global","maps/game/global/global.spk"),"mesh v2 global mount failed");resources.global=true;Need(stats,Wolf2009_Mount("qav2char","streampacks/allies_kreisau_agt02.spk"),"mesh v2 detail mount failed");resources.character=true;
        Need(stats,Wolf2009_LoadMaterialCatalog("fixtures/wolf-materials.bin"),"mesh v2 original material catalog absent");resources.catalog=true;
        idStr aliasHum,aliasEmpty;SkinnedMeshDataV2 humanoid,empty;auto* hum=Load(stats,resources,Request("qav2",Humanoid),aliasHum,humanoid);auto* emp=Load(stats,resources,Request("qav2",Empty),aliasEmpty,empty);Hierarchy(stats,hum,humanoid);Hierarchy(stats,emp,empty);
        Need(stats,aliasHum!=aliasEmpty&&humanoid.joints.size()==52&&empty.joints.size()==52,"named mesh v2 identities collapsed");std::size_t refDiff=0;
#ifdef WOLF2009_ANIMATION
        idStr clipAlias;Need(stats,!Wolf2009_RegisterSourceAnimation(aliasHum,clipAlias)&&clipAlias.IsEmpty(),"v1 source animation producer accepted mesh v2 without an explicit clip binding");
#endif
        for(unsigned j=0;j<52;++j){for(unsigned c=0;c<12;++c)refDiff+=humanoid.skeleton.joints[j].rawReference[c]!=empty.skeleton.joints[j].rawReference[c];
            Need(stats,hum->GetJoints()[j].name==emp->GetJoints()[j].name&&memcmp(hum->GetDefaultPose()[j].q.ToFloatPtr(),emp->GetDefaultPose()[j].q.ToFloatPtr(),4*sizeof(float))==0&&memcmp(hum->GetDefaultPose()[j].t.ToFloatPtr(),emp->GetDefaultPose()[j].t.ToFloatPtr(),3*sizeof(float))==0,"reference-different SKEL changed same MODL native bind pose");}
        Need(stats,refDiff>0,"named original reference arrays did not differ");
        Pose(stats,hum,resources.a,humanoid,Bind(humanoid));Pose(stats,emp,resources.b,empty,Bind(empty));const auto independent=Hash(resources.b);
        auto moved=Bind(humanoid);for(auto& j:moved)j.translation[0]+=3.25;Pose(stats,hum,resources.a,humanoid,moved);Need(stats,resources.a!=resources.b&&Hash(resources.b)==independent,"mesh v2 instances shared mutable dynamic snapshots");
        for(unsigned lod:{0u,1u}){resources.DeleteSnapshots();idStr alias;SkinnedMeshDataV2 data;auto* model=Load(stats,resources,Request("qav2char",Empty,lod),alias,data);Hierarchy(stats,model,data);Pose(stats,model,resources.a,data,Bind(data));
            if(lod==0){const auto original=Hash(resources.a);idStr skinName;Need(stats,Wolf2009_RegisterSkinnedAppearance(alias,"resist_fighter_B","fixtures/wolf-materials.bin",skinName),"shared production appearance preflight failed for mesh v2");
                const idDeclSkin* skin=declManager->FindSkin(skinName,false);Need(stats,skin&&skin->GetState()!=DS_DEFAULTED,"mesh v2 authored skin defaulted");Pose(stats,model,resources.b,data,Bind(data),skin);
                Need(stats,resources.a!=resources.b&&Hash(resources.a)==original&&Hash(resources.b)==original,"mesh v2 appearance changed the independent shared geometry snapshot");
                CheckAppearance(stats,data,resources.a,resources.b,skin,skinName.c_str());}}
        resources.DeleteSnapshots();const auto headReq=MakeSkinnedMeshRequestV2("wolf2009/qav2/MODL/models/characters/civilians/head_male_03/anims/head_civilian_male_03_mesh.md5r","wolf2009/qav2/SKEL/maps/game/trainyard/trainyard","models/characters/civilians/head_male_03/anims/head_civilian_male_03_mesh_skel.hkx",0,SkinnedVertexProfile::WeightedOrRigid64);
        idStr headAlias;SkinnedMeshDataV2 head;auto* headModel=Load(stats,resources,headReq,headAlias,head);Need(stats,head.joints.size()==27,"standalone head27 geometry count differs");Hierarchy(stats,headModel,head);Pose(stats,headModel,resources.a,head,Bind(head));
        auto headMoved=Bind(head);for(auto& j:headMoved)j.translation[2]+=1.5;Pose(stats,headModel,resources.a,head,headMoved);
        resources.DeleteSnapshots();hum->PurgeModel();Need(stats,!hum->IsLoaded(),"mesh v2 purge did not unload model");hum->LoadModel();Need(stats,hum->IsLoaded()&&!hum->IsDefaultModel(),"mesh v2 same-epoch reload failed");Pose(stats,hum,resources.a,humanoid,Bind(humanoid));
        Need(stats,!Wolf2009_Unmount("qav2")&&!Wolf2009_Unmount("qav2global")&&!Wolf2009_Unmount("qav2char"),"mesh v2 resident models lost source leases");
        resources.DeleteModels();Need(stats,Wolf2009_Unmount("qav2")&&Wolf2009_Unmount("qav2global")&&Wolf2009_Unmount("qav2char"),"mesh v2 model destruction leaked pins");resources.main=resources.global=resources.character=false;
        Need(stats,Wolf2009_Mount("qav2","maps/game/trainyard/trainyard.mpk"),"mesh v2 source remount failed");resources.main=true;Need(stats,Wolf2009_Mount("qav2global","maps/game/global/global.spk"),"mesh v2 skeleton remount failed");resources.global=true;
        auto* stale=renderModelManager->FindModel(aliasHum);if(stale&&stale!=renderModelManager->DefaultModel())resources.models.push_back(stale);Need(stats,!stale||stale->IsDefaultModel(),"mesh v2 stale epoch alias silently rebound");
        idStr freshAlias;SkinnedMeshDataV2 fresh;auto* freshModel=Load(stats,resources,Request("qav2",Humanoid),freshAlias,fresh);Need(stats,freshAlias!=aliasHum,"mesh v2 remount reused old alias");Pose(stats,freshModel,resources.a,fresh,Bind(fresh));resources.DeleteModels();
        Need(stats,Wolf2009_Unmount("qav2")&&Wolf2009_Unmount("qav2global"),"mesh v2 final model lifetime leaked pins");resources.main=resources.global=false;
        printf("WOLF2009_NATIVE_SKINNED_MESH_V2_QA_OK checks=%zu poses=%zu corners=%zu body_joints=52 head_joints=27 reference_raw_differences=%zu max_position_error=%.9g max_bounds_error=%.9g hkxa_reads_by_oracle=0 same_epoch_reload=1 stale_epoch_refused=1 leases_released=1 head_attachment=0 gpu=0\n",stats.checks,stats.poses,stats.corners,refDiff,stats.positionError,stats.boundsError);return true;
    }catch(const std::exception& e){fprintf(stderr,"WOLF2009_NATIVE_SKINNED_MESH_V2_FAIL checks=%zu poses=%zu message=%s\n",stats.checks,stats.poses,e.what());}catch(...){fprintf(stderr,"WOLF2009_NATIVE_SKINNED_MESH_V2_FAIL checks=%zu message=native-exception\n",stats.checks);}return false;
}
