// SPDX-License-Identifier: GPL-3.0-or-later
#include "RenderData.h"
#include <cmath>
#include <map>
#include <stdexcept>
namespace wolf2009 {
namespace idtech4a {
namespace {
void Need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Bytes AuthoredDiffuse() {
    Bytes bytes(136,0);
    const auto put=[&](unsigned at,std::uint32_t word){for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<unsigned char>(word>>(8*i));};
    put(0,0x20534444);put(4,124);put(8,0x1007);put(12,4);put(16,4);put(28,1);
    put(76,32);put(80,4);put(84,0x31545844);put(108,0x1000);
    return bytes; // An independently authored 4x4 DXT1 block.
}
RenderSurface WhiteSurface() {
    RenderSurface surface;
    surface.model = "_area0";
    surface.material = "synthetic/vertex-modulated";
    surface.vertices = {{0,0,0,0,0,0xffffffffu}, {1,0,0,1,0,0xffffffffu},
                        {0,1,0,0,1,0xffffffffu}, {9,9,9,0,0,0x12345678u}};
    surface.indices = {0,1,2};
    return surface;
}
void CheckSourceColor(const DiffuseMaterial& material, std::uint32_t word,
                      const std::array<unsigned char,4>& expected) {
    const auto native = NativeDiffuseColor(material, word);
    Need(native == expected, "native diffuse components differ from the source shader profile");
    // Independent reference: D3DCOLOR creates normalized ARGB->RGBA input,
    // then the retail standard/detail vertex program selects .zyxw.
    const double input[4] = {double((word >> 16) & 255u) / 255.0,
                             double((word >> 8) & 255u) / 255.0,
                             double(word & 255u) / 255.0,
                             double(word >> 24) / 255.0};
    const double output[4] = {input[2], input[1], input[0], input[3]};
    for (std::size_t i = 0; i < 4; ++i)
        Need(std::abs(double(native[i]) / 255.0 - output[i]) < 1e-12,
             "normalized native colour differs from D3DCOLOR plus GBuffer permutation");
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
        "synthetic/unknown { Technique UnsupportedTechnique Image DiffuseMap textures/synthetic VertexColor } "
        "synthetic/unmodulated { Image DiffuseMap textures/synthetic } "
        "synthetic/detail { Technique DSAddGBuffer%r_gBufferMode%_DetailNormal_Depth "
        "Image DiffuseMap textures/synthetic VertexColor } "
        "synthetic/tint { Image DiffuseMap textures/synthetic VertexColor Vector ColorModulate 1 1 1 1 } "
        "synthetic/add { Image DiffuseMap textures/synthetic Vector ColorAdd 0 0 0 0 VertexColor } "
        "synthetic/no-vertex-tint { Image DiffuseMap textures/synthetic Vector ColorModulate 1 1 1 1 }");
    const ResourcePath path = ParseResourcePath("wolf2009/synthetic/proc/maps/synthetic");
    const RenderSurface white = WhiteSurface();
    Need(HasIdentityWhiteReferencedColor(white), "unused coloured vertex disqualified identity-white draw");
    const DiffuseMaterial accepted = BuildDiffuseMaterial(path, white, &catalog);
    Need(accepted.supported && accepted.identityWhiteVertexColor && !accepted.nativeVertexColor && accepted.binding.vertexColor,
         "geometry-qualified identity-white diffuse rejected");
    Need(accepted.name == "wolf2009/synthetic/materials-identity-white/synthetic/vertex-modulated",
         "identity-white declaration has no separate namespace");
    Need(accepted.declaration.find("vertexColor") == std::string::npos &&
         accepted.declaration.find("textures/synthetic.dds") != std::string::npos,
         "identity-white stage invented general vertex-color support");
    unsigned reads=0;
    const auto qualified=QualifyDiffuseMaterial(accepted,[&](const std::string& requested){
        Need(requested==accepted.image,"DDS qualification searched a different source resource");++reads;return AuthoredDiffuse();});
    Need(reads==1&&qualified.supported&&qualified.name==accepted.name&&qualified.image==accepted.image&&qualified.declaration==accepted.declaration,
         "Valid source DDS changed the admitted material");
    for(unsigned mode=0;mode<2;++mode){
        const auto missing=QualifyDiffuseMaterial(accepted,[&](const std::string& requested)->Bytes{
            Need(requested==accepted.image,"Invalid DDS qualification tried a different resource");++reads;
            if(mode)throw std::runtime_error("authored missing source");return Bytes(7,0);});
        Need(!missing.supported&&!missing.reason.empty()&&missing.image.empty()&&missing.name==accepted.name&&
             missing.declaration=="{\n noShadows\n { map _default }\n}\n"&&missing.binding.vertexColor==accepted.binding.vertexColor&&
             NativeDiffuseColor(missing,0x12345678u)==std::array<unsigned char,4>{{255,255,255,255}},
             "Missing/invalid DDS did not preserve source identity with the actual checker encoding");
        Need(accepted.supported&&!accepted.image.empty(),"DDS qualification mutated a caller's admitted profile");
    }
    Need(reads==3,"DDS qualification did not inspect each selected source exactly once");
    Need(!BuildDiffuseMaterial(path, white.material, &catalog).supported,
         "unqualified name-only request accepted VertexColor");
    // Distinct raw byte positions, including alpha, leave the identity variant
    // and enable the verified source-colour stage instead of inheriting white.
    for (std::uint32_t word : {0xfffffffeu, 0xfffffeffu, 0xfffeffffu, 0xfeffffffu, 0x12345678u}) {
        RenderSurface colored = white;
        colored.vertices[1].diffuseColor = word;
        Need(!HasIdentityWhiteReferencedColor(colored), "nonwhite referenced byte accepted");
        const DiffuseMaterial modulated = BuildDiffuseMaterial(path, colored, &catalog);
        Need(modulated.supported && !modulated.identityWhiteVertexColor && modulated.nativeVertexColor &&
             modulated.name != accepted.name && modulated.declaration.find("vertexColor") != std::string::npos,
             "coloured draw inherited identity-white material or omitted its colour stage");
    }
    RenderSurface referenced = white;
    referenced.indices = {0,1,3};
    Need(!HasIdentityWhiteReferencedColor(referenced), "referenced coloured vertex ignored");
    const auto modulated = BuildDiffuseMaterial(path, referenced, &catalog);
    Need(modulated.name == "wolf2009/synthetic/materials-vertex-color/synthetic/vertex-modulated",
         "source-colour profile has no separate cache namespace");
    CheckSourceColor(modulated, 0x11223344u, {{0x44,0x33,0x22,0x11}});
    CheckSourceColor(modulated, 0x80007fffu, {{255,127,0,128}});
    CheckSourceColor(modulated, 0x00abcdefu, {{0xef,0xcd,0xab,0}});
    CheckSourceColor(modulated, 0xffffffffu, {{255,255,255,255}});
    CheckSourceColor(modulated, 0u, {{0,0,0,0}});
    Need(NativeDiffuseColor(accepted, 0x11223344u) == std::array<unsigned char,4>{{255,255,255,255}},
         "identity variant uploaded an unused coloured source word");
    RenderSurface another = referenced;
    another.vertices[3].diffuseColor = 0xff123456u;
    Need(BuildDiffuseMaterial(path, another, &catalog).name == modulated.name,
         "raw colour values created unnecessary per-colour native declarations");
    for (bool whiteFirst : {false, true}) {
        std::map<std::string, DiffuseMaterial> cache;
        const RenderSurface* order[] = {whiteFirst ? &white : &referenced, whiteFirst ? &referenced : &white};
        for (const RenderSurface* source : order) {
            const DiffuseMaterial material = BuildDiffuseMaterial(path, *source, &catalog);
            const auto entry = cache.insert(std::make_pair(material.name, material)).first;
            Need(entry->second.supported &&
                 entry->second.identityWhiteVertexColor == HasIdentityWhiteReferencedColor(*source) &&
                 entry->second.nativeVertexColor != HasIdentityWhiteReferencedColor(*source),
                 "material cache order changed geometry qualification");
        }
        Need(cache.size() == 2, "identity and modulated variants share a material cache key");
    }
    RenderSurface alpha = white;
    alpha.material = "synthetic/alpha-modulated";
    const auto alphaMaterial = BuildDiffuseMaterial(path, alpha, &catalog);
    Need(alphaMaterial.supported && alphaMaterial.identityWhiteVertexColor &&
         alphaMaterial.declaration.find("alphaTest 0.5") != std::string::npos,
         "identity-white profile lost authored alpha test");
    alpha.vertices[1].diffuseColor = 0xff223344u;
    const auto coloredAlpha = BuildDiffuseMaterial(path, alpha, &catalog);
    Need(coloredAlpha.supported && coloredAlpha.nativeVertexColor &&
         coloredAlpha.declaration.find("alphaTest 0.5") != std::string::npos,
         "source-colour profile lost authored alpha test");
    CheckSourceColor(coloredAlpha, alpha.vertices[1].diffuseColor, {{0x44,0x33,0x22,255}});
    for (std::uint32_t color : {0xfe223344u,0x11223344u,0x00223344u}) {
        alpha.vertices[1].diffuseColor = color;
        const auto rejected = BuildDiffuseMaterial(path, alpha, &catalog);
        Need(!rejected.supported && !rejected.nativeVertexColor &&
             rejected.reason.find("vertex alpha") != std::string::npos,
             "source alpha-modulated cutout inherited texture-only native alpha test");
    }
    RenderSurface detail = referenced;
    detail.material = "synthetic/detail";
    Need(BuildDiffuseMaterial(path, detail, &catalog).nativeVertexColor,
         "verified detail/depth GBuffer profile omitted source colour");
    RenderSurface plain = referenced;
    plain.material = "synthetic/unmodulated";
    const auto unmodulated = BuildDiffuseMaterial(path, plain, &catalog);
    Need(unmodulated.supported && !unmodulated.nativeVertexColor && !unmodulated.identityWhiteVertexColor &&
         NativeDiffuseColor(unmodulated, 0x11223344u) == std::array<unsigned char,4>{{255,255,255,255}},
         "material without VertexColor used source colour");
    // Explicit expressions, including an apparent constant identity, stay out
    // of every simple-diffuse profile. No expression interpreter is implied.
    for (const char* name : {"synthetic/tint", "synthetic/add", "synthetic/no-vertex-tint"}) {
        for (bool identity : {false,true}) {
            RenderSurface custom = identity ? white : referenced;
            custom.material = name;
            const auto rejected = BuildDiffuseMaterial(path, custom, &catalog);
            Need(!rejected.supported && !rejected.nativeVertexColor && !rejected.identityWhiteVertexColor &&
                 rejected.image.empty() && rejected.declaration.find("_default") != std::string::npos,
                 "custom colour expression inherited a default diffuse profile");
            Need(NativeDiffuseColor(rejected, 0x11223344u) == std::array<unsigned char,4>{{255,255,255,255}},
                 "unsupported colour expression decoded native vertex colours");
        }
    }
    RenderSurface unknown = white;
    unknown.material = "synthetic/unknown";
    Need(!BuildDiffuseMaterial(path, unknown, &catalog).supported,
         "identity-white profile enabled unsupported shader composition");
    const auto unsupported=BuildDiffuseMaterial(path,unknown,&catalog);
    const auto retained=QualifyDiffuseMaterial(unsupported,[&](const std::string&){++reads;return AuthoredDiffuse();});
    Need(reads==3&&!retained.supported&&retained.reason==unsupported.reason&&retained.declaration==unsupported.declaration,
         "DDS qualification read resources or changed the reason for an unsupported source shader");
    RenderSurface empty = white;
    empty.indices.clear();
    Need(!HasIdentityWhiteReferencedColor(empty), "empty draw qualified as identity-white");
    Need(!BuildDiffuseMaterial(path, empty, &catalog).supported, "empty draw enabled VertexColor");
    RenderSurface invalid = white;
    invalid.indices[2] = 100;
    bool rejected = false;
    try { HasIdentityWhiteReferencedColor(invalid); } catch (const std::exception&) { rejected = true; }
    Need(rejected, "out-of-range colour qualification index accepted");
    return 0;
}
}
}
