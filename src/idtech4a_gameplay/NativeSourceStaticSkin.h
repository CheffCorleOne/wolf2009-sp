// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_STATIC_SKIN_H
#define WOLF2009_NATIVE_SOURCE_STATIC_SKIN_H
// Game-private C++11 owner; no objects or STL cross GetGameAPI/API8.
#include "Material.h"
#include <exception>
#include <memory>
#include <string>
class idStr;
enum class WolfSourceStaticSkinVisualPolicy {
    Strict=0,
    BaseDiffuseWithOmittedVerifiedModulationV1=1
};
// Portable policy seam. The compatibility profile deliberately omits two
// source-verified ColorMap modulation classes; it does not reinterpret them
// as diffuse or infer blending/colour math. Every other class keeps the
// resident strict shader, including its explicit unsupported fallback.
inline const char* Wolf2009_StaticSkinVisualPolicyName(WolfSourceStaticSkinVisualPolicy policy){
    switch(policy){
        case WolfSourceStaticSkinVisualPolicy::Strict:return "strict";
        case WolfSourceStaticSkinVisualPolicy::BaseDiffuseWithOmittedVerifiedModulationV1:return "base-diffuse-with-omitted-verified-modulation-v1";
    }
    return NULL;
}
inline bool Wolf2009_StaticSkinOmitsVerifiedModulation(WolfSourceStaticSkinVisualPolicy policy,const wolf2009::MaterialResult& facts){
    if(policy!=WolfSourceStaticSkinVisualPolicy::BaseDiffuseWithOmittedVerifiedModulationV1)return false;
    std::string technique=facts.technique;for(char& c:technique)if(c>='A'&&c<='Z')c+=32;
    const bool qualified=(facts.shaderClass==wolf2009::MaterialShaderClass::AmbientDecalModulate&&technique=="ambientdecalmodulate")||
        (facts.shaderClass==wolf2009::MaterialShaderClass::AmbientDecal2xModulate&&technique=="ambientdecal2xmodulate");
    if(!qualified||facts.resolved||facts.colorPath.empty()||!facts.diffusePath.empty()||!facts.normalPath.empty()||!facts.specularPath.empty()||
       facts.hasAlphaTest||facts.explicitColorModulate||facts.explicitColorAdd)return false;
    try{return wolf2009::MaterialCatalog::NormalizeVirtualPath(facts.colorPath)==facts.colorPath;}catch(const std::exception&){return false;}
}
struct WolfSourceStaticSkinRequest {
    // All paths/names are explicit. The model must already be loaded through
    // the ordinary qualified static-MODL renderer. No pack/name fallback.
    std::string exactModelResource,nativeModelName,materialArchive,authoredSkin;
    WolfSourceStaticSkinVisualPolicy visualPolicy=WolfSourceStaticSkinVisualPolicy::Strict;
};
struct WolfSourceStaticSkinState {
    unsigned surfaces=0,matchedSurfaces=0,changedSurfaces=0,remaps=0,textures=0;
    unsigned omittedModulationSurfaces=0,omittedModulationTriangles=0;
    unsigned long long archiveBytes=0,archiveFingerprint=0;
    WolfSourceStaticSkinVisualPolicy visualPolicy=WolfSourceStaticSkinVisualPolicy::Strict;
    bool explicitPairsOnly=false,sharedGeometryUnchanged=false,authoredNoOp=false,retailShaderParity=false;
    bool compatibilityOverlay=false;
};
class WolfSourceStaticSkin {
  public:
    ~WolfSourceStaticSkin();
    const char* NativeAlias() const;
    const char* AuthoredName() const;
    const char* NativeModelName() const;
    bool Preflight(idStr& reason) const;
    bool Query(WolfSourceStaticSkinState&,idStr& reason) const;
    WolfSourceStaticSkin(const WolfSourceStaticSkin&)=delete;
    WolfSourceStaticSkin& operator=(const WolfSourceStaticSkin&)=delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfSourceStaticSkin(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceStaticSkin> Wolf2009_PrepareSourceStaticSkin(
        const WolfSourceStaticSkinRequest&,idStr&);
};
// Explicit from/to pair skin conversion. SkinCatalog refuses selected
// wildcard/model/duplicate directives. Changed targets require the existing
// qualified native diffuse shader policy and identical shared UV/RGBA encoding.
// Unmatched surfaces retain their existing material. A valid selected skin
// with no matches is retained as an explicit identity customSkin and reported
// as authoredNoOp; it is never treated as a missing skin. New material
// clones and skin declarations never replace canonical or foreign declarations.
// The optional visual policy runs after the authored mapping, retains all
// source/native geometry, and reports its omitted modulation passes separately
// from authoredNoOp. Strict remains the default; retail shader parity is false.
// Keep this owner alive until every native entity using NativeAlias is removed.
std::unique_ptr<WolfSourceStaticSkin> Wolf2009_PrepareSourceStaticSkin(
    const WolfSourceStaticSkinRequest&,idStr& reason);
int Wolf2009_LiveSourceStaticSkins();
#endif
