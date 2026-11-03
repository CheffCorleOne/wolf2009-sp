// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_PLAYER_HULL_H
#define WOLF2009_SOURCE_PLAYER_HULL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wolf2009 {

enum class SourcePlayerHullPolicy { Unspecified, NativeFloatExactConvexSurfaceV1 };
struct SourcePlayerHullLimits {
    std::size_t maximumVertices = 64;
    std::size_t maximumEdges = 160;
    std::size_t maximumFaces = 96;
};
struct SourcePlayerHullFace {
    std::array<std::uint32_t,3> vertices;
    std::array<int,3> edges;
};
struct SourcePlayerHull {
    // Every caller-selected support is retained verbatim and must be used by
    // the final surface. No padding, epsilon displacement or hull reduction.
    std::vector<std::array<float,3>> points;
    std::vector<std::array<std::uint32_t,2>> edges;
    std::vector<SourcePlayerHullFace> faces;
    std::size_t exactPredicates = 0;
};

// Exact orientation of finite IEEE binary32 inputs. A double filter handles
// well-separated cases; a bounded integer dyadic determinant resolves zero
// and near-coplanar cases, with no long-double/platform precision dependency.
int SourcePlayerHullOrientation(const std::array<float,3>& a,
    const std::array<float,3>& b,const std::array<float,3>& c,
    const std::array<float,3>& d);
SourcePlayerHull BuildSourcePlayerHull(const std::vector<std::array<float,3>>& points,
    SourcePlayerHullPolicy policy,const SourcePlayerHullLimits& limits = SourcePlayerHullLimits());

}
#endif
