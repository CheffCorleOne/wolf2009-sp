// SPDX-License-Identifier: GPL-3.0-or-later
#include "Geometry.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace wolf2009 { namespace {
typedef std::vector<unsigned char> Bytes;
void Assert(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void Float(Bytes& bytes, std::size_t offset, float value) {
    std::uint32_t bits; std::memcpy(&bits, &value, 4);
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<unsigned char>(bits >> (i * 8));
}
struct Fixture {
    std::string source;
    std::map<std::string, Bytes> buffers;
    explicit Fixture(bool packed = true) {
        const std::string format = packed ? "Position 4 Normal Dec_11_11_10N Tangent Dec_11_11_10N Binormal Dec_11_11_10N DiffuseColor TexCoord 2 0" :
                                            "Position 4 Normal Tangent Binormal DiffuseColor TexCoord 2 0";
        source = "MemSize 1\nMD5RProcVersion 7\n123\n"
                 "VertexBuffer[1]{VertexBuffer{VertexFormat{" + format + "} SystemMemory VideoMemory Vertex[4]{file \"world_vb1.MD5RBin\"}}}"
                 "IndexBuffer[1]{IndexBuffer{SystemMemory BitDepth 16 Index[3]{file \"world_ib1.MD5RBin\"}}}"
                 "Model[1]{Model \"_area0\"{Mesh[1]{Mesh{Material \"synthetic/material\" DrawBuffers 0 0 PrimBatch[1]{PrimBatch{DrawIndexedTriList 1 3 0 1}} Bounds 0 0 0 1 1 0}} Bounds 0 0 0 1 1 0}}\n"
                 "nodes{1 bin 24\n";
        source.append("{\0\"\n]}\0synthetic\0opaque!!", 24);
        source += "\n}\n";
        const std::size_t stride = packed ? 40 : 64, uv = packed ? 32 : 56;
        Bytes vertices(4 * stride, 0);
        const float points[4][3] = {{99,99,0},{0,0,0},{1,0,0},{0,1,0}};
        for (unsigned v = 0; v < 4; ++v) {
            for (unsigned c = 0; c < 3; ++c) Float(vertices, v * stride + c * 4, points[v][c]);
            Float(vertices, v * stride + 12, 1); Float(vertices, v * stride + uv, float(v) * 0.25f);
        }
        buffers["synthetic/world_vb1"] = vertices;
        buffers["synthetic/world_ib1"] = Bytes{1,0,2,0,3,0};
    }
    GeometryScene Decode(const GeometryLimits& limits = GeometryLimits()) {
        const Bytes bytes(source.begin(), source.end());
        return DecodeProc7(bytes, "synthetic/world", [&](const std::string& name) {
            const std::map<std::string, Bytes>::const_iterator found = buffers.find(name);
            if (found == buffers.end()) throw std::runtime_error("Missing synthetic resource"); return found->second;
        }, limits);
    }
    void Replace(const std::string& from, const std::string& to) {
        const std::size_t at = source.find(from); Assert(at != std::string::npos, "Fixture edit missing"); source.replace(at, from.size(), to);
    }
};
template<class F> void Reject(F operation) {
    bool rejected = false; try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    Assert(rejected, "Malformed geometry was accepted");
}
}

int RunGeometryTests() {
    unsigned passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& operation) {
        try { operation(); ++passed; }
        catch (const std::exception& error) { ++failed; std::cerr << "geometry test " << name << ": " << error.what() << '\n'; }
    };
    test("packed positions, UV and absolute indices", [] {
        Fixture f; const GeometryScene s = f.Decode(); Assert(s.models == 1 && s.vertices.size() == 4 && s.surfaces.size() == 1, "Wrong scene counts");
        Assert(s.surfaces[0].indices == std::vector<std::uint32_t>({1,2,3}), "minVertex was incorrectly added to absolute indices");
        Assert(s.vertices[2].x == 1 && s.vertices[2].u == 0.5f && s.surfaces[0].material == "synthetic/material", "Position or UV/material lost");
    });
    test("unpacked 64-byte vertices", [] { Fixture f(false); const GeometryScene s = f.Decode(); Assert(s.vertices[3].y == 1 && s.vertices[3].u == 0.75f, "Wrong unpacked vertex stride or UV offset"); });
    test("opaque binary tail", [] { Fixture f; Assert(f.Decode().surfaces.size() == 1, "Binary tail interpreted as text"); });
    test("version and syntax rejection", [] {
        Fixture f; f.Replace("MD5RProcVersion 7", "MD5RProcVersion 8"); Reject([&] { f.Decode(); });
        Fixture g; g.source.resize(g.source.size() - 2); Reject([&] { g.Decode(); });
        Fixture h; h.Replace("bin 24", "bin 240"); Reject([&] { h.Decode(); });
        Fixture j; j.source += "ZeroBasedIndices\n"; Reject([&] { j.Decode(); });
    });
    test("declared count and draw range rejection", [] {
        Fixture f; f.Replace("VertexBuffer[1]", "VertexBuffer[2]"); Reject([&] { f.Decode(); });
        Fixture g; g.Replace("1 3 0 1", "1 3 0 2"); Reject([&] { g.Decode(); });
        Fixture h; h.buffers["synthetic/world_ib1"][0] = 0; Reject([&] { h.Decode(); });
    });
    test("missing, truncated and non-finite buffers", [] {
        Fixture f; f.buffers.erase("synthetic/world_vb1"); Reject([&] { f.Decode(); });
        Fixture g; g.buffers["synthetic/world_vb1"].pop_back(); Reject([&] { g.Decode(); });
        Fixture h; Float(h.buffers["synthetic/world_vb1"], 40, (std::numeric_limits<float>::infinity)()); Reject([&] { h.Decode(); });
    });
    test("bounds, unsupported formats and path rejection", [] {
        Fixture f; f.Replace("Bounds 0 0 0 1 1 0", "Bounds 0 0 0 0 0 0"); Reject([&] { f.Decode(); });
        Fixture g; g.Replace("Position 4", "Position 3"); Reject([&] { g.Decode(); });
        Fixture h; h.Replace("world_vb1.MD5RBin", "../world_vb1.MD5RBin"); Reject([&] { h.Decode(); });
    });
    test("resource and syntax limits", [] {
        Fixture f; GeometryLimits limits; limits.vertices = 3; Reject([&] { f.Decode(limits); });
        limits = GeometryLimits(); limits.syntaxNodes = 10; Reject([&] { f.Decode(limits); });
        limits = GeometryLimits(); limits.totalBufferBytes = 159; Reject([&] { f.Decode(limits); });
    });
    test("non-render shadow batch is counted, not fabricated", [] {
        Fixture f; f.Replace("DrawIndexedTriList", "SilTraceIndexedTriList"); const GeometryScene s = f.Decode();
        Assert(s.surfaces.empty() && s.vertices.empty() && s.skippedMeshes == 1, "Shadow-only mesh became visible geometry");
    });
    std::cout << "wolf2009 geometry self-test: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
} // namespace wolf2009
