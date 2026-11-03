// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeCollisionMesh.h"
#include "GameplayMap.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
void Need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
typedef std::array<float, 3> Point;
typedef std::array<std::uint32_t, 3> PointKey;
struct Face {
    std::array<std::uint32_t, 3> vertices;
    std::array<int, 3> edges;
    Point normal, minimum, maximum;
    float distance;
    std::uint32_t contents;
    bool back = false;
};
struct Edge {
    std::uint32_t a = 0, b = 0;
    std::size_t users = 0, face = 0;
    bool internal = false;
    int firstDirection = 0;
};
bool ModelName(const std::string &name) {
    if (name.empty() || name.size() > 255 || name.front() == '/' || name.back() == '/')
        return false;
    bool slash = false;
    for (unsigned char c : name) {
        if (c == '/') {
            if (slash)
                return false;
            slash = true;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-'))
                return false;
            slash = false;
        }
    }
    return true;
}
Point NativePoint(CollisionVec3 source, double limit) {
    const double coordinates[3] = {source.x, source.y, source.z};
    Point point;
    for (std::size_t k = 0; k < 3; ++k) {
        Need(std::isfinite(coordinates[k]) && std::abs(coordinates[k]) <= limit,
             "Native collision coordinate exceeds bounds");
        point[k] = static_cast<float>(coordinates[k]);
        Need(std::isfinite(point[k]), "Native collision float projection overflow");
        if (point[k] == 0)
            point[k] = 0; // one exact geometric zero, independent of sign bit
    }
    return point;
}
PointKey Key(const Point &point) {
    PointKey key;
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559,
                  "Native CM requires IEEE float32");
    for (std::size_t k = 0; k < 3; ++k)
        std::memcpy(&key[k], &point[k], sizeof(float));
    return key;
}
bool EqualPlane(const Face &a, const Face &b) {
    return a.normal == b.normal && a.distance == b.distance && a.contents == b.contents;
}
Face MakeFace(const NativeCollisionTriangle &source, const std::vector<std::uint32_t> &remap,
              const std::vector<Point> &vertices, double maximumPlaneError) {
    Face face;
    face.contents = source.contents;
    GameplayCollisionMaterial(source.contents); // reject unknown contents before constructing topology
    for (std::size_t k = 0; k < 3; ++k) {
        Need(source.vertex[k] < remap.size(), "Native collision triangle index is out of range");
        face.vertices[k] = remap[source.vertex[k]];
    }
    Need(face.vertices[0] != face.vertices[1] && face.vertices[0] != face.vertices[2] &&
             face.vertices[1] != face.vertices[2],
         "Native collision triangle collapses in float32");
    const Point &a = vertices[face.vertices[0]], &b = vertices[face.vertices[1]],
                &c = vertices[face.vertices[2]];
    double ab[3], ac[3], normal[3];
    for (std::size_t k = 0; k < 3; ++k) {
        ab[k] = double(b[k]) - a[k];
        ac[k] = double(c[k]) - a[k];
        face.minimum[k] = std::min(a[k], std::min(b[k], c[k]));
        face.maximum[k] = std::max(a[k], std::max(b[k], c[k]));
    }
    double normalError[3];
    for (std::size_t k = 0; k < 3; ++k) {
        const std::size_t i = (k + 1) % 3, j = (k + 2) % 3;
        // Fusing only one product can leave a nonzero residual for u x u.
        // Round both products consistently, then reject uncertain normals.
        volatile double positive = ab[i] * ac[j], negative = ab[j] * ac[i];
        normal[k] = positive - negative;
        normalError[k] = 16 * std::numeric_limits<double>::epsilon() *
                         (std::abs(positive) + std::abs(negative));
    }
    Need(std::abs(normal[0]) > normalError[0] || std::abs(normal[1]) > normalError[1] ||
             std::abs(normal[2]) > normalError[2],
         "Native collision triangle normal is numerically degenerate");
    const double length =
        std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    Need(std::isfinite(length) && length > 0, "Native collision triangle is degenerate");
    double distance = 0;
    for (std::size_t k = 0; k < 3; ++k) {
        face.normal[k] = static_cast<float>(normal[k] / length);
        distance += double(face.normal[k]) * a[k];
    }
    face.distance = static_cast<float>(distance);
    Need(std::isfinite(face.distance), "Native collision plane projection overflow");
    for (const Point *point : {&a, &b, &c}) {
        double residual = -double(face.distance);
        for (std::size_t k = 0; k < 3; ++k)
            residual += double(face.normal[k]) * (*point)[k];
        Need(std::abs(residual) <= maximumPlaneError,
             "Native collision float32 plane does not preserve its triangle");
    }
    // Native CM patch polygons use cross(b-a,c-a) with the same CCW edge
    // loop. idWinding::GetPlane uses the opposite convention; using that
    // helper's winding convention here would make finite traces miss.
    return face;
}
void Vector(std::ostream &out, const Point &p) {
    out << "( " << p[0] << ' ' << p[1] << ' ' << p[2] << " )";
}
void CheckText(std::ostringstream &out, std::size_t limit) {
    Need(out.good() && out.tellp() >= 0 && static_cast<std::uint64_t>(out.tellp()) <= limit,
         "Native collision text exceeds limit");
}
} // namespace

NativeCollisionMeshFiles BuildNativeCollisionMesh(const NativeCollisionMesh &mesh,
                                                  const std::string &modelName, std::uint32_t mapCRC,
                                                  const NativeCollisionMeshLimits &limits) {
    Need(ModelName(modelName), "Invalid native collision model name");
    Need(std::isfinite(limits.coordinate) && limits.coordinate > 0 &&
             std::isfinite(limits.planeProjectionError) && limits.planeProjectionError >= 0 &&
             limits.vertices > 0 && limits.triangles > 0 && limits.edges > 0 && limits.textBytes > 0,
         "Invalid native collision limits");
    const std::size_t maxInt = static_cast<std::size_t>(std::numeric_limits<int>::max());
    Need(!mesh.vertices.empty() && mesh.vertices.size() <= limits.vertices &&
             mesh.vertices.size() <= maxInt && !mesh.triangles.empty() &&
             mesh.triangles.size() <= limits.triangles && mesh.triangles.size() < maxInt / 3,
         "Native collision mesh exceeds count limits");

    NativeCollisionMeshFiles result;
    std::vector<Point> vertices;
    std::map<PointKey, std::uint32_t> vertexLookup;
    result.vertexRemap.reserve(mesh.vertices.size());
    for (CollisionVec3 source : mesh.vertices) {
        const Point point = NativePoint(source, limits.coordinate);
        const auto entry =
            vertexLookup.insert(std::make_pair(Key(point), static_cast<std::uint32_t>(vertices.size())));
        if (entry.second)
            vertices.push_back(point);
        result.vertexRemap.push_back(entry.first->second);
    }
    std::vector<Face> faces;
    faces.reserve(mesh.triangles.size());
    std::vector<Edge> edges(1); // CM signed edge references cannot represent reversed edge zero.
    std::map<std::array<std::uint32_t, 3>, int> edgeLookup;
    for (const auto &source : mesh.triangles) {
        Need(source.sides == NativeCollisionSides::Front || source.sides == NativeCollisionSides::Both,
             "Unknown native collision sidedness policy");
        const Face front = MakeFace(source, result.vertexRemap, vertices, limits.planeProjectionError);
        const int sheets = source.sides == NativeCollisionSides::Both ? 2 : 1;
        for (int sheet = 0; sheet < sheets; ++sheet) {
            Need(faces.size() < limits.triangles, "Native collision polygon count exceeds limit");
            Face face = front;
            face.back = sheet != 0;
            if (face.back) {
                std::swap(face.vertices[1], face.vertices[2]);
                for (float &component : face.normal)
                    component = -component;
                face.distance = -face.distance;
            }
            for (std::size_t k = 0; k < 3; ++k) {
                const std::uint32_t a = face.vertices[k], b = face.vertices[(k + 1) % 3];
                const std::array<std::uint32_t, 3> key = {
                    {std::min(a, b), std::max(a, b), static_cast<std::uint32_t>(face.back)}};
                auto found = edgeLookup.find(key);
                if (found == edgeLookup.end()) {
                    Need(edges.size() - 1 < limits.edges && edges.size() < maxInt,
                         "Native collision edges exceed limit");
                    const int index = static_cast<int>(edges.size());
                    found = edgeLookup.insert(std::make_pair(key, index)).first;
                    Edge edge;
                    edge.a = key[0];
                    edge.b = key[1];
                    edge.face = faces.size();
                    edges.push_back(edge);
                }
                Edge &edge = edges[found->second];
                const int direction = a == edge.a ? 1 : -1;
                Need(edge.users < 2 && (edge.users == 0 || direction != edge.firstDirection),
                     "Native collision sheet has non-manifold or inconsistent edge winding");
                face.edges[k] = direction * found->second;
                ++edge.users;
                if (edge.users == 1)
                    edge.firstDirection = direction;
                if (edge.users == 2)
                    edge.internal =
                        direction == -edge.firstDirection && EqualPlane(faces[edge.face], face);
            }
            faces.push_back(face);
        }
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "CM \"1.00\"\n"
        << mapCRC << "\ncollisionModel \"" << modelName << "\" {\nvertices {\n"
        << vertices.size() << '\n';
    CheckText(out, limits.textBytes);
    for (const auto &vertex : vertices) {
        Vector(out, vertex);
        out << '\n';
        CheckText(out, limits.textBytes);
    }
    out << "}\nedges {\n" << edges.size() << '\n';
    for (const auto &edge : edges) {
        out << "( " << edge.a << ' ' << edge.b << " ) " << int(edge.internal) << ' ' << edge.users
            << '\n';
        if (edge.internal)
            ++result.internalEdges;
        CheckText(out, limits.textBytes);
    }
    out << "}\nnodes {\n( -1 0 )\n}\npolygons {\n";
    CheckText(out, limits.textBytes);
    for (const auto &face : faces) {
        out << "3 ( " << face.edges[0] << ' ' << face.edges[1] << ' ' << face.edges[2] << " ) ";
        Vector(out, face.normal);
        out << ' ' << face.distance << ' ';
        Vector(out, face.minimum);
        out << ' ';
        Vector(out, face.maximum);
        out << " \"" << GameplayCollisionMaterial(face.contents) << "\"\n";
        CheckText(out, limits.textBytes);
    }
    out << "}\nbrushes {\n}\n}\n";
    CheckText(out, limits.textBytes);
    result.cmText = out.str();
    result.vertices = vertices.size();
    result.edges = edges.size() - 1;
    result.polygons = faces.size();
    return result;
}
} // namespace wolf2009
