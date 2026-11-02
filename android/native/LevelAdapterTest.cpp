// SPDX-License-Identifier: GPL-3.0-or-later
// Host checks for one-time actor assembly and the Android diffuse adapter.
#include "SceneAssembly.h"
#include "TextureBinding.h"
#include "Archive.h"
#include "WagonScene.h"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
wolf2009::GeometryScene Triangle(const std::string& model){wolf2009::GeometryScene scene;scene.vertices={{0,0,0,0,0},{1,0,0,1,0},{0,1,0,0,1}};wolf2009::GeometrySurface surface;surface.model=model;surface.material="surface";surface.indices={0,1,2};scene.surfaces.push_back(surface);return scene;}
int Tests(){unsigned passed=0;try{
    auto world=Triangle("_area0");const auto actor=Triangle("authored_cart");wolf2009::GeometryLimits limits;const auto first=wolf2009android::AppendPlacedActor(world,actor,limits);Need(first==1&&world.vertices.size()==6&&world.surfaces[1].indices==std::vector<std::uint32_t>({3,4,5})&&world.surfaces[0].indices[0]==0,"Actor append lost index base or changed world geometry");++passed;
    auto over=Triangle("_area0");limits.vertices=5;bool refused=false;try{wolf2009android::AppendPlacedActor(over,actor,limits);}catch(const std::exception&){refused=true;}Need(refused&&over.vertices.size()==3&&over.surfaces.size()==1,"Combined vertex budget failure changed scene");++passed;
    auto invalid=actor;invalid.surfaces[0].indices[2]=100;limits=wolf2009::GeometryLimits();refused=false;try{wolf2009android::AppendPlacedActor(over,invalid,limits);}catch(const std::exception&){refused=true;}Need(refused&&over.vertices.size()==3,"Malformed actor index accepted or scene changed");++passed;
    limits.totalBufferBytes=6*sizeof(wolf2009::GeometryVertex)+5*sizeof(std::uint32_t);refused=false;try{wolf2009android::AppendPlacedActor(over,actor,limits);}catch(const std::exception&){refused=true;}Need(refused&&over.surfaces.size()==1,"Combined CPU buffer budget ignored");++passed;
    std::cout<<"Android level assembly self-test: "<<passed<<" passed, 0 failed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
int Audit(const char* assets,const char* mpk){
    std::ifstream materialSource(assets,std::ios::binary);Need(bool(materialSource),"Cannot read user material package");const auto catalog=wolf2009android::ReadMaterialCatalog(materialSource);
    std::ifstream input(mpk,std::ios::binary);Need(bool(input),"Cannot read user map package");const auto archive=wolf2009::Archive::Scan(input);
    struct Location{std::size_t chunk,entry;};std::map<std::string,Location> buffers,textures;Location proc={0,0};std::string name;unsigned procs=0;
    for(std::size_t c=0;c<archive.Chunks().size();c++)for(std::size_t e=0;e<archive.Chunks()[c].entries.size();e++){const auto& chunk=archive.Chunks()[c];const auto& entry=chunk.entries[e];Location location={c,e};if(chunk.type=="PROC"){++procs;proc=location;name=entry.name;}else if(chunk.type=="PBBF")Need(buffers.insert(std::make_pair(entry.name,location)).second,"Duplicate PBBF");else if(chunk.type=="TXTR")Need(textures.insert(std::make_pair(wolf2009::MaterialCatalog::NormalizeVirtualPath(entry.name),location)).second,"Duplicate TXTR");}
    Need(procs==1,"Expected one PROC");wolf2009::GeometryLimits limits;limits.vertices=limits.triangles=2000000;limits.totalBufferBytes=256u*1024u*1024u;
    auto world=wolf2009::DecodeProc7(archive.ReadEntry(input,proc.chunk,proc.entry),name,[&](const std::string& key){const auto it=buffers.find(key);Need(it!=buffers.end(),"Missing render PBBF");return archive.ReadEntry(input,it->second.chunk,it->second.entry);},limits);
    wolf2009::WagonSceneOptions options;options.geometry=limits;auto wagon=wolf2009::WagonScene::Load(input,archive,name,options);const auto* stable=wagon->PlacedGeometry().vertices.data();const auto first=wolf2009android::AppendPlacedActor(world,wagon->PlacedGeometry(),limits);
    const auto plan=wolf2009android::BindTextures(world,catalog.get(),[&](const std::string& key){const auto it=textures.find(key);return it==textures.end()?std::vector<unsigned char>():archive.ReadEntry(input,it->second.chunk,it->second.entry);},wolf2009android::TextureLimits(),first);
    std::size_t actorBindings=0,actorTriangles=0;for(std::size_t i=first;i<world.surfaces.size();i++){if(plan.surfaces[i].texture>=0)++actorBindings;actorTriangles+=world.surfaces[i].indices.size()/3;}
    for(unsigned i=0;i<120;i++)wagon->Advance(1.0/60);Need(stable==wagon->PlacedGeometry().vertices.data()&&wagon->State().supportContact,"Runtime changed uploaded actor mesh or lost support");
    std::cout<<"actorVertices="<<wagon->PlacedGeometry().vertices.size()<<" actorSurfaces="<<world.surfaces.size()-first<<" actorTriangles="<<actorTriangles<<" actorTextured="<<actorBindings<<" drawBindings="<<plan.worldBindings<<" textured="<<plan.texturedBindings<<" DDS="<<plan.ddsTextures<<" solid="<<plan.solidTextures<<" rgbaBytes="<<plan.rgbaBytes<<" cartY2s="<<wagon->State().cartOrigin.y<<" riderY2s="<<wagon->State().playerFeet.y<<" contact="<<wagon->State().supportContact<<" stableMesh=1\n";return 0;
}
}
int main(int argc,char** argv){try{return argc==3?Audit(argv[1],argv[2]):Tests();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
