// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_MATERIAL_H
#define WOLF2009_MATERIAL_H
#include <cstddef>
#include <memory>
#include <string>
namespace wolf2009 {
enum class MaterialShaderClass {
    Unknown, OpaqueDiffuse, AlphaTestDiffuse,
    AmbientDecalModulate, AmbientDecal2xModulate, Unsupported
};
struct MaterialLimits {
    std::size_t sourceBytes = 4u * 1024u * 1024u;
    std::size_t totalSourceBytes = 32u * 1024u * 1024u;
    std::size_t tokens = 1000000;
    std::size_t tokenBytes = 4096;
    std::size_t materials = 50000;
    std::size_t guides = 4096;
    std::size_t nesting = 64;
};
struct MaterialResult {
    std::string material, diffusePath, normalPath, specularPath, reason;
    // Metadata, not a shader implementation. A known ColorMap decal pass must
    // not be rendered as an opaque color. Unsupported composition retains any
    // independently resolved DiffuseMap for diagnostics/caller fallback policy.
    MaterialShaderClass shaderClass = MaterialShaderClass::Unknown;
    std::string technique, colorPath;
    bool vertexColor = false,hasAlphaTest = false;
    float alphaTest = 0;
    float uvScale[2] = {1.0f,1.0f};
    bool hasUvScale = false;
    bool resolved = false; // Simple diffuse binding and any explicit UV scale are known.
    bool partial = false; // Non-diffuse/complex bindings were deliberately omitted.
};
// A bounded path resolver, not a shader interpreter. Sources are caller-owned
// text; virtual texture names never become host filesystem paths here. Guide
// expansion uses declared parameters, including textual suffixes/prefixes.
// Supports guide and inlineGuide definitions, and bounded inline invocations
// inside a material. Conflicting bindings/overrides stay explicitly unresolved;
// no general shader expressions or blending/vertex-color math are evaluated.
class MaterialCatalog {
public:
    explicit MaterialCatalog(const MaterialLimits& limits = MaterialLimits());
    ~MaterialCatalog();
    MaterialCatalog(const MaterialCatalog&) = delete;
    MaterialCatalog& operator=(const MaterialCatalog&) = delete;
    void AddGuideSource(const std::string& sourceName,const std::string& text);
    void AddMaterialSource(const std::string& sourceName,const std::string& text);
    MaterialResult Resolve(const std::string& materialName) const;
    std::size_t GuideCount() const;
    std::size_t MaterialCount() const;
    // ASCII lowercase, slash normalization, .tga/.dds removal. Rejects absolute,
    // drive-qualified and parent-traversal names. It performs no filesystem IO.
    static std::string NormalizeVirtualPath(const std::string& value);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
int RunMaterialTests();
}
#endif
