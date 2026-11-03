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
struct ModelFixture : Fixture {
    explicit ModelFixture(bool packedLoad = true, bool external = false) : Fixture(packedLoad) {
        const std::string runtime = "Position 4 Normal Tangent Binormal DiffuseColor TexCoord 2 0";
        const std::string stored = "Position 4 Normal Dec_10_10_10N Tangent Dec_10_10_10N Binormal Dec_10_10_10N DiffuseColor TexCoord 2 0";
        source = "MemSize 1\nMD5RVersion 1\nVertexBuffer[1]{VertexBuffer{VertexFormat{" + runtime + "}";
        if (packedLoad) source += "LoadVertexFormat{" + stored + "}";
        source += "SystemMemory VideoMemory Vertex[4]{";
        const Bytes& vb = buffers["synthetic/world_vb1"];
        if (external) source += "file \"world_vb1.MD5RBin\"";
        else {
            source += "bin " + std::to_string(vb.size()) + "\n";
            if ((source.size() & 3) == 0) source.insert(0, " ");
            source.append(reinterpret_cast<const char*>(vb.data()), vb.size());
        }
        source += "}}}IndexBuffer[1]{IndexBuffer{BitDepth 16 Index[3]{";
        const Bytes& ib = buffers["synthetic/world_ib1"];
        if (external) source += "file \"world_ib1.MD5RBin\"";
        else { source += "bin 6\n"; source.append(reinterpret_cast<const char*>(ib.data()), ib.size()); }
        source += "}}}Mesh[1]{Mesh{Material \"synthetic/static-model\" DrawBuffers 0 0 PrimBatch[1]{PrimBatch{DrawIndexedTriList 1 3 0 1}} Bounds 0 0 0 1 1 0}}Bounds 0 0 0 1 1 0\n";
    }
    GeometryScene DecodeModel(const GeometryLimits& limits = GeometryLimits()) {
        const Bytes bytes(source.begin(), source.end());
        return DecodeStaticModel1(bytes, "synthetic/model", [&](const std::string& name) {
            const std::map<std::string, Bytes>::const_iterator found = buffers.find(name);
            if (found == buffers.end()) throw std::runtime_error("Missing synthetic resource"); return found->second;
        }, limits);
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
    test("raw DiffuseColor preservation in both supported layouts", [] {
        for (unsigned layout = 0; layout < 2; ++layout) {
            Fixture f(layout == 0);
            const std::size_t stride = layout == 0 ? 40 : 64, offset = layout == 0 ? 28 : 52;
            Bytes& vb = f.buffers["synthetic/world_vb1"];
            // Distinct source bytes exercise raw LE preservation rather than
            // accidentally passing with white, grey or symmetric R/B channels.
            vb[stride + offset] = 0x78; vb[stride + offset + 1] = 0x56;
            vb[stride + offset + 2] = 0x34; vb[stride + offset + 3] = 0x12;
            const GeometryScene scene = f.Decode();
            Assert(scene.vertices[1].diffuseColor == 0x12345678u, "Raw DiffuseColor offset/order changed");
            Assert(scene.vertices[1].x == 0 && scene.vertices[1].u == .25f &&
                   scene.vertices[2].diffuseColor == 0, "Colour read damaged neighbouring vertex data");
        }
    });
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
    test("static MD5R packed LoadVertexFormat and unaligned inline bytes", [] {
        ModelFixture f; const std::size_t marker = f.source.find("bin 160\n");
        Assert(marker != std::string::npos && ((marker + 8) & 3) != 0, "Fixture did not exercise unaligned data");
        const GeometryScene s = f.DecodeModel();
        Assert(s.models == 1 && s.vertices.size() == 4 && s.surfaces.size() == 1, "Wrong static model counts");
        Assert(s.vertices[2].x == 1 && s.vertices[2].u == .5f && s.surfaces[0].indices == std::vector<std::uint32_t>({1,2,3}),
               "Static stored format or absolute indices were decoded incorrectly");
        Assert(s.surfaces[0].model == "synthetic/model" && s.surfaces[0].material == "synthetic/static-model", "Static model identity was lost");
    });
    test("static unpacked inline and external packed data", [] {
        ModelFixture f(false); Assert(f.DecodeModel().vertices[3].u == .75f, "Unpacked static stride failed");
        ModelFixture g(true, true); Assert(g.DecodeModel().vertices[2].x == 1, "External static buffer failed");
    });
    test("static version boundary", [] {
        ModelFixture f; f.Replace("MD5RVersion 1", "MD5RVersion 2"); Reject([&] { f.DecodeModel(); });
        ModelFixture g; Reject([&] { g.Decode(); });
        Fixture h; const Bytes bytes(h.source.begin(), h.source.end()); Reject([&] { DecodeStaticModel1(bytes, "synthetic/model"); });
    });
    test("skeletal and skinning data rejected explicitly", [] {
        ModelFixture f; f.source.insert(0, "Joint[0]{}\n"); Reject([&] { f.DecodeModel(); });
        ModelFixture g; g.Replace("Position 4 Normal", "Position 4 BlendIndex Normal"); Reject([&] { g.DecodeModel(); });
        ModelFixture h; h.Replace("Position 4 Normal Dec", "Position 4 BlendWeight 3 4 Normal Dec"); Reject([&] { h.DecodeModel(); });
    });
    test("invalid static stored layout and payload", [] {
        ModelFixture f; f.Replace("LoadVertexFormat{Position 4", "LoadVertexFormat{Position 3"); Reject([&] { f.DecodeModel(); });
        ModelFixture g; g.Replace("bin 160", "bin 159"); Reject([&] { g.DecodeModel(); });
        ModelFixture h; h.Replace("Bounds 0 0 0 1 1 0", "Bounds 0 0 0 0 0 0"); Reject([&] { h.DecodeModel(); });
    });
    test("static allocation limits and unresolved buffer", [] {
        ModelFixture f; GeometryLimits limits; limits.models = 0; Reject([&] { f.DecodeModel(limits); });
        limits = GeometryLimits(); limits.bufferBytes = 159; Reject([&] { f.DecodeModel(limits); });
        ModelFixture g(true, true); const Bytes bytes(g.source.begin(), g.source.end()); Reject([&] { DecodeStaticModel1(bytes, "synthetic/model"); });
    });
    test("world packed encoding boundary preserved", [] {
        Fixture f;
        const std::string from = "Dec_11_11_10N", to = "Dec_10_10_10N";
        for (std::size_t at = f.source.find(from); at != std::string::npos; at = f.source.find(from)) f.source.replace(at, from.size(), to);
        Reject([&] { f.Decode(); });
    });
    std::cout << "wolf2009 geometry self-test: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
} // namespace wolf2009
