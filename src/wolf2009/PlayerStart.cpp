// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerStart.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wolf2009 { namespace {
std::string Fold(std::string text) {
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] >= 'A' && text[i] <= 'Z') text[i] = char(text[i] + ('a' - 'A'));
    return text;
}
bool Finite3(const float* value) { return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]); }
bool FinitePosition(const GeometryVertex& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
PlayerStartSupport FailedProbe(PlayerStartSupport support, PlayerStartSupportStatus status) {
    support.status = status; support.hasHit = false; support.hasGeometryBounds = false; return support;
}
void Options(const PlayerStartProbeOptions& options) {
    if (!(std::isfinite(options.maxProbeDown) && options.maxProbeDown > 0 &&
          std::isfinite(options.maxGroundGap) && options.maxGroundGap >= 0 && options.maxGroundGap <= options.maxProbeDown &&
          std::isfinite(options.aboveOriginTolerance) && options.aboveOriginTolerance >= 0 && options.aboveOriginTolerance <= options.maxProbeDown &&
          std::isfinite(options.minimumUpNormal) && options.minimumUpNormal >= 0 && options.minimumUpNormal <= 1))
        throw std::invalid_argument("Invalid diagnostic player-start probe options");
}
} // namespace

std::vector<PlayerStartCandidate> CollectPlayerStarts(const EntityDocument& entities) {
    std::vector<PlayerStartCandidate> candidates;
    for (std::size_t i = 0; i < entities.entities.size(); ++i) {
        const EntityRecord& entity = entities.entities[i];
        const EntityProperty* classname = entity.Find("classname");
        if (!classname || classname->kind != EntityProperty::Text) continue;
        const std::string name = Fold(classname->text);
        PlayerStartKind kind;
        if (name == "info_player_start") kind = PlayerStartKind::Primary;
        else if (name == "info_player_respawn") kind = PlayerStartKind::Respawn;
        else if (name == "info_player_teleport") kind = PlayerStartKind::Teleport;
        else continue;
        PlayerStartCandidate candidate;
        candidate.entityIndex = i; candidate.kind = kind; candidate.placement = ReadEntityPlacement(entity);
        candidate.editorNameHint = Fold(candidate.placement.name).find("editor") != std::string::npos;
        candidates.push_back(candidate);
    }
    return candidates;
}

PlayerStartSelection SelectDiagnosticPlayerStart(const std::vector<PlayerStartCandidate>& candidates,
                                                 const std::string& requestedName) {
    PlayerStartSelection selection;
    if (candidates.empty()) return selection;
    if (!requestedName.empty()) {
        const std::string requested = Fold(requestedName); std::size_t matches = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i) if (Fold(candidates[i].placement.name) == requested) {
            selection.candidateIndex = i; ++matches;
        }
        if (matches != 1) {
            selection.reason = matches ? PlayerStartSelectionReason::RequestedNameAmbiguous : PlayerStartSelectionReason::RequestedNameNotFound;
            return selection;
        }
        selection.selected = true; selection.reason = PlayerStartSelectionReason::ExplicitName; return selection;
    }
    std::size_t primaries = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) if (candidates[i].kind == PlayerStartKind::Primary) {
        selection.candidateIndex = i; ++primaries;
    }
    if (primaries != 1) {
        selection.reason = primaries ? PlayerStartSelectionReason::MultiplePrimaryCandidates : PlayerStartSelectionReason::NoPrimary;
        return selection;
    }
    if (candidates[selection.candidateIndex].editorNameHint) {
        selection.reason = PlayerStartSelectionReason::EditorNameNeedsExplicitSelection; return selection;
    }
    selection.selected = true; selection.reason = PlayerStartSelectionReason::UniquePrimary; return selection;
}

PlayerStartSupport AssessPlayerStart(const PlayerStartCandidate& candidate, const GeometryScene& geometry,
                                     const PlayerStartProbeOptions& options) {
    Options(options);
    PlayerStartSupport support; support.worldAreasOnly = options.worldAreasOnly;
    if (!candidate.placement.hasOrigin) { support.status = PlayerStartSupportStatus::MissingOrigin; return support; }
    const float* origin = candidate.placement.origin;
    if (!Finite3(origin)) { support.status = PlayerStartSupportStatus::NonFiniteOrigin; return support; }
    float lower[3] = {(std::numeric_limits<float>::infinity)(), (std::numeric_limits<float>::infinity)(), (std::numeric_limits<float>::infinity)()};
    float upper[3] = {-lower[0], -lower[1], -lower[2]};
    double closestGap = (std::numeric_limits<double>::infinity)();
    for (std::size_t s = 0; s < geometry.surfaces.size(); ++s) {
        const GeometrySurface& surface = geometry.surfaces[s];
        if (options.worldAreasOnly && surface.model.compare(0, 5, "_area") != 0) continue;
        if (options.surfaceFilter && !options.surfaceFilter(surface)) continue;
        if (surface.indices.size() % 3) return FailedProbe(support, PlayerStartSupportStatus::MalformedGeometry);
        for (std::size_t t = 0; t < surface.indices.size() / 3; ++t) {
            if (support.checkedTriangles >= options.maxTriangles) return FailedProbe(support, PlayerStartSupportStatus::TriangleBudgetExceeded);
            ++support.checkedTriangles;
            const std::uint32_t ai = surface.indices[t * 3], bi = surface.indices[t * 3 + 1], ci = surface.indices[t * 3 + 2];
            if (ai >= geometry.vertices.size() || bi >= geometry.vertices.size() || ci >= geometry.vertices.size()) {
                return FailedProbe(support, PlayerStartSupportStatus::MalformedGeometry);
            }
            const GeometryVertex& a = geometry.vertices[ai];
            const GeometryVertex& b = geometry.vertices[bi];
            const GeometryVertex& c = geometry.vertices[ci];
            if (!FinitePosition(a) || !FinitePosition(b) || !FinitePosition(c)) {
                return FailedProbe(support, PlayerStartSupportStatus::MalformedGeometry);
            }
            const float values[3][3] = {{a.x,a.y,a.z},{b.x,b.y,b.z},{c.x,c.y,c.z}};
            for (unsigned p = 0; p < 3; ++p) for (unsigned k = 0; k < 3; ++k) {
                lower[k] = (std::min)(lower[k], values[p][k]); upper[k] = (std::max)(upper[k], values[p][k]);
            }
            support.hasGeometryBounds = true;
            const double abx = double(b.x) - a.x, aby = double(b.y) - a.y, abz = double(b.z) - a.z;
            const double acx = double(c.x) - a.x, acy = double(c.y) - a.y, acz = double(c.z) - a.z;
            const double determinant = abx * acy - aby * acx;
            if (std::fabs(determinant) <= 1e-12) continue; // No vertical-ray intersection with a vertical/degenerate face.
            const double dx = double(origin[0]) - a.x, dy = double(origin[1]) - a.y;
            const double u = (dx * acy - dy * acx) / determinant;
            const double v = (abx * dy - aby * dx) / determinant;
            if (u < -1e-6 || v < -1e-6 || u + v > 1.000001) continue;
            const double z = double(a.z) + u * abz + v * acz;
            const double gap = double(origin[2]) - z;
            if (gap < -options.aboveOriginTolerance || gap > options.maxProbeDown || gap >= closestGap) continue;
            const double raw[3] = {aby * acz - abz * acy, abz * acx - abx * acz, determinant};
            const double length = std::sqrt(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]);
            if (!(length > 0 && std::isfinite(length))) continue;
            closestGap = gap; support.hasHit = true; support.verticalGap = float(gap);
            support.point[0] = origin[0]; support.point[1] = origin[1]; support.point[2] = float(z);
            for (unsigned k = 0; k < 3; ++k) {
                support.rawNormal[k] = float(raw[k] / length);
                support.normal[k] = support.rawNormal[k] * (raw[2] < 0 ? -1 : 1);
            }
            support.surfaceIndex = s; support.triangleIndex = t; support.model = surface.model; support.material = surface.material;
        }
    }
    if (!support.hasGeometryBounds) { support.status = PlayerStartSupportStatus::NoEligibleGeometry; return support; }
    for (unsigned k = 0; k < 3; ++k) { support.geometryBounds[k] = lower[k]; support.geometryBounds[k + 3] = upper[k]; }
    if (!support.hasHit) {
        const bool outside = origin[0] < lower[0] - .001f || origin[0] > upper[0] + .001f ||
                             origin[1] < lower[1] - .001f || origin[1] > upper[1] + .001f;
        support.status = outside ? PlayerStartSupportStatus::OutsideProvidedGeometryBounds : PlayerStartSupportStatus::NoGroundWithinProbe;
    } else if (support.normal[2] < options.minimumUpNormal) support.status = PlayerStartSupportStatus::GroundTooSteep;
    else if (support.verticalGap > options.maxGroundGap) support.status = PlayerStartSupportStatus::GroundGapTooLarge;
    else support.status = PlayerStartSupportStatus::NearbyRenderSupport;
    return support;
}

const char* PlayerStartSelectionReasonText(PlayerStartSelectionReason reason) {
    switch (reason) {
        case PlayerStartSelectionReason::UniquePrimary: return "Unique primary start selected for diagnostics";
        case PlayerStartSelectionReason::ExplicitName: return "Named start selected explicitly for diagnostics";
        case PlayerStartSelectionReason::NoCandidates: return "No player-start candidates were found";
        case PlayerStartSelectionReason::NoPrimary: return "Only respawn/teleport candidates exist; select one explicitly";
        case PlayerStartSelectionReason::MultiplePrimaryCandidates: return "Multiple primary starts require explicit selection";
        case PlayerStartSelectionReason::EditorNameNeedsExplicitSelection: return "Editor-related name requires explicit diagnostic selection";
        case PlayerStartSelectionReason::RequestedNameNotFound: return "Requested player-start name was not found";
        case PlayerStartSelectionReason::RequestedNameAmbiguous: return "Requested player-start name is ambiguous";
    }
    return "Unknown player-start selection result";
}
const char* PlayerStartSupportStatusText(PlayerStartSupportStatus status) {
    switch (status) {
        case PlayerStartSupportStatus::NearbyRenderSupport: return "Nearby render support found; collision is not established";
        case PlayerStartSupportStatus::MissingOrigin: return "Player-start origin is missing";
        case PlayerStartSupportStatus::NonFiniteOrigin: return "Player-start origin is not finite";
        case PlayerStartSupportStatus::NoEligibleGeometry: return "No eligible geometry was provided";
        case PlayerStartSupportStatus::OutsideProvidedGeometryBounds: return "Origin is outside the provided geometry's horizontal bounds";
        case PlayerStartSupportStatus::NoGroundWithinProbe: return "No support was found within the configured vertical probe";
        case PlayerStartSupportStatus::GroundGapTooLarge: return "Ground gap is too large; missing placed geometry may be required";
        case PlayerStartSupportStatus::GroundTooSteep: return "Nearest intersected surface is too steep";
        case PlayerStartSupportStatus::MalformedGeometry: return "Provided triangle geometry is malformed";
        case PlayerStartSupportStatus::TriangleBudgetExceeded: return "Support probe triangle budget was exceeded";
    }
    return "Unknown player-start support result";
}
}
