// SPDX-License-Identifier: GPL-3.0-or-later
#include "RenderData.h"
#include <map>
#include <stdexcept>
namespace wolf2009 {
namespace idtech4a {
namespace {
void Need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
RenderSurface WhiteSurface() {
    RenderSurface surface;
    surface.model = "_area0";
    surface.material = "synthetic/vertex-modulated";
    surface.vertices = {{0,0,0,0,0,0xffffffffu}, {1,0,0,1,0,0xffffffffu},
                        {0,1,0,0,1,0xffffffffu}, {9,9,9,0,0,0x12345678u}};
    surface.indices = {0,1,2};
    return surface;
}
}
int RunIdentityWhiteMaterialTests() {
    MaterialCatalog catalog;
    catalog.AddGuideSource("synthetic.guide",
        "guide diffuse_modulated(TextureName) { Image DiffuseMap TextureName VertexColor } "
        "guide alpha_modulated(TextureName) { Image DiffuseMap TextureName VertexColor AlphaTest 0.5 }");
    catalog.AddMaterialSource("synthetic.mtr",
        "guide synthetic/vertex-modulated diffuse_modulated(textures/synthetic) "
        "guide synthetic/alpha-modulated alpha_modulated(textures/synthetic-alpha) "
        "synthetic/unknown { Technique UnsupportedTechnique Image DiffuseMap textures/synthetic VertexColor }");
    const ResourcePath path = ParseResourcePath("wolf2009/synthetic/proc/maps/synthetic");
    const RenderSurface white = WhiteSurface();
    Need(HasIdentityWhiteReferencedColor(white), "unused coloured vertex disqualified identity-white draw");
    const DiffuseMaterial accepted = BuildDiffuseMaterial(path, white, &catalog);
    Need(accepted.supported && accepted.identityWhiteVertexColor && accepted.binding.vertexColor,
         "geometry-qualified identity-white diffuse rejected");
    Need(accepted.name == "wolf2009/synthetic/materials-identity-white/synthetic/vertex-modulated",
         "identity-white declaration has no separate namespace");
    Need(accepted.declaration.find("vertexColor") == std::string::npos &&
         accepted.declaration.find("textures/synthetic.dds") != std::string::npos,
         "identity-white stage invented general vertex-color support");
    Need(!BuildDiffuseMaterial(path, white.material, &catalog).supported,
         "unqualified name-only request accepted VertexColor");
    // Distinct raw byte positions, including the fourth component. No source
    // RGBA/ARGB interpretation is necessary to reject any nonidentity input.
    for (std::uint32_t word : {0xfffffffeu, 0xfffffeffu, 0xfffeffffu, 0xfeffffffu, 0x12345678u}) {
        RenderSurface colored = white;
        colored.vertices[1].diffuseColor = word;
        Need(!HasIdentityWhiteReferencedColor(colored), "nonwhite referenced byte accepted");
        const DiffuseMaterial rejected = BuildDiffuseMaterial(path, colored, &catalog);
        Need(!rejected.supported && !rejected.identityWhiteVertexColor &&
             rejected.name != accepted.name && rejected.declaration.find("_default") != std::string::npos,
             "coloured draw inherited identity-white material");
    }
    RenderSurface referenced = white;
    referenced.indices = {0,1,3};
    Need(!HasIdentityWhiteReferencedColor(referenced), "referenced coloured vertex ignored");
    for (bool whiteFirst : {false, true}) {
        std::map<std::string, DiffuseMaterial> cache;
        const RenderSurface* order[] = {whiteFirst ? &white : &referenced, whiteFirst ? &referenced : &white};
        for (const RenderSurface* source : order) {
            const DiffuseMaterial material = BuildDiffuseMaterial(path, *source, &catalog);
            const auto entry = cache.insert(std::make_pair(material.name, material)).first;
            Need(entry->second.supported == HasIdentityWhiteReferencedColor(*source),
                 "material cache order changed geometry qualification");
        }
        Need(cache.size() == 2, "qualified and unverified variants share a material cache key");
    }
    RenderSurface alpha = white;
    alpha.material = "synthetic/alpha-modulated";
    const auto alphaMaterial = BuildDiffuseMaterial(path, alpha, &catalog);
    Need(alphaMaterial.supported && alphaMaterial.identityWhiteVertexColor &&
         alphaMaterial.declaration.find("alphaTest 0.5") != std::string::npos,
         "identity-white profile lost authored alpha test");
    RenderSurface unknown = white;
    unknown.material = "synthetic/unknown";
    Need(!BuildDiffuseMaterial(path, unknown, &catalog).supported,
         "identity-white profile enabled unsupported shader composition");
    RenderSurface empty = white;
    empty.indices.clear();
    Need(!HasIdentityWhiteReferencedColor(empty), "empty draw qualified as identity-white");
    RenderSurface invalid = white;
    invalid.indices[2] = 100;
    bool rejected = false;
    try { HasIdentityWhiteReferencedColor(invalid); } catch (const std::exception&) { rejected = true; }
    Need(rejected, "out-of-range colour qualification index accepted");
    return 0;
}
}
}
