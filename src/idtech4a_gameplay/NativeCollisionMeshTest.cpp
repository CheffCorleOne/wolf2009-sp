// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeCollisionMesh.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Refuses(F operation) {
    bool refused = false;
    try {
        operation();
    } catch (const std::runtime_error &) {
        refused = true;
    }
    Check(refused, "Expected native collision import refusal");
}
wolf2009::NativeCollisionTriangle Triangle(unsigned a, unsigned b, unsigned c, unsigned contents = 1) {
    wolf2009::NativeCollisionTriangle t;
    t.vertex[0] = a;
    t.vertex[1] = b;
    t.vertex[2] = c;
    t.contents = contents;
    return t;
}
wolf2009::NativeCollisionMesh Quad() {
    wolf2009::NativeCollisionMesh mesh;
    mesh.vertices = {{-64, -64, 0}, {64, -64, 32}, {64, 64, 32}, {-64, 64, 0}};
    mesh.triangles = {Triangle(0, 1, 2), Triangle(0, 2, 3)};
    return mesh;
}
// Read the emitted wire fields independently; verify that signed edge loops
// really bound each face in native CCW order with outward front normal.
void Topology(const std::string &text, std::size_t expectedVertices, std::size_t expectedFaces) {
    std::istringstream in(text);
    std::string token;
    while (in >> token && token != "vertices") {
    }
    std::size_t count;
    in >> token >> count;
    Check(token == "{" && count == expectedVertices, "CM vertex header");
    std::vector<wolf2009::CollisionVec3> points(count);
    for (auto &p : points) {
        in >> token >> p.x >> p.y >> p.z >> token;
        Check(token == ")", "CM vertex");
    }
    in >> token >> token >> token >> count;
    Check(token == "{", "CM edges header");
    struct Edge {
        unsigned a, b;
        int internal, users;
    };
    std::vector<Edge> edges(count);
    for (auto &edge : edges) {
        in >> token >> edge.a >> edge.b >> token >> edge.internal >> edge.users;
        Check(edge.a < points.size() && edge.b < points.size(), "CM edge vertex range");
    }
    while (in >> token && token != "polygons") {
    }
    in >> token;
    std::size_t faces = 0;
    while (in >> token && token != "}") {
        Check(token == "3", "CM triangular polygon");
        int refs[3];
        in >> token >> refs[0] >> refs[1] >> refs[2] >> token;
        wolf2009::CollisionVec3 normal, lo, hi;
        double distance;
        in >> token >> normal.x >> normal.y >> normal.z >> token >> distance;
        in >> token >> lo.x >> lo.y >> lo.z >> token;
        in >> token >> hi.x >> hi.y >> hi.z >> token >> token;
        wolf2009::CollisionVec3 p[3];
        unsigned starts[3], ends[3];
        for (unsigned k = 0; k < 3; ++k) {
            const unsigned edge = static_cast<unsigned>(std::abs(refs[k]));
            Check(edge != 0 && edge < edges.size(), "CM signed edge range");
            starts[k] = refs[k] > 0 ? edges[edge].a : edges[edge].b;
            ends[k] = refs[k] > 0 ? edges[edge].b : edges[edge].a;
            p[k] = points[starts[k]];
            Check(p[k].x >= lo.x && p[k].x <= hi.x && p[k].y >= lo.y && p[k].y <= hi.y &&
                      p[k].z >= lo.z && p[k].z <= hi.z,
                  "CM polygon bounds exclude vertex");
            Check(std::abs(normal.x * p[k].x + normal.y * p[k].y + normal.z * p[k].z - distance) < .0001,
                  "CM polygon plane excludes vertex");
        }
        for (unsigned k = 0; k < 3; ++k)
            Check(ends[k] == starts[(k + 1) % 3], "CM edge loop is open");
        const wolf2009::CollisionVec3 a = {p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z},
                                      b = {p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z};
        const double facing = (a.y * b.z - a.z * b.y) * normal.x + (a.z * b.x - a.x * b.z) * normal.y +
                              (a.x * b.y - a.y * b.x) * normal.z;
        Check(facing > 0, "CM winding disagrees with native CCW front");
        ++faces;
    }
    Check(in.good() && faces == expectedFaces, "CM polygon count");
}
} // namespace

int main() {
    using namespace wolf2009;
    try {
        auto mesh = Quad();
        auto one = BuildNativeCollisionMesh(mesh, "wolf2009/test/slope", 123456789u);
        Check(one.vertices == 4 && one.edges == 5 && one.polygons == 2 && one.internalEdges == 1,
              "Coplanar native seam was not identified");
        Check(one.cmText.find("123456789\ncollisionModel") != std::string::npos,
              "CM caller CRC was lost");
        Topology(one.cmText, 4, 2);
        mesh.vertices.push_back({-64, -64, -0.0});
        one = BuildNativeCollisionMesh(mesh, "slope");
        Check(one.vertexRemap[4] == one.vertexRemap[0] && one.vertices == 4,
              "Exact native vertex equality");
        mesh.vertices[4].z = .000001;
        Check(BuildNativeCollisionMesh(mesh, "slope").vertices == 5, "Vertices were epsilon welded");
        mesh = Quad();
        mesh.triangles[1].contents = 8;
        Check(BuildNativeCollisionMesh(mesh, "slope").internalEdges == 0,
              "Different contents were hidden as seam");
        mesh = Quad();
        mesh.vertices[3].z = 1;
        Check(BuildNativeCollisionMesh(mesh, "slope").internalEdges == 0,
              "Noncoplanar ridge was hidden as seam");
        mesh = Quad();
        for (auto &t : mesh.triangles)
            t.sides = NativeCollisionSides::Both;
        const auto both = BuildNativeCollisionMesh(mesh, "slope");
        Check(both.vertices == 4 && both.edges == 10 && both.polygons == 4 && both.internalEdges == 2,
              "Front/back sheets do not preserve both coplanar seams");
        Topology(both.cmText, 4, 4);
        mesh = Quad();
        for (const std::string &name : {"", "../slope", "/slope", "a//b", "a/", "a.cm", "a\"b", "a\\b"})
            Refuses([&] { BuildNativeCollisionMesh(mesh, name); });
        mesh.triangles[0].contents = 0;
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        mesh.triangles[0].vertex[2] = 4;
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        mesh.vertices[0].x = std::numeric_limits<double>::quiet_NaN();
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        mesh.vertices[0].z = 10000001;
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        // Finite, legal coordinates whose native float plane misses its own
        // triangle by almost one unit. Do not silently shift the source face.
        mesh.vertices = {
            {9999000, 9999000, 9999000}, {9998973, 9999069, 9998944}, {9999052, 9998930, 9999012}};
        mesh.triangles = {Triangle(0, 1, 2)};
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        mesh.vertices[1] = mesh.vertices[0];
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        mesh.vertices[1] = {-64.00000001, -64, 0};
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        mesh.vertices[0] = {0, 0, 0};
        mesh.vertices[1] = {1, 1, 1};
        mesh.vertices[2] = {2, 2, 2};
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh.vertices = {{0.0000001,0.0000003,0.0000007}, {1,3,7}, {2,6,14}};
        mesh.triangles = {Triangle(0,1,2)};
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        mesh.triangles.push_back(mesh.triangles[0]);
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        std::swap(mesh.triangles[1].vertex[1], mesh.triangles[1].vertex[2]);
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        mesh.triangles[0].sides = static_cast<NativeCollisionSides>(99);
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test"); });
        mesh = Quad();
        NativeCollisionMeshLimits limits;
        limits.textBytes = 32;
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test", 0, limits); });
        limits = NativeCollisionMeshLimits();
        limits.edges = 3;
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test", 0, limits); });
        limits = NativeCollisionMeshLimits();
        limits.triangles = 3;
        for (auto &t : mesh.triangles)
            t.sides = NativeCollisionSides::Both;
        Refuses([&] { BuildNativeCollisionMesh(mesh, "test", 0, limits); });
        std::cout << "WOLF2009_NATIVE_COLLISION_MESH_TEST_OK\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
