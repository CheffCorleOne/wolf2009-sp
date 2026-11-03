// SPDX-License-Identifier: GPL-3.0-or-later
#include "RenderData.h"
#include "ZipArchive.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace wolf2009 {
namespace idtech4a {
namespace {
void Need(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string Virtual(std::string text) {
    Need(!text.empty() && text.size() <= 4096, "invalid renderer resource path length");
    for (char& c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        Need(byte >= 32 && byte < 127 && c != ':' && c != '"' && c != '#' &&
             c != '(' && c != ')' && c != '{' && c != '}' && c != ',',
             "unsafe renderer resource path byte");
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    Need(text.front() != '/' && text.back() != '/', "absolute or empty renderer path");
    std::size_t begin = 0;
    do {
        const std::size_t end = text.find('/', begin);
        const std::string part = text.substr(begin, end - begin);
        Need(!part.empty() && part != "." && part != "..", "unsafe renderer path component");
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
    return text;
}
bool Ends(const std::string& s, const char* suffix) {
    const std::string tail(suffix);
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
}
std::string ResourcePath::Prefix() const { return "wolf2009/" + mount + "/"; }
std::string ResourcePath::Path(const std::string& resourceType, const std::string& resourceName) const {
    return Prefix() + Virtual(resourceType) + "/" + Virtual(resourceName);
}
ResourcePath ParseResourcePath(const std::string& path) {
    const std::string value = Virtual(path), prefix = "wolf2009/";
    Need(value.compare(0, prefix.size(), prefix) == 0, "not a Wolf2009 renderer path");
    const std::size_t mountEnd = value.find('/', prefix.size()), typeEnd = value.find('/', mountEnd + 1);
    Need(mountEnd != std::string::npos && typeEnd != std::string::npos,
         "renderer path must include a mount, type and name");
    ResourcePath result;
    result.mount = value.substr(prefix.size(), mountEnd - prefix.size());
    result.type = value.substr(mountEnd + 1, typeEnd - mountEnd - 1);
    Need(result.type.size() == 4, "renderer path needs an explicit FourCC");
    result.name = value.substr(typeEnd + 1);
    return result;
}
bool HasResourceType(const char* path, const char* type) {
    if (!path || !type) return false;
    try { return ParseResourcePath(path).type == Virtual(type); }
    catch (const std::exception&) { return false; }
}
GeometryScene LoadGeometry(const ResourcePath& source, const ResourceReader& read) {
    Need(bool(read), "renderer has no resource reader");
    const BufferResolver buffers = [&](const std::string& name) {
        return read(source.Path("PBBF", name));
    };
    if (source.type == "proc") {
        const std::string name = Ends(source.name, ".proc") ? source.name : source.name + ".proc";
        return DecodeProc7(read(source.Path("PROC", name)), name, buffers);
    }
    Need(source.type == "modl", "renderer only accepts PROC and static MODL geometry");
    const std::string name = Ends(source.name, ".md5r") ? source.name : source.name + ".md5r";
    return DecodeStaticModel1(read(source.Path("MODL", name)), name, buffers);
}
std::vector<RenderSurface> CompactSurfaces(const GeometryScene& scene) {
    std::vector<RenderSurface> result;
    const std::uint32_t unused = (std::numeric_limits<std::uint32_t>::max)();
    std::vector<std::uint32_t> remap(scene.vertices.size(), unused), touched;
    result.reserve(scene.surfaces.size());
    for (const GeometrySurface& surface : scene.surfaces) {
        if (surface.indices.empty()) continue;
        RenderSurface out;
        out.model = surface.model;
        out.material = surface.material;
        out.indices.reserve(surface.indices.size());
        Need(surface.indices.size() % 3 == 0, "native draw is not a triangle list");
        for (std::uint32_t index : surface.indices) {
            Need(index < scene.vertices.size(), "native draw index outside decoded scene");
            if (remap[index] == unused) {
                Need(out.vertices.size() < unused, "native draw vertex count overflow");
                remap[index] = static_cast<std::uint32_t>(out.vertices.size());
                touched.push_back(index);
                out.vertices.push_back(scene.vertices[index]);
            }
            out.indices.push_back(remap[index]);
        }
        for (std::uint32_t index : touched) remap[index] = unused;
        touched.clear();
        result.push_back(std::move(out));
    }
    return result;
}
std::unique_ptr<MaterialCatalog> ReadMaterialCatalog(const Bytes& archive) {
    Need(archive.size() <= 64u * 1024u * 1024u, "material archive exceeds bridge budget");
    const std::string text(archive.begin(), archive.end());
    std::istringstream input(text, std::ios::in | std::ios::binary);
    const ZipArchive zip = ZipArchive::Scan(input);
    std::unique_ptr<MaterialCatalog> result(new MaterialCatalog);
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < zip.Entries().size(); ++i) {
            std::string name = zip.Entries()[i].name;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
            if (!Ends(name, pass ? ".mtr" : ".guide")) continue;
            name = Virtual(name);
            const Bytes member = zip.ReadEntry(input, i);
            const std::string source(member.begin(), member.end());
            if (pass) result->AddMaterialSource(name, source);
            else result->AddGuideSource(name, source);
        }
    }
    return result;
}
bool HasIdentityWhiteReferencedColor(const RenderSurface& surface) {
    if (surface.indices.empty()) return false;
    Need(surface.indices.size() % 3 == 0, "colour qualification needs a triangle draw");
    bool identity = true;
    for (std::uint32_t index : surface.indices) {
        Need(index < surface.vertices.size(), "colour qualification index outside vertices");
        if (surface.vertices[index].diffuseColor != 0xffffffffu) identity = false;
    }
    return identity;
}
namespace {
DiffuseMaterial BuildMaterial(const ResourcePath& source, const std::string& name,
                              const MaterialCatalog* catalog, bool identityWhite) {
    DiffuseMaterial result;
    // Generated names are source-qualified: a different MPK/SPK cannot silently
    // replace this material's TXTR source in the global native declaration cache.
    if (catalog) result.binding = catalog->Resolve(name);
    result.reason = catalog ? result.binding.reason : "no material catalog configured";
    const MaterialShaderClass shader = result.binding.shaderClass;
    result.supported = result.binding.resolved &&
        (shader == MaterialShaderClass::OpaqueDiffuse || shader == MaterialShaderClass::AlphaTestDiffuse);
    result.identityWhiteVertexColor = result.supported && result.binding.vertexColor && identityWhite;
    result.name = source.Prefix() +
        (result.identityWhiteVertexColor ? "materials-identity-white/" : "materials/") + Virtual(name);
    if (result.supported) {
        result.image = source.Path("TXTR", MaterialCatalog::NormalizeVirtualPath(result.binding.diffusePath) + ".dds");
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << "{\n noShadows\n {\n map \"" << result.image << "\"\n";
        if (result.binding.hasAlphaTest) text << " alphaTest " << result.binding.alphaTest << "\n";
        // The only permitted simplification is a geometry-qualified constant
        // white identity. General source channel/normalization semantics stay
        // unverified; no coloured word is decoded or swizzled here.
        if (result.binding.vertexColor && !result.identityWhiteVertexColor) {
            result.supported = false;
            result.reason = "vertex-colour source channel layout is not verified";
        }
        text << " }\n}\n";
        result.declaration = text.str();
    }
    if (!result.supported) {
        result.image.clear();
        result.declaration = "{\n noShadows\n {\n map _default\n }\n}\n";
    }
    return result;
}
}
DiffuseMaterial BuildDiffuseMaterial(const ResourcePath& source, const std::string& name,
                                    const MaterialCatalog* catalog) {
    return BuildMaterial(source, name, catalog, false);
}
DiffuseMaterial BuildDiffuseMaterial(const ResourcePath& source, const RenderSurface& surface,
                                    const MaterialCatalog* catalog) {
    return BuildMaterial(source, surface.material, catalog, HasIdentityWhiteReferencedColor(surface));
}
}
}
