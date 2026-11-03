// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_IDTECH4A_RENDER_DATA_H
#define WOLF2009_IDTECH4A_RENDER_DATA_H
#include "Dds.h"
#include "Geometry.h"
#include "Material.h"
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
};
// Conservative native ambient diffuse stage. Unsupported source techniques
// get an explicit engine checker material, never an invented opaque pass.
DiffuseMaterial BuildDiffuseMaterial(const ResourcePath& source,
                                    const std::string& name,
                                    const MaterialCatalog* catalog);
}
}
#endif
