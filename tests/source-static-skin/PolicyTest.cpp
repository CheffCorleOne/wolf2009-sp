// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceStaticSkin.h"
#include "RenderData.h"
#include "SkinCatalog.h"
#include "StreamMounts.h"
#include "ZipArchive.h"
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace {
using namespace wolf2009;
namespace render=wolf2009::idtech4a;
typedef WolfSourceStaticSkinVisualPolicy Policy;
const Policy compatibility=Policy::BaseDiffuseWithOmittedVerifiedModulationV1;
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Keep(const MaterialResult& facts){
    Need(!Wolf2009_StaticSkinOmitsVerifiedModulation(Policy::Strict,facts),"strict policy omitted a source shader");
    Need(!Wolf2009_StaticSkinOmitsVerifiedModulation(compatibility,facts),"compatibility omitted an unqualified source shader");
}
void SelfTest(){
    Need(WolfSourceStaticSkinRequest().visualPolicy==Policy::Strict,"strict request default changed");
    Need(WolfSourceStaticSkinState().visualPolicy==Policy::Strict,"strict state default changed");
    Need(!WolfSourceStaticSkinState().compatibilityOverlay&&!WolfSourceStaticSkinState().retailShaderParity,"default state claimed visual compatibility or retail parity");
    Need(std::string(Wolf2009_StaticSkinVisualPolicyName(Policy::Strict))!=Wolf2009_StaticSkinVisualPolicyName(compatibility),"policy identity namespaces coincide");
    Need(!Wolf2009_StaticSkinVisualPolicyName(static_cast<Policy>(77)),"unknown visual policy has an identity");
    MaterialCatalog catalog;
    catalog.AddMaterialSource("synthetic-modulation.mtr",
        "arbitrary/modulate { Technique AmbientDecalModulate Image ColorMap textures/source_color PolygonOffset 1 1 Contents Nonsolid VertexColor }\n"
        "arbitrary/twice { Technique AmbientDecal2xModulate Image ColorMap textures/source_color PolygonOffset 2 2 Contents Nonsolid VertexColor }\n"
        "arbitrary/diffuse { Image DiffuseMap textures/base }\n"
        "arbitrary/cutout { Image DiffuseMap textures/base AlphaTest .25 }\n"
        "arbitrary/missing-offset { Technique AmbientDecalModulate Image ColorMap textures/source_color Contents Nonsolid }\n"
        "arbitrary/missing-nonsolid { Technique AmbientDecal2xModulate Image ColorMap textures/source_color PolygonOffset 2 2 }\n"
        "arbitrary/mixed { Technique AmbientDecalModulate Image ColorMap textures/source_color Image DiffuseMap textures/base PolygonOffset 1 1 Contents Nonsolid }\n"
        "arbitrary/alpha { Technique AmbientDecalModulate Image ColorMap textures/source_color AlphaTest .5 PolygonOffset 1 1 Contents Nonsolid }\n"
        "arbitrary/blend { Technique AmbientDecalModulate Image ColorMap textures/source_color PolygonOffset 1 1 Contents Nonsolid BlendFunc One One }\n"
        "arbitrary/other-technique { Technique AmbientDecalAdd Image ColorMap textures/source_color PolygonOffset 1 1 Contents Nonsolid }\n"
        "arbitrary/unknown { Technique CompletelyUnknown Image DiffuseMap textures/base }\n"
        "arbitrary/color-modulate { Technique AmbientDecalModulate Image ColorMap textures/source_color PolygonOffset 1 1 Contents Nonsolid Vector ColorModulate 1 1 1 1 }\n"
        "arbitrary/color-add { Technique AmbientDecal2xModulate Image ColorMap textures/source_color PolygonOffset 2 2 Contents Nonsolid Vector ColorAdd 0 0 0 0 }\n"
        "arbitrary/normal-map { Technique AmbientDecalModulate Image ColorMap textures/source_color Image NormalMap textures/normal PolygonOffset 1 1 Contents Nonsolid }\n"
        "arbitrary/specular-map { Technique AmbientDecalModulate Image ColorMap textures/source_color Image SpecularMap textures/specular PolygonOffset 1 1 Contents Nonsolid }\n");
    for(const char* name:{"arbitrary/modulate","arbitrary/twice"}){
        const auto facts=catalog.Resolve(name);
        Need(Wolf2009_StaticSkinOmitsVerifiedModulation(compatibility,facts),"verified modulation was not admitted");
        Need(!Wolf2009_StaticSkinOmitsVerifiedModulation(Policy::Strict,facts),"strict policy inherited compatibility omission");
        Need(!Wolf2009_StaticSkinOmitsVerifiedModulation(static_cast<Policy>(77),facts),"unknown policy admitted omission");
        Need(!facts.resolved&&facts.diffusePath.empty()&&facts.colorPath=="textures/source_color"&&facts.vertexColor,"policy changed source composition facts");
    }
    for(const char* name:{"arbitrary/diffuse","arbitrary/cutout","arbitrary/missing-offset","arbitrary/missing-nonsolid","arbitrary/mixed","arbitrary/alpha","arbitrary/blend","arbitrary/other-technique","arbitrary/unknown","arbitrary/color-modulate","arbitrary/color-add","arbitrary/normal-map","arbitrary/specular-map","absent"})Keep(catalog.Resolve(name));
    const auto verified=catalog.Resolve("arbitrary/modulate");
    auto changed=verified;changed.technique="AmbientDecal2xModulate";Keep(changed);
    changed=verified;changed.shaderClass=MaterialShaderClass::Unsupported;Keep(changed);
    changed=verified;changed.colorPath="../escape";Keep(changed);
    changed=verified;changed.colorPath="Textures/Unnormalized";Keep(changed);
    changed=verified;changed.resolved=true;Keep(changed);
    changed=verified;changed.diffusePath="textures/base";Keep(changed);
    changed=verified;changed.hasAlphaTest=true;Keep(changed);
    changed=verified;changed.explicitColorModulate=true;Keep(changed);
    changed=verified;changed.explicitColorAdd=true;Keep(changed);
    std::cout<<"STATIC_SKIN_POLICY_SELFTEST_PASS strict_default=1 verified_classes=2 unqualified_omission=0 source_facts_preserved=1\n";
}
render::Bytes ReadFile(const std::string& path){
    std::ifstream file(path,std::ios::binary);Need(bool(file),"owner file absent");file.seekg(0,std::ios::end);const auto size=file.tellg();
    Need(size>0&&size<=std::streamoff(64u<<20),"material archive byte budget");file.seekg(0);render::Bytes bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()),size);Need(bool(file),"short owner read");return bytes;
}
void OriginalWagon(const std::string& base){
    const auto bytes=ReadFile(base+"/assets.pk4");const auto catalog=render::ReadMaterialCatalog(bytes);
    std::istringstream stream(std::string(bytes.begin(),bytes.end()),std::ios::in|std::ios::binary);
    const auto zip=ZipArchive::Scan(stream);const auto skins=SkinCatalog::Read(stream,zip);
    const std::string package=base+"/maps/game/trainyard/trainyard.mpk";
    StreamMounts mounts([&](const std::string&){return std::unique_ptr<std::istream>(new std::ifstream(package,std::ios::binary));});mounts.Mount("main");
    const render::ResourceReader read=[&](const std::string& path){
        const auto parsed=render::ParseResourcePath(path);Need(parsed.mount=="main","escaped exact owner source mount");std::string name=parsed.name;
        const auto exact=mounts.Find(parsed.type,name);if(exact.status==StreamLookupStatus::Unique)return mounts.Read(exact.matches[0]);
        const std::string suffix=parsed.type=="modl"?".md5r":parsed.type=="pbbf"?".md5rbin":parsed.type=="txtr"?".dds":"";
        Need(!suffix.empty(),"unexpected owner resource type");if(name.size()>=suffix.size()&&name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0)name.resize(name.size()-suffix.size());
        return mounts.ReadUnique(parsed.type,name);
    };
    const auto source=render::ParseResourcePath("wolf2009/main/MODL/models/vehicles/box_car_a.md5r");
    auto geometry=render::LoadGeometry(source,read);const auto original=render::CompactSurfaces(geometry);
    const auto applied=skins.Apply("models/vehicles/box_car_full_in",geometry);const auto authored=render::CompactSurfaces(geometry);
    Need(original.size()==10&&authored.size()==10&&applied.changedSurfaces==0&&applied.matchedSurfaces==0,"original wagon/skin selection facts changed");
    unsigned omitted=0,omittedTriangles=0,diffuse=0,diffuseTriangles=0;std::set<std::string> textures;
    for(std::size_t i=0;i<original.size();++i){
        const auto& before=original[i];const auto& after=authored[i];
        Need(before.material==after.material&&before.indices==after.indices&&before.vertices.size()==after.vertices.size(),"authored no-op changed source draw");
        for(std::size_t v=0;v<before.vertices.size();++v){const auto& a=before.vertices[v];const auto& b=after.vertices[v];Need(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.u==b.u&&a.v==b.v&&a.diffuseColor==b.diffuseColor,"source XYZ/UV/raw colour changed");}
        const auto profile=render::BuildDiffuseMaterial(source,after,catalog.get());
        Need(!Wolf2009_StaticSkinOmitsVerifiedModulation(Policy::Strict,profile.binding),"strict original wagon lost a draw");
        std::string texture;
        if(Wolf2009_StaticSkinOmitsVerifiedModulation(compatibility,profile.binding)){
            Need(!profile.supported&&profile.declaration.find("_default")!=std::string::npos,"strict modulation was not a diagnostic fallback");
            ++omitted;omittedTriangles+=unsigned(after.indices.size()/3);texture=source.Path("TXTR",profile.binding.colorPath+".dds");
        }else{
            Need(profile.supported,"original wagon has another unsupported composition");++diffuse;diffuseTriangles+=unsigned(after.indices.size()/3);texture=profile.image;
        }
        if(textures.insert(texture).second){const auto image=DecodeDdsMip0(read(texture));Need(image.width&&image.height&&!image.rgba.empty(),"original diffuse/ColorMap payload absent or invalid");}
    }
    Need(omitted==3&&omittedTriangles==100&&diffuse==7&&diffuseTriangles==3358,"original wagon policy coverage differs");
    std::cout<<"STATIC_SKIN_POLICY_WAGON_PASS source_surfaces=10 source_triangles=3458 authored_noop=1 omitted_surfaces="<<omitted<<" omitted_triangles="<<omittedTriangles<<" diffuse_surfaces="<<diffuse<<" diffuse_triangles="<<diffuseTriangles<<" original_textures="<<textures.size()<<" shared_geometry_changes=0 retail_shader_parity=0\n";
}
}
int main(int argc,char** argv){try{
    Need(argc==2,"usage: wolf2009-static-skin-policy --self-test | OWNER_SP_BASE (read-only)");
    if(std::string(argv[1])=="--self-test")SelfTest();else OriginalWagon(argv[1]);return 0;
}catch(const std::exception& e){std::cerr<<"STATIC_SKIN_POLICY_FAIL "<<e.what()<<'\n';return 1;}}
