// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_GEOMETRY_H
#define WOLF2009_GEOMETRY_H
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace wolf2009 {
struct GeometryLimits {
    std::size_t sourceBytes = 64u * 1024u * 1024u;
    std::size_t bufferBytes = 256u * 1024u * 1024u;
    std::size_t totalBufferBytes = 512u * 1024u * 1024u;
    std::size_t syntaxNodes = 2000000;
    std::size_t vertices = 4000000;
    std::size_t triangles = 4000000;
    std::size_t surfaces = 100000;
    std::size_t models = 100000;
};
struct GeometryVertex {
    float x, y, z, u, v;
    // Raw little-endian DiffuseColor word, preserved without channel inference.
    // Packed 40-byte vertices store it at 28; float 64-byte vertices at 52.
    std::uint32_t diffuseColor;
};
struct GeometrySurface {
    std::string model, material;
    std::vector<std::uint32_t> indices;
};
struct GeometryScene {
    std::vector<GeometryVertex> vertices;
    std::vector<GeometrySurface> surfaces;
    std::size_t models = 0, skippedMeshes = 0;
};
// The resolver returns a named PBBF payload, never a host pathname. Original
// .MD5RBin references are normalized to extensionless, lowercase virtual names.
typedef std::function<std::vector<unsigned char>(const std::string&)> BufferResolver;
GeometryScene DecodeProc7(const std::vector<unsigned char>& data,
                          const std::string& resourceName,
                          const BufferResolver& resolver,
                          const GeometryLimits& limits = GeometryLimits());
// Strict static MD5RVersion 1 support. Stored LoadVertexFormat takes precedence
// over the runtime format. Joint/skinning variants are explicitly rejected;
// no bind-pose reconstruction or animation support is implied.
GeometryScene DecodeStaticModel1(const std::vector<unsigned char>& data,
                                 const std::string& resourceName,
                                 const BufferResolver& resolver = BufferResolver(),
                                 const GeometryLimits& limits = GeometryLimits());
int RunGeometryTests();
}
#endif
