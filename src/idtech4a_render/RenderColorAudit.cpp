// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only owner-supplied corpus audit. Reports raw words, never assumes RGBA.
#include "RenderData.h"
#include "StreamMounts.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace {
using namespace wolf2009;
using namespace wolf2009::idtech4a;
void Need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Bytes ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    Need(bool(file), "could not open owner-supplied input");
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    Need(size >= 0 && size <= 64u * 1024u * 1024u, "audit input outside bounded range");
    file.seekg(0);
    Bytes data(static_cast<std::size_t>(size));
    if (size) file.read(reinterpret_cast<char*>(data.data()), size);
    Need(bool(file), "short audit input");
    return data;
}
std::string Json(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    out << '"';
    return out.str();
}
std::string Hex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}
bool EqualBytes(std::uint32_t value) { return value == (value & 255u) * 0x01010101u; }
struct Stats {
    std::size_t surfaces = 0, vertices = 0, triangles = 0;
    std::size_t white = 0, equal = 0, other = 0;
    std::map<std::uint32_t, std::size_t> words;
    void Add(const RenderSurface& surface) {
        ++surfaces; vertices += surface.vertices.size(); triangles += surface.indices.size() / 3;
        for (const auto& vertex : surface.vertices) {
            const std::uint32_t word = vertex.diffuseColor;
            ++words[word];
            if (word == 0xffffffffu) ++white;
            if (EqualBytes(word)) ++equal;
            else ++other;
        }
    }
    bool AllWhite() const { return vertices && white == vertices; }
    bool AllEqualBytes() const { return vertices && equal == vertices; }
};
void Print(const char* kind, const std::string& map, const std::string& name,
           const Stats& stats, const MaterialResult* binding = NULL) {
    std::cout << "{\"kind\":" << Json(kind) << ",\"map\":" << Json(map)
              << ",\"name\":" << Json(name) << ",\"surfaces\":" << stats.surfaces
              << ",\"vertices\":" << stats.vertices << ",\"triangles\":" << stats.triangles
              << ",\"white_words\":" << stats.white << ",\"equal_byte_words\":" << stats.equal
              << ",\"other_words\":" << stats.other << ",\"unique_words\":" << stats.words.size();
    if (binding) {
        std::cout << ",\"vertex_color\":" << (binding->vertexColor ? "true" : "false")
                  << ",\"resolved\":" << (binding->resolved ? "true" : "false")
                  << ",\"shader_class\":" << int(binding->shaderClass)
                  << ",\"technique\":" << Json(binding->technique)
                  << ",\"diffuse\":" << Json(binding->diffusePath)
                  << ",\"reason\":" << Json(binding->reason);
    }
    // A bounded exact histogram is enough to inspect neutral candidates. Large
    // coloured inputs retain complete counts and explicitly omit their words.
    std::cout << ",\"word_histogram_omitted\":" << (stats.words.size() > 64 ? "true" : "false");
    if (stats.words.size() <= 64) {
        std::cout << ",\"word_histogram\":{";
        bool first = true;
        for (const auto& word : stats.words) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << Json(Hex(word.first)) << ':' << word.second;
        }
        std::cout << '}';
    }
    std::cout << "}\n";
}
void Map(const std::string& base, const char* label, const char* package, const MaterialCatalog& catalog) {
    StreamMounts mounts([&](const std::string&) {
        return std::unique_ptr<std::istream>(new std::ifstream(base + "/" + package, std::ios::binary));
    });
    mounts.Mount(label);
    std::ifstream file(base + "/" + package, std::ios::binary);
    const Archive archive = Archive::Scan(file);
    std::string proc;
    for (const auto& chunk : archive.Chunks()) if (chunk.type == "PROC") {
        for (const auto& entry : chunk.entries) { Need(proc.empty(), "multiple PROC resources"); proc = entry.name; }
    }
    Need(!proc.empty(), "missing PROC resource");
    const ResourcePath path = ParseResourcePath(std::string("wolf2009/") + label + "/PROC/" + proc);
    const ResourceReader read = [&](const std::string& virtualName) {
        const auto selected = ParseResourcePath(virtualName);
        Need(selected.mount == label, "audit escaped selected source");
        std::string name = selected.name;
        const auto exact = mounts.Find(selected.type, name);
        if (exact.status == StreamLookupStatus::Unique) return mounts.Read(exact.matches[0]);
        const std::string suffix = selected.type == "proc" ? ".proc" : selected.type == "txtr" ? ".dds" : ".md5rbin";
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            name.resize(name.size() - suffix.size());
        return mounts.ReadUnique(selected.type, name);
    };
    const auto scene = LoadGeometry(path, read);
    const auto surfaces = CompactSurfaces(scene);
    Stats world, vertexColor, neutral, invariant, colored;
    std::map<std::string, Stats> perMaterial;
    for (const auto& surface : surfaces) {
        if (surface.model.compare(0, 5, "_area") != 0) continue;
        world.Add(surface);
        perMaterial[surface.material].Add(surface);
        const auto binding = catalog.Resolve(surface.material);
        if (binding.vertexColor) {
            vertexColor.Add(surface);
            Stats single; single.Add(surface);
            if (single.AllWhite()) neutral.Add(surface);
            else if (single.AllEqualBytes()) invariant.Add(surface);
            else colored.Add(surface);
            Print("surface", label, surface.material, single, &binding);
        }
    }
    Print("summary", label, "world", world);
    Print("summary", label, "vertex-color", vertexColor);
    Print("summary", label, "all-white-surfaces", neutral);
    Print("summary", label, "nonwhite-order-invariant-surfaces", invariant);
    Print("summary", label, "channel-order-dependent-surfaces", colored);
    for (const auto& material : perMaterial) {
        const auto binding = catalog.Resolve(material.first);
        Print("material", label, material.first, material.second, &binding);
    }
}
}
int main(int argc, char** argv) {
    try {
        Need(argc == 2, "usage: wolf2009-render-audit OWNER_SP_BASE (read-only; JSONL to stdout)");
        const std::string base = argv[1];
        const auto catalog = ReadMaterialCatalog(ReadFile(base + "/assets.pk4"));
        Map(base, "empty", "maps/game/empty/empty.mpk", *catalog);
        Map(base, "trainyard", "maps/game/trainyard/trainyard.mpk", *catalog);
        Map(base, "mtw_ware", "maps/game/mtw/mtw_ware.mpk", *catalog);
        return 0;
    } catch (const std::exception& error) { std::cerr << "RENDER_COLOR_AUDIT_FAIL " << error.what() << '\n'; return 1; }
}
