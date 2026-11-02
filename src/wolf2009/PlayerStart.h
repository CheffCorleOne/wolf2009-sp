// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_PLAYER_START_H
#define WOLF2009_PLAYER_START_H
#include "Entities.h"
#include "Geometry.h"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace wolf2009 {
enum class PlayerStartKind { Primary, Respawn, Teleport };
struct PlayerStartCandidate {
    std::size_t entityIndex = 0;
    PlayerStartKind kind = PlayerStartKind::Primary;
    EntityPlacement placement;
    // A naming hint only, not proof of runtime editor-only behavior.
    bool editorNameHint = false;
};
std::vector<PlayerStartCandidate> CollectPlayerStarts(const EntityDocument& entities);

enum class PlayerStartSelectionReason {
    UniquePrimary, ExplicitName, NoCandidates, NoPrimary,
    MultiplePrimaryCandidates, EditorNameNeedsExplicitSelection,
    RequestedNameNotFound, RequestedNameAmbiguous
};
struct PlayerStartSelection {
    bool selected = false;
    std::size_t candidateIndex = 0; // Valid only when selected is true.
    PlayerStartSelectionReason reason = PlayerStartSelectionReason::NoCandidates;
};
// A diagnostic choice, never a reconstruction of campaign/mission entry logic.
// Without a name, requires exactly one primary start and no editor-name hint.
// Respawns and teleports require explicit selection, not implicit fallback.
PlayerStartSelection SelectDiagnosticPlayerStart(const std::vector<PlayerStartCandidate>& candidates,
                                                 const std::string& requestedName = std::string());
const char* PlayerStartSelectionReasonText(PlayerStartSelectionReason reason);

struct PlayerStartProbeOptions {
    // These are prototype validation thresholds, not recovered game physics.
    float maxProbeDown = 256;
    float maxGroundGap = 8;
    float aboveOriginTolerance = .5f;
    float minimumUpNormal = .7f;
    std::size_t maxTriangles = 4000000;
    bool worldAreasOnly = true;
    // Optional caller-owned material/collision policy. No material is assumed
    // solid by this module merely because its triangles were rendered.
    std::function<bool(const GeometrySurface&)> surfaceFilter;
};
enum class PlayerStartSupportStatus {
    NearbyRenderSupport, MissingOrigin, NonFiniteOrigin,
    NoEligibleGeometry, OutsideProvidedGeometryBounds, NoGroundWithinProbe,
    GroundGapTooLarge, GroundTooSteep, MalformedGeometry, TriangleBudgetExceeded
};
struct PlayerStartSupport {
    PlayerStartSupportStatus status = PlayerStartSupportStatus::NoEligibleGeometry;
    bool hasHit = false, hasGeometryBounds = false, worldAreasOnly = true;
    float point[3] = {0, 0, 0};
    float rawNormal[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0}; // Flipped upward against the downward probe.
    float verticalGap = 0; // Authored origin.z - support.z; never applied as a snap.
    float geometryBounds[6] = {0, 0, 0, 0, 0, 0};
    std::size_t surfaceIndex = 0, triangleIndex = 0, checkedTriangles = 0;
    std::string model, material;
};
// Center-point vertical render-geometry probe in world XYZ coordinates (Z up).
// Does not move the candidate, build a player capsule, prove head/body clearance,
// evaluate spawn scripts, or substitute render triangles for Havok collision.
// Non-area surfaces must already have world-space placement when enabled.
// Invalid options throw; malformed or incomplete geometry returns an explicit status.
PlayerStartSupport AssessPlayerStart(const PlayerStartCandidate& candidate,
                                     const GeometryScene& geometry,
                                     const PlayerStartProbeOptions& options = PlayerStartProbeOptions());
const char* PlayerStartSupportStatusText(PlayerStartSupportStatus status);
int RunPlayerStartTests();
}
#endif
