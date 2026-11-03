// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureBinding.h"
#include "Dds.h"
#include "ZipArchive.h"
#include <algorithm>
#include <map>
#include <new>
#include <stdexcept>
#include <utility>
namespace wolf2009android { namespace {
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string Lower(std::string text){for(char& c:text){if(c>='A'&&c<='Z')c=char(c+32);if(c=='\\')c='/';}return text;}
bool Ends(const std::string& text,const char* suffix){const std::string tail(suffix);return text.size()>=tail.size()&&text.compare(text.size()-tail.size(),tail.size(),tail)==0;}
enum Outcome {Ready,Missing,DecodeFailure,Budget};
struct Cached {int texture=-1;Outcome outcome=Missing;std::string reason;};
}
std::shared_ptr<const wolf2009::MaterialCatalog> ReadMaterialCatalog(std::istream& input){
    wolf2009::ZipLimits zipLimits;zipLimits.fileBytes=128u*1024u*1024u;zipLimits.entries=50000;zipLimits.totalNameBytes=8u*1024u*1024u;
    const wolf2009::ZipArchive archive=wolf2009::ZipArchive::Scan(input,zipLimits);
    return ReadMaterialCatalog(input,archive);
}
std::shared_ptr<const wolf2009::MaterialCatalog> ReadMaterialCatalog(std::istream& input,const wolf2009::ZipArchive& archive){
    const wolf2009::MaterialLimits limits;
    std::shared_ptr<wolf2009::MaterialCatalog> catalog(new wolf2009::MaterialCatalog(limits));
    for(unsigned pass=0;pass<2;pass++)for(std::size_t i=0;i<archive.Entries().size();i++){
        const wolf2009::ZipEntry& entry=archive.Entries()[i];const std::string name=Lower(entry.name);
        if(!Ends(name,pass==0?".guide":".mtr"))continue;
        Require(entry.size<=limits.sourceBytes,"Material source exceeds configured size limit");
        const std::vector<unsigned char> bytes=archive.ReadEntry(input,i);const std::string text(bytes.begin(),bytes.end());
        if(pass==0)catalog->AddGuideSource(entry.name,text);else catalog->AddMaterialSource(entry.name,text);
    }
    Require(catalog->MaterialCount()!=0,"Selected PK4 contains no material declarations");return catalog;
}
std::shared_ptr<const wolf2009::SkinCatalog> ReadSkinCatalog(std::istream& input) {
    wolf2009::ZipLimits limits;
    limits.fileBytes = 128u*1024u*1024u;
    limits.entries = 50000;
    limits.totalNameBytes = 8u*1024u*1024u;
    const auto archive = wolf2009::ZipArchive::Scan(input, limits);
    return std::make_shared<wolf2009::SkinCatalog>(wolf2009::SkinCatalog::Read(input,archive));
}
TexturePlan BindTextures(const wolf2009::GeometryScene& scene,const wolf2009::MaterialCatalog* catalog,
                         const TextureResolver& resolver,const TextureLimits& limits,std::size_t firstAttachedSurface){
    TexturePlan plan;plan.surfaces.resize(scene.surfaces.size());std::map<std::string,Cached> cache;
    for(std::size_t i=0;i<scene.surfaces.size();i++){
        const wolf2009::GeometrySurface& surface=scene.surfaces[i];SurfaceBinding& binding=plan.surfaces[i];
        if((surface.model.compare(0,5,"_area")!=0&&i<firstAttachedSurface)||surface.indices.empty()){binding.reason="non-world surface omitted";continue;}
        ++plan.worldBindings;
        if(!catalog){binding.reason="material catalog not loaded";++plan.flatBindings;continue;}
        wolf2009::MaterialResult material;
        try{material=catalog->Resolve(surface.material);}catch(const std::bad_alloc&){throw;}catch(const std::exception& e){binding.reason=e.what();++plan.flatBindings;continue;}
        binding.resolved=material.resolved;binding.partial=material.partial;
        if (material.shaderClass == wolf2009::MaterialShaderClass::AmbientDecalModulate ||
            material.shaderClass == wolf2009::MaterialShaderClass::AmbientDecal2xModulate) {
            // These passes multiply an already drawn surface. Until vertex colour and
            // shader uniforms are available, painting them into the depth buffer hides it.
            binding.omitOverlay = true;
            binding.reason = "multiplicative overlay omitted from the opaque pass";
            ++plan.omittedOverlays;
            continue;
        }
        if (material.hasAlphaTest) binding.alphaCutoff = material.alphaTest;
        if(!material.resolved){binding.reason=material.reason;++plan.flatBindings;continue;}
        ++plan.resolvedBindings;if(material.partial)++plan.partialBindings;
        binding.uvScale[0]=material.uvScale[0];binding.uvScale[1]=material.uvScale[1];
        std::string path;
        try{path=wolf2009::MaterialCatalog::NormalizeVirtualPath(material.diffusePath);}catch(const std::bad_alloc&){throw;}catch(const std::exception& e){binding.reason=e.what();++plan.flatBindings;continue;}
        auto found=cache.find(path);
        if(found==cache.end()){
            Cached outcome;
            if(plan.images.size()>=limits.textures||plan.rgbaBytes>=limits.retainedBytes){outcome.outcome=Budget;outcome.reason="scene texture budget exceeded";}
            else try{
                TextureImage image;image.name=path;
                if(path=="_white"||path=="_black"){
                    image.width=image.height=1;image.generated=true;image.rgba.assign(4,path=="_white"?255:0);image.rgba[3]=255;
                }else{
                    Require(bool(resolver),"Texture resolver not supplied");const std::vector<unsigned char> bytes=resolver(path);
                    if(bytes.empty()){outcome.outcome=Missing;outcome.reason="diffuse DDS not found in selected MPK";}
                    else{
                        wolf2009::DdsLimits ddsLimits;ddsLimits.maximumWidth=ddsLimits.maximumHeight=limits.dimension;ddsLimits.decodedBytes=limits.decodedImageBytes;
                        wolf2009::DdsImage decoded=wolf2009::DecodeDdsMip0(bytes,ddsLimits);
                        Require(decoded.format!="A8","A8 diffuse opacity semantics are not supported by this opaque preview");
                        image.width=decoded.width;image.height=decoded.height;image.rgba.swap(decoded.rgba);
                    }
                }
                if(!image.rgba.empty()){
                    if(image.rgba.size()>limits.retainedBytes-plan.rgbaBytes){outcome.outcome=Budget;outcome.reason="scene texture byte budget exceeded";}
                    else{outcome.texture=int(plan.images.size());outcome.outcome=Ready;plan.rgbaBytes+=image.rgba.size();if(image.generated)++plan.solidTextures;else ++plan.ddsTextures;plan.images.push_back(std::move(image));}
                }
            }catch(const std::bad_alloc&){throw;}catch(const std::exception& e){outcome.outcome=DecodeFailure;outcome.reason=e.what();}
            found=cache.insert(std::make_pair(path,outcome)).first;
        }
        binding.texture=found->second.texture;binding.reason=found->second.reason;
        if(binding.texture>=0)++plan.texturedBindings;
        else{++plan.flatBindings;if(found->second.outcome==Missing)++plan.missingBindings;else if(found->second.outcome==DecodeFailure)++plan.decodeFallbacks;else if(found->second.outcome==Budget)++plan.budgetFallbacks;}
    }
    return plan;
}
}
