// SPDX-License-Identifier: GPL-3.0-or-later
#include "Archive.h"
#include "Geometry.h"
#include "MapPhysics.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace {
std::string Key(std::string value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '\\') value[i] = '/'; else if (c >= 'A' && c <= 'Z') value[i] = char(c + 32);
    }
    return value;
}
std::string CanonicalExisting(const std::string& path, bool directory) {
#ifdef _WIN32
    HANDLE handle = CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot resolve existing path");
    char buffer[32768];
    const DWORD count = GetFinalPathNameByHandleA(handle, buffer, sizeof(buffer), FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if (count == 0 || count >= sizeof(buffer)) throw std::runtime_error("Cannot canonicalize path");
    std::string result(buffer, count);
    if (result.compare(0, 4, "\\\\?\\") == 0) result.erase(0, 4);
    return result;
#else
    (void)directory;
    char* value = realpath(path.c_str(), NULL);
    if (!value) throw std::runtime_error("Cannot resolve existing path");
    const std::string result(value); free(value); return result;
#endif
}
std::string OutputPath(const std::string& input, const std::string& output) {
    const std::size_t slash = output.find_last_of("/\\");
    const std::string leaf = slash == std::string::npos ? output : output.substr(slash + 1);
    if (leaf.empty() || leaf == "." || leaf == ".." || leaf.find(':') != std::string::npos)
        throw std::runtime_error("Invalid output filename");
    const std::string parent = CanonicalExisting(slash == std::string::npos ? "." : output.substr(0, slash + 1), true);
    const std::string absolute = parent + "/" + leaf;
    const std::string canonicalInput = Key(CanonicalExisting(input, false)), canonicalOutput = Key(absolute);
    const std::size_t base = canonicalInput.find("/sp/base/");
    if (base != std::string::npos) {
        const std::string installation = canonicalInput.substr(0, base);
        if (canonicalOutput == installation || canonicalOutput.compare(0, installation.size() + 1, installation + "/") == 0)
            throw std::runtime_error("Output must be outside the original game installation");
    }
    if (canonicalOutput == canonicalInput) throw std::runtime_error("Output cannot replace input");
    return absolute;
}
std::string Label(const std::string& value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        out += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ? char(c) : '_';
    }
    return out.empty() ? "unnamed" : out;
}
void WriteExclusive(const std::string& output, const wolf2009::GeometryScene& scene) {
#ifdef _WIN32
    const int fd = _open(output.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = open(output.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
#endif
    if (fd < 0) throw std::runtime_error("Cannot create output exclusively; existing files are never overwritten");
#ifdef _WIN32
    FILE* file = _fdopen(fd, "wb");
#else
    FILE* file = fdopen(fd, "wb");
#endif
    if (!file) {
#ifdef _WIN32
        _close(fd);
#else
        close(fd);
#endif
        throw std::runtime_error("Cannot open output stream");
    }
    bool ok = std::fprintf(file, "# Wolf2009 PROCB7 static world geometry diagnostic export\n# Z up. No textures, entity placements, animation or gameplay.\n") >= 0;
    for (std::size_t v = 0; ok && v < scene.vertices.size(); ++v) {
        const wolf2009::GeometryVertex& vertex = scene.vertices[v];
        ok = std::fprintf(file, "v %.9g %.9g %.9g\n", double(vertex.x), double(vertex.y), double(vertex.z)) >= 0;
    }
    for (std::size_t v = 0; ok && v < scene.vertices.size(); ++v)
        ok = std::fprintf(file, "vt %.9g %.9g\n", double(scene.vertices[v].u), double(scene.vertices[v].v)) >= 0;
    for (std::size_t s = 0; ok && s < scene.surfaces.size(); ++s) {
        const wolf2009::GeometrySurface& surface = scene.surfaces[s];
        ok = std::fprintf(file, "g %s_%llu\nusemtl %s\n", Label(surface.model).c_str(), static_cast<unsigned long long>(s), Label(surface.material).c_str()) >= 0;
        for (std::size_t i = 0; ok && i < surface.indices.size(); i += 3)
            ok = std::fprintf(file, "f %u/%u %u/%u %u/%u\n", surface.indices[i] + 1, surface.indices[i] + 1,
                              surface.indices[i + 1] + 1, surface.indices[i + 1] + 1,
                              surface.indices[i + 2] + 1, surface.indices[i + 2] + 1) >= 0;
    }
    const bool closed = std::fclose(file) == 0;
    if (!ok || !closed) throw std::runtime_error("Output write failed; partial diagnostic file was retained");
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        const int geometry = wolf2009::RunGeometryTests();
        const int starts = wolf2009::RunPlayerStartTests();
        return geometry || starts ? 1 : 0;
    }
    const bool validate = argc == 3 && std::string(argv[1]) == "--validate";
    const bool includeSubmodels = argc == 4 && std::string(argv[1]) == "--include-submodels";
    const bool playerStarts = (argc == 3 || argc == 4) && std::string(argv[1]) == "--player-starts";
    if (argc != 3 && !includeSubmodels && !playerStarts) {
        std::cerr << "wolf2009-scene INPUT.mpk OUTPUT.obj\nwolf2009-scene --validate INPUT.mpk\n"
                     "wolf2009-scene --player-starts INPUT.mpk [ENTITY_NAME]\n"
                     "wolf2009-scene --include-submodels INPUT.mpk OUTPUT.obj\nwolf2009-scene --self-test\n"
                     "Read-only PROC7/PBBF static world geometry export; never overwrites output.\n";
        return 2;
    }
    try {
        const char* source = argv[(validate || includeSubmodels || playerStarts) ? 2 : 1];
        const std::string output = (validate || playerStarts) ? "" : OutputPath(source, argv[includeSubmodels ? 3 : 2]);
        std::ifstream input(source, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open input archive");
        const wolf2009::Archive archive = wolf2009::Archive::Scan(input);
        struct Location { std::size_t chunk, entry; };
        std::map<std::string, Location> buffers;
        Location proc = {0, 0}; std::string resourceName; unsigned procCount = 0;
        for (std::size_t c = 0; c < archive.Chunks().size(); ++c) {
            const wolf2009::Chunk& chunk = archive.Chunks()[c];
            if (chunk.type != "PROC" && chunk.type != "PBBF") continue;
            for (std::size_t e = 0; e < chunk.entries.size(); ++e) {
                Location location = {c, e};
                if (chunk.type == "PROC") { ++procCount; proc = location; resourceName = chunk.entries[e].name; }
                else if (!buffers.insert(std::make_pair(Key(chunk.entries[e].name), location)).second)
                    throw std::runtime_error("Ambiguous duplicate PBBF resource name");
            }
        }
        if (procCount != 1) throw std::runtime_error("Expected exactly one PROC resource in input MPK");
        const wolf2009::BufferResolver resolver = [&](const std::string& name) {
            const std::map<std::string, Location>::const_iterator entry = buffers.find(Key(name));
            if (entry == buffers.end()) throw std::runtime_error("Missing PBBF render buffer: " + name);
            return archive.ReadEntry(input, entry->second.chunk, entry->second.entry);
        };
        wolf2009::GeometryScene scene = wolf2009::DecodeProc7(archive.ReadEntry(input, proc.chunk, proc.entry), resourceName, resolver);
        if (playerStarts) {
            const auto candidates = wolf2009::ReadMapPlayerStarts(input, archive, resourceName);
            const auto choice = wolf2009::SelectDiagnosticPlayerStart(candidates, argc == 4 ? argv[3] : "");
            std::cout << "Candidates: " << candidates.size() << "\nSelection: "
                      << wolf2009::PlayerStartSelectionReasonText(choice.reason) << '\n';
            for (const auto& start : candidates)
                std::cout << start.entityIndex << ' ' << Label(start.placement.classname) << ' '
                          << Label(start.placement.name) << '\n';
            if (choice.selected) {
                const auto& start = candidates.at(choice.candidateIndex);
                const auto support = wolf2009::AssessPlayerStart(start, scene);
                std::cout << "Selected: " << Label(start.placement.name) << "\nRender support: "
                          << wolf2009::PlayerStartSupportStatusText(support.status) << '\n';
                if (support.hasHit) std::cout << "World surface Z: " << support.point[2]
                                              << "\nVertical gap: " << support.verticalGap << '\n';
            }
            return 0;
        }
        const std::size_t allVertices = scene.vertices.size();
        std::size_t triangles = 0;
        for (std::size_t s = 0; s < scene.surfaces.size(); ++s) triangles += scene.surfaces[s].indices.size() / 3;
        const std::size_t allSurfaces = scene.surfaces.size();
        std::size_t exportedTriangles = 0;
        if (!validate && !includeSubmodels) {
            // PROC submodels are in local space. They require ENTS placements;
            // displaying them at the origin would fabricate a world transform.
            scene.surfaces.erase(std::remove_if(scene.surfaces.begin(), scene.surfaces.end(),
                [](const wolf2009::GeometrySurface& surface) { return surface.model.compare(0, 5, "_area") != 0; }), scene.surfaces.end());
        }
        for (std::size_t s = 0; s < scene.surfaces.size(); ++s) exportedTriangles += scene.surfaces[s].indices.size() / 3;
        if (!validate) {
            // Compact AFTER selecting world surfaces: unused local-space prop
            // vertices must not alter exported scene bounds or auto-fit cameras.
            const std::uint32_t absent = (std::numeric_limits<std::uint32_t>::max)();
            std::vector<std::uint32_t> remap(scene.vertices.size(), absent);
            std::vector<wolf2009::GeometryVertex> used;
            for (std::size_t s = 0; s < scene.surfaces.size(); ++s) {
                std::vector<std::uint32_t>& indices = scene.surfaces[s].indices;
                for (std::size_t i = 0; i < indices.size(); ++i) {
                    const std::uint32_t original = indices[i];
                    if (remap[original] == absent) {
                        remap[original] = static_cast<std::uint32_t>(used.size());
                        used.push_back(scene.vertices[original]);
                    }
                    indices[i] = remap[original];
                }
            }
            scene.vertices.swap(used);
        }
        if (!validate) WriteExclusive(output, scene);
        std::cout << "{\"models\":" << scene.models << ",\"vertices\":" << allVertices
                  << ",\"surfaces\":" << allSurfaces << ",\"triangles\":" << triangles
                  << ",\"skippedMeshes\":" << scene.skippedMeshes << ",\"exportedSurfaces\":" << (validate ? 0 : scene.surfaces.size())
                  << ",\"exportedTriangles\":" << (validate ? 0 : exportedTriangles)
                  << ",\"exportedVertices\":" << (validate ? 0 : scene.vertices.size())
                  << ",\"submodelsIncluded\":" << (includeSubmodels ? "true" : "false") << "}\n";
        if (includeSubmodels) std::cerr << "Diagnostic submodels are in local space; ENTS placements are not applied.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Geometry export failed: " << error.what() << '\n'; return 1;
    }
}
