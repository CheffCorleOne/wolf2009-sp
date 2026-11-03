// SPDX-License-Identifier: GPL-3.0-or-later
// Optional host integration audit; arguments are user-owned assets/map/global/voice.
#include "TrainStopScene.h"
#include "SceneAssembly.h"
#include "TextureBinding.h"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <cmath>
namespace {
void Need(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct Location {std::size_t chunk,entry;};
int Audit(char** args) {
    std::unique_ptr<wolf2009::TrainStopScene> runtime;
    wolf2009::GeometryScene scene;
    wolf2009android::TexturePlan plan;
    std::size_t firstActor=0,firstDoor=0;
    {
        std::ifstream assets(args[1],std::ios::binary),map(args[2],std::ios::binary),global(args[3],std::ios::binary);
        Need(bool(assets)&&bool(map)&&bool(global),"Missing input package");
        const auto zip=wolf2009::ZipArchive::Scan(assets);
        const auto materials=wolf2009android::ReadMaterialCatalog(assets,zip);
        const auto skins=wolf2009::SkinCatalog::Read(assets,zip);
        const auto archive=wolf2009::Archive::Scan(map),globalArchive=wolf2009::Archive::Scan(global);
        const std::string voiceName="streampacks/scripted_voice_kc1_trn_vo_english.spk",voiceFile(args[4]);
        wolf2009::StreamMounts mounts([&](const std::string& name){Need(name==voiceName,"Wrong voice association");return std::unique_ptr<std::istream>(new std::ifstream(voiceFile,std::ios::binary));});mounts.Mount(voiceName);
        const wolf2009::TrainStopSources sources={map,archive,assets,zip,global,globalArchive,mounts,skins};
        wolf2009::WagonSceneOptions options;options.geometry.vertices=options.geometry.triangles=2000000;options.geometry.totalBufferBytes=256u<<20;
        runtime=wolf2009::TrainStopScene::Load(sources,options);
        std::map<std::string,Location> buffers,textures;Location proc={0,0};std::string resource;unsigned count=0;
        for(std::size_t c=0;c<archive.Chunks().size();++c)for(std::size_t e=0;e<archive.Chunks()[c].entries.size();++e){const auto& chunk=archive.Chunks()[c];const auto& entry=chunk.entries[e];Location loc={c,e};if(chunk.type=="PROC"){++count;proc=loc;resource=entry.name;}else if(chunk.type=="PBBF")Need(buffers.insert({entry.name,loc}).second,"Ambiguous PBBF");else if(chunk.type=="TXTR")Need(textures.insert({wolf2009::MaterialCatalog::NormalizeVirtualPath(entry.name),loc}).second,"Ambiguous TXTR");}
        Need(count==1,"Expected one PROC");
        scene=wolf2009::DecodeProc7(archive.ReadEntry(map,proc.chunk,proc.entry),resource,[&](const std::string& name){auto it=buffers.find(name);Need(it!=buffers.end(),"Missing PBBF");return archive.ReadEntry(map,it->second.chunk,it->second.entry);},options.geometry);
        firstActor=wolf2009android::AppendPlacedActor(scene,runtime->Wagon().PlacedGeometry(),options.geometry);
        for(const auto& prop:runtime->Wagon().ArrivalInstances())wolf2009android::AppendPlacedActor(scene,prop.geometry,options.geometry);
        firstDoor=wolf2009android::AppendPlacedActor(scene,runtime->DoorBindGeometry(),options.geometry);
        plan=wolf2009android::BindTextures(scene,materials.get(),[&](const std::string& name){auto it=textures.find(name);return it==textures.end()?std::vector<unsigned char>():archive.ReadEntry(map,it->second.chunk,it->second.entry);},wolf2009android::TextureLimits(),firstActor);
        Need(firstDoor+1==scene.surfaces.size()&&plan.surfaces[firstDoor].texture>=0,"Door mesh not appended/textured");
    } // All input streams, catalogs and mounts destroyed; runtime must own data.
    const auto* doorVertices=runtime->DoorBindGeometry().vertices.data();
    const auto* combinedVertices=scene.vertices.data();const auto initialMatrix=runtime->DoorWorldMatrix();
    Need(!runtime->State().actorStarted&&!runtime->State().doorActivated&&runtime->Wagon().State().visibleArrivalInstances==0,"Initial state is already activated");
    for(unsigned frame=0;frame<1500;++frame)runtime->Advance(1./60);
    Need(runtime->State().voiceReady&&runtime->State().actorStarted&&runtime->State().doorActivated&&runtime->ActorPose().pose.model.size()==52,"Committed arrival failed to start actual animation/door");
    Need(runtime->Wagon().State().visibleArrivalInstances==3&&runtime->Wagon().ArrivalChanges().size()==4,"Arrival visibility differs");
    Need(std::abs(runtime->State().doorSlide+59.243133545)<.001,"Wrong final door slide");
    Need(doorVertices==runtime->DoorBindGeometry().vertices.data()&&combinedVertices==scene.vertices.data(),"Frame update copied immutable mesh");
    for(float x:runtime->DoorWorldMatrix())Need(std::isfinite(x),"Nonfinite GPU door matrix");
    std::size_t triangles=0,vertices=0;std::vector<bool> used(scene.vertices.size(),false);
    for(std::size_t i=0;i<scene.surfaces.size();++i)if(i>=firstActor||scene.surfaces[i].model.compare(0,5,"_area")==0){triangles+=scene.surfaces[i].indices.size()/3;for(auto index:scene.surfaces[i].indices)if(!used[index]){used[index]=true;++vertices;}}
    std::cout<<"Campaign adapter: vertices="<<vertices<<" triangles="<<triangles<<" bindings="<<plan.worldBindings<<" textured="<<plan.texturedBindings<<" DDS="<<plan.ddsTextures<<" solid="<<plan.solidTextures<<" rgbaBytes="<<plan.rgbaBytes<<" doorVertices="<<runtime->DoorBindGeometry().vertices.size()<<" doorTriangles="<<runtime->DoorBindGeometry().surfaces[0].indices.size()/3<<" doorTextured=1 slide="<<runtime->State().doorSlide<<" joints="<<runtime->ActorPose().pose.model.size()<<" visible=3 events=4 ownedSources=1 stableMeshes=1\n";
    const auto generation=runtime->State().generation;runtime->Reset();
    Need(runtime->State().generation==generation+1&&!runtime->State().actorStarted&&!runtime->State().doorActivated&&runtime->Wagon().State().visibleArrivalInstances==0&&runtime->DoorWorldMatrix()==initialMatrix&&doorVertices==runtime->DoorBindGeometry().vertices.data(),"Campaign reset did not restore initial pose/hide state");
    std::cout<<"Campaign adapter reset: generation="<<runtime->State().generation<<" hidden=3 events=0 originalDoorMatrix=1\n";return 0;
}
}
int main(int argc,char** argv){try{Need(argc==5,"Expected assets.pk4 trainyard.mpk global.spk localized-voice.spk");return Audit(argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
