// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ANDROID_TEXTURE_BINDING_H
#define WOLF2009_ANDROID_TEXTURE_BINDING_H
#include "Geometry.h"
#include "Material.h"
#include "SkinCatalog.h"
#include "ZipArchive.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
namespace wolf2009android {
struct TextureLimits {
    std::size_t retainedBytes=96u*1024u*1024u;
    std::size_t decodedImageBytes=64u*1024u*1024u;
    std::size_t textures=256;
    std::uint32_t dimension=4096;
};
struct TextureImage {
    std::string name;
    std::uint32_t width=0,height=0;
    std::vector<unsigned char> rgba;
    bool generated=false;
};
struct SurfaceBinding {
    int texture=-1;
    float uvScale[2]={1,1};
    bool resolved=false,partial=false;
    bool omitOverlay=false;
    float alphaCutoff=-1;
    std::string reason;
};
struct TexturePlan {
    std::vector<TextureImage> images;
    std::vector<SurfaceBinding> surfaces; // Same indexing as GeometryScene surfaces.
    // Legacy name: counts world surfaces plus explicitly attached actor surfaces.
    std::size_t worldBindings=0,resolvedBindings=0,texturedBindings=0,flatBindings=0;
    std::size_t partialBindings=0,rgbaBytes=0,ddsTextures=0,solidTextures=0;
    std::size_t omittedOverlays=0;
    std::size_t missingBindings=0,decodeFallbacks=0,budgetFallbacks=0;
};
typedef std::function<std::vector<unsigned char>(const std::string&)> TextureResolver;
std::shared_ptr<const wolf2009::MaterialCatalog> ReadMaterialCatalog(std::istream& input);
std::shared_ptr<const wolf2009::MaterialCatalog> ReadMaterialCatalog(std::istream& input,const wolf2009::ZipArchive& archive);
std::shared_ptr<const wolf2009::SkinCatalog> ReadSkinCatalog(std::istream& input);
// Default eligibility is _area*. An explicit suffix marker includes already
// placed actor surfaces without changing core model names or unplaced props.
TexturePlan BindTextures(const wolf2009::GeometryScene& scene,
                         const wolf2009::MaterialCatalog* catalog,
                         const TextureResolver& resolver,
                         const TextureLimits& limits=TextureLimits(),
                         std::size_t firstAttachedSurface=(std::numeric_limits<std::size_t>::max)());
}
#endif
