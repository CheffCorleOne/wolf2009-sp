// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for the platform-independent Android diffuse binding adapter.
#include "TextureBinding.h"
#include "Archive.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Put(Bytes& out,std::size_t offset,std::uint32_t value,unsigned count=4){for(unsigned i=0;i<count;i++)out[offset+i]=static_cast<unsigned char>(value>>(i*8));}
std::uint32_t Crc(const Bytes& bytes){std::uint32_t crc=0xffffffffu;for(unsigned char byte:bytes){crc^=byte;for(unsigned i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&std::uint32_t(-int(crc&1)));}return crc^0xffffffffu;}
Bytes RedDds(){Bytes bytes(136,0);Put(bytes,0,0x20534444);Put(bytes,4,124);Put(bytes,8,0x1007);Put(bytes,12,4);Put(bytes,16,4);Put(bytes,28,1);Put(bytes,76,32);Put(bytes,80,4);Put(bytes,84,0x31545844);Put(bytes,108,0x1000);Put(bytes,128,0xf800,2);return bytes;}
wolf2009::GeometryScene Scene(const std::vector<std::string>& materials){wolf2009::GeometryScene scene;for(const std::string& material:materials){wolf2009::GeometrySurface surface;surface.model="_area0";surface.material=material;surface.indices={0,1,2};scene.surfaces.push_back(surface);}return scene;}
Bytes Zip(const std::vector<std::pair<std::string,std::string>>& sources){
    Bytes local,central;
    for(const auto& source:sources){
        const Bytes data(source.second.begin(),source.second.end());const std::uint32_t crc=Crc(data);const std::size_t offset=local.size(),start=central.size();
        local.resize(offset+30+source.first.size()+data.size(),0);Put(local,offset,0x04034b50);Put(local,offset+4,20,2);Put(local,offset+14,crc);Put(local,offset+18,data.size());Put(local,offset+22,data.size());Put(local,offset+26,source.first.size(),2);
        std::copy(source.first.begin(),source.first.end(),local.begin()+offset+30);std::copy(data.begin(),data.end(),local.begin()+offset+30+source.first.size());
        central.resize(start+46+source.first.size(),0);Put(central,start,0x02014b50);Put(central,start+4,20,2);Put(central,start+6,20,2);Put(central,start+16,crc);Put(central,start+20,data.size());Put(central,start+24,data.size());Put(central,start+28,source.first.size(),2);Put(central,start+42,offset);std::copy(source.first.begin(),source.first.end(),central.begin()+start+46);
    }
    Bytes result=local;result.insert(result.end(),central.begin(),central.end());const std::size_t end=result.size();result.resize(end+22,0);Put(result,end,0x48530a02);Put(result,end+8,sources.size(),2);Put(result,end+10,sources.size(),2);Put(result,end+12,central.size());Put(result,end+16,local.size());return result;
}
int Tests(){unsigned passed=0;
    try{
        const Bytes zip=Zip({{"materials/test.mtr","guide textures/a surface(\"textures/shared\",2,3)\ntextures/b { Image DiffuseMap textures/shared_d }\ntextures/partial { Image DiffuseMap textures/shared_d Image NormalMap add(foo,bar) }\n"},{"guides/test.guide","guide surface(Name,U,V) { Image DiffuseMap Name_d UVScale U V }\n"}});
        std::istringstream stream(std::string(zip.begin(),zip.end()));const auto catalog=wolf2009android::ReadMaterialCatalog(stream);
        Need(catalog->GuideCount()==1&&catalog->MaterialCount()==3,"Catalog source ordering/counts incorrect");++passed;
        wolf2009::MaterialCatalog passes;
        passes.AddMaterialSource("materials/synthetic.mtr",
            "textures/decal {\nTechnique AmbientDecalModulate\nImage ColorMap textures/decal_d\n"
            "PolygonOffset 1 1\nContents Nonsolid\nVertexColor\n}\n"
            "textures/cutout {\nImage DiffuseMap textures/cut_d\nAlphaTest 0.25\n}\n");
        auto overlay = wolf2009android::BindTextures(Scene({"textures/decal"}), &passes,
            [](const std::string&) -> Bytes { throw std::runtime_error("Unexpected decal texture read"); });
        Need(overlay.omittedOverlays == 1 && overlay.surfaces[0].omitOverlay &&
             overlay.images.empty() && overlay.flatBindings == 0, "Overlay painted as an opaque surface");
        ++passed;
        auto cutout = wolf2009android::BindTextures(Scene({"textures/cutout"}), &passes,
            [](const std::string&) { return RedDds(); });
        Need(cutout.surfaces[0].texture == 0 && cutout.surfaces[0].alphaCutoff == 0.25f &&
             !cutout.surfaces[0].omitOverlay, "Alpha test did not reach the GPU batch");
        ++passed;
        unsigned calls=0;const auto resolve=[&](const std::string& name){++calls;Need(name=="textures/shared_d","Normalized diffuse path incorrect");return RedDds();};
        auto plan=wolf2009android::BindTextures(Scene({"textures/a","textures/b","textures/partial"}),catalog.get(),resolve);
        Need(plan.texturedBindings==3&&plan.flatBindings==0&&plan.ddsTextures==1&&calls==1,"Diffuse deduplication or binding counts incorrect");
        Need(plan.images[0].rgba[0]==255&&plan.images[0].rgba[1]==0&&plan.images[0].rgba[2]==0&&plan.surfaces[0].uvScale[0]==2&&plan.surfaces[0].uvScale[1]==3,"Golden diffuse pixel/UV scale incorrect");++passed;
        Need(plan.partialBindings==1&&plan.surfaces[2].partial&&plan.surfaces[2].texture==0,"Known diffuse of partial material was discarded");++passed;
        plan=wolf2009android::BindTextures(Scene({"textures/a","textures/a"}),catalog.get(),[](const std::string&){return Bytes();});Need(plan.flatBindings==2&&plan.missingBindings==2&&plan.images.empty(),"Missing DDS did not remain flat");++passed;
        plan=wolf2009android::BindTextures(Scene({"textures/a"}),catalog.get(),[](const std::string&){return Bytes(4,0);});Need(plan.flatBindings==1&&plan.decodeFallbacks==1,"Bad DDS did not remain flat");++passed;
        wolf2009android::TextureLimits limits;limits.retainedBytes=63;plan=wolf2009android::BindTextures(Scene({"textures/a"}),catalog.get(),resolve,limits);Need(plan.images.empty()&&plan.budgetFallbacks==1&&plan.rgbaBytes==0,"Texture memory cap failed");++passed;
        limits=wolf2009android::TextureLimits();limits.textures=0;calls=0;plan=wolf2009android::BindTextures(Scene({"textures/a"}),catalog.get(),resolve,limits);Need(calls==0&&plan.budgetFallbacks==1,"Texture count cap did not prevent IO");++passed;
        wolf2009::MaterialCatalog solids;solids.AddMaterialSource("solid.mtr","white { Image DiffuseMap _white }\nblack { Image DiffuseMap _black }\n");
        plan=wolf2009android::BindTextures(Scene({"white","black"}),&solids,[](const std::string&)->Bytes{throw std::runtime_error("Solid texture performed IO");});Need(plan.solidTextures==2&&plan.ddsTextures==0&&plan.images[0].rgba[0]==255&&plan.images[1].rgba[0]==0&&plan.images[1].rgba[3]==255,"Explicit solid builtin colors incorrect");++passed;
        calls=0;plan=wolf2009android::BindTextures(Scene({"textures/a"}),NULL,resolve);Need(calls==0&&plan.flatBindings==1&&plan.resolvedBindings==0,"No-catalog fallback performed IO");++passed;
        auto scene=Scene({"textures/a"});scene.surfaces[0].model="local_prop";plan=wolf2009android::BindTextures(scene,catalog.get(),resolve);Need(plan.worldBindings==0&&plan.images.empty(),"Local submodel entered world texture plan");++passed;
        plan=wolf2009android::BindTextures(scene,catalog.get(),resolve,wolf2009android::TextureLimits(),0);Need(plan.worldBindings==1&&plan.texturedBindings==1&&plan.images.size()==1,"Explicit placed actor did not enter texture plan");++passed;
        scene=Scene({"textures/a","textures/a","textures/a"});scene.surfaces[1].model="unplaced_prop";scene.surfaces[2].model="placed_actor";plan=wolf2009android::BindTextures(scene,catalog.get(),resolve,wolf2009android::TextureLimits(),2);Need(plan.worldBindings==2&&plan.texturedBindings==2&&plan.surfaces[1].texture<0&&plan.images.size()==1,"Actor marker included unplaced props or broke shared texture cache");++passed;
        const Bytes skinZip=Zip({{"skins/test.skin","skin example { models/base models/inside }"}});std::istringstream skinStream(std::string(skinZip.begin(),skinZip.end()));const auto skins=wolf2009android::ReadSkinCatalog(skinStream);auto skinScene=Scene({"models/base"});const auto applied=skins->Apply("example",skinScene);Need(applied.changedSurfaces==1&&skinScene.surfaces[0].material=="models/inside","PK4 skin import lost explicit remap");++passed;
        std::istringstream noSkinStream(std::string(zip.begin(),zip.end()));Need(wolf2009android::ReadSkinCatalog(noSkinStream)->SkinCount()==0,"Skinless material archive invented skin definitions");++passed;
        std::cout<<"Android texture binding self-test: "<<passed<<" passed, 0 failed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Texture binding test failed after "<<passed<<": "<<e.what()<<'\n';return 1;}
}
int Audit(const char* assets,const char* mpk){
    std::ifstream source(assets,std::ios::binary);Need(bool(source),"Cannot read assets PK4");const auto catalog=wolf2009android::ReadMaterialCatalog(source);
    std::ifstream input(mpk,std::ios::binary);Need(bool(input),"Cannot read MPK");const auto archive=wolf2009::Archive::Scan(input);
    struct Location{std::size_t chunk,entry;};Location proc={0,0};std::string name;unsigned procs=0;std::map<std::string,Location> geometry,textures;
    for(std::size_t c=0;c<archive.Chunks().size();c++){const auto& chunk=archive.Chunks()[c];for(std::size_t e=0;e<chunk.entries.size();e++){
        Location here={c,e};if(chunk.type=="PROC"){proc=here;name=chunk.entries[e].name;++procs;}
        else if(chunk.type=="PBBF")geometry[chunk.entries[e].name]=here;
        else if(chunk.type=="TXTR")Need(textures.insert(std::make_pair(wolf2009::MaterialCatalog::NormalizeVirtualPath(chunk.entries[e].name),here)).second,"Duplicate TXTR");
    }}
    Need(procs==1,"Expected one PROC");const auto scene=wolf2009::DecodeProc7(archive.ReadEntry(input,proc.chunk,proc.entry),name,[&](const std::string& path){const auto found=geometry.find(path);Need(found!=geometry.end(),"Missing PBBF");return archive.ReadEntry(input,found->second.chunk,found->second.entry);});
    const auto plan=wolf2009android::BindTextures(scene,catalog.get(),[&](const std::string& path){const auto found=textures.find(path);return found==textures.end()?Bytes():archive.ReadEntry(input,found->second.chunk,found->second.entry);});
    std::cout<<"{\"materials\":"<<catalog->MaterialCount()<<",\"guides\":"<<catalog->GuideCount()<<",\"worldBindings\":"<<plan.worldBindings<<",\"resolvedBindings\":"<<plan.resolvedBindings<<",\"texturedBindings\":"<<plan.texturedBindings<<",\"ddsTextures\":"<<plan.ddsTextures<<",\"solidTextures\":"<<plan.solidTextures<<",\"flatBindings\":"<<plan.flatBindings<<",\"partialBindings\":"<<plan.partialBindings<<",\"rgbaBytes\":"<<plan.rgbaBytes<<",\"missing\":"<<plan.missingBindings<<",\"decodeFailures\":"<<plan.decodeFallbacks<<",\"budget\":"<<plan.budgetFallbacks<<"}\n";return 0;
}
}
int main(int argc,char** argv){try{return argc==3?Audit(argv[1],argv[2]):Tests();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
