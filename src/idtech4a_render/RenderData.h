// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_IDTECH4A_RENDER_DATA_H
#define WOLF2009_IDTECH4A_RENDER_DATA_H
#include "Dds.h"
#include "Geometry.h"
#include "Material.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace wolf2009 {
namespace idtech4a {
typedef std::vector<unsigned char> Bytes;
typedef std::function<Bytes(const std::string&)> ResourceReader;
struct ResourcePath {
    std::string mount, type, name;
    std::string Prefix() const;
    std::string Path(const std::string& resourceType, const std::string& resourceName) const;
};
// Only the explicit read-only engine namespace is accepted. The decoder's
// relative buffer resolver always stays in the selected source's PBBF table.
ResourcePath ParseResourcePath(const std::string& path);
bool HasResourceType(const char* path, const char* type);
GeometryScene LoadGeometry(const ResourcePath& source, const ResourceReader& read);
struct RenderSurface {
    std::string model, material;
    std::vector<GeometryVertex> vertices;
    std::vector<std::uint32_t> indices;
};
// Compact every draw independently, preserving source index order and UVs.
// GeometryVertex intentionally has no decoded normals: native FinishSurfaces
// derives normals and tangents rather than treating uninitialised data as valid.
std::vector<RenderSurface> CompactSurfaces(const GeometryScene& scene);
std::unique_ptr<MaterialCatalog> ReadMaterialCatalog(const Bytes& archive);
struct DiffuseMaterial {
    std::string name, image, declaration, reason;
    MaterialResult binding;
    bool supported = false;
    bool identityWhiteVertexColor = false;
    bool nativeVertexColor = false;
};
// Every referenced raw word must be FFFFFFFF; unused vertices do not affect the draw. Bad indices
// throw, empty draws do not qualify. No ARGB/RGBA order is inferred.
bool HasIdentityWhiteReferencedColor(const RenderSurface& surface);
// Conservative native ambient diffuse stage. Unsupported source techniques
// get an explicit engine checker material, never an invented opaque pass.
DiffuseMaterial BuildDiffuseMaterial(const ResourcePath& source,
                                    const std::string& name,
                                    const MaterialCatalog* catalog);
// Geometry-qualified identity and standard/detail GBuffer colour modulation
// have separate native names/cache keys. Name-only VertexColor requests fail.
// Explicit colour vectors fail; alpha-tested draws additionally require A=255
// at every referenced vertex, matching the native texture-only depth prepass.
DiffuseMaterial BuildDiffuseMaterial(const ResourcePath& source,
                                    const RenderSurface& surface,
                                    const MaterialCatalog* catalog);
// Apply the renderer's DDS validation to an otherwise admitted declaration.
// Missing/invalid images retain the material identity and source binding facts,
// but use the explicit checker encoding (unscaled UVs and white native RGBA).
// This is data only: it creates no native declarations or resource fallback.
DiffuseMaterial QualifyDiffuseMaterial(DiffuseMaterial candidate,
                                      const ResourceReader& read);
// Native byte components after the source D3DCOLOR declaration and GBuffer
// vertex shader's .zyxw permutation. Disabled/fallback profiles return white.
std::array<unsigned char,4> NativeDiffuseColor(const DiffuseMaterial& material,
                                              std::uint32_t sourceWord);
int RunIdentityWhiteMaterialTests();
}
}
#endif
