// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerStart.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace wolf2009 { namespace {
void Assert(bool value) { if (!value) throw std::runtime_error("Assertion failed"); }
void Near(float a, float b) { Assert(std::fabs(a - b) < .0001f); }
EntityProperty Text(const std::string& key, const std::string& text) {
    EntityProperty p; p.key = key; p.kind = EntityProperty::Text; p.text = text; return p;
}
EntityProperty Numbers(const std::string& key, const std::vector<float>& values) {
    EntityProperty p; p.key = key; p.kind = EntityProperty::Floats; p.numbers = values; return p;
}
EntityRecord Entity(const std::string& classname, const std::string& name) {
    EntityRecord e; e.properties.push_back(Text("classname", classname)); e.properties.push_back(Text("name", name));
    e.properties.push_back(Numbers("origin", {0,0,0})); return e;
}
PlayerStartCandidate Start(float x = 0, float y = 0, float z = 0) {
    PlayerStartCandidate c; c.placement.name = "synthetic_start"; c.placement.classname = "info_player_start";
    c.placement.hasOrigin = true; c.placement.origin[0] = x; c.placement.origin[1] = y; c.placement.origin[2] = z; return c;
}
GeometryScene Floor(float z = 0, bool reverse = false) {
    GeometryScene scene; scene.models = 1;
    scene.vertices = {{-10,-10,z,0,0},{10,-10,z,0,0},{-10,10,z,0,0},{10,10,z,0,0}};
    GeometrySurface surface; surface.model = "_area_synthetic"; surface.material = "synthetic/floor";
    surface.indices = reverse ? std::vector<std::uint32_t>{0,2,1,1,2,3} : std::vector<std::uint32_t>{0,1,2,1,3,2};
    scene.surfaces.push_back(surface); return scene;
}
void Reject(const std::function<void()>& operation) {
    bool threw = false; try { operation(); } catch (const std::exception&) { threw = true; } Assert(threw);
}
} // namespace

int RunPlayerStartTests() {
    unsigned passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& operation) {
        try { operation(); ++passed; }
        catch (const std::exception& error) { ++failed; std::cerr << "player start test " << name << ": " << error.what() << '\n'; }
    };
    test("collect primary/respawn/teleport and preserve source fields", [] {
        EntityDocument d; d.entities.push_back(Entity("worldspawn", "synthetic_world"));
        EntityRecord primary = Entity("INFO_PLAYER_START", "synthetic_primary");
        primary.properties.push_back(Numbers("rotation", {0,1,0,-1,0,0,0,0,1}));
        EntityProperty angle; angle.key = "angle"; angle.kind = EntityProperty::Integer; angle.integer = 90; primary.properties.push_back(angle);
        d.entities.push_back(primary); d.entities.push_back(Entity("info_player_respawn", "synthetic_respawn"));
        d.entities.push_back(Entity("info_player_teleport", "synthetic_teleport")); d.entities.push_back(Entity("target_null", "synthetic_target"));
        const auto candidates = CollectPlayerStarts(d);
        Assert(candidates.size() == 3 && candidates[0].entityIndex == 1 && candidates[0].kind == PlayerStartKind::Primary);
        Assert(candidates[1].kind == PlayerStartKind::Respawn && candidates[2].kind == PlayerStartKind::Teleport);
        Assert(candidates[0].placement.hasAngle && candidates[0].placement.hasRotation);
        Near(candidates[0].placement.angle, 90); Near(candidates[0].placement.rotation[1], 1);
        const auto selected = SelectDiagnosticPlayerStart(candidates);
        Assert(selected.selected && selected.candidateIndex == 0 && selected.reason == PlayerStartSelectionReason::UniquePrimary);
    });
    test("editor-related name is only a hint requiring explicit diagnostic selection", [] {
        EntityDocument d; d.entities.push_back(Entity("info_player_start", "synthetic_EDITOR_marker"));
        const auto candidates = CollectPlayerStarts(d); Assert(candidates.size() == 1 && candidates[0].editorNameHint);
        const auto automatic = SelectDiagnosticPlayerStart(candidates);
        Assert(!automatic.selected && automatic.reason == PlayerStartSelectionReason::EditorNameNeedsExplicitSelection);
        const auto explicitSelection = SelectDiagnosticPlayerStart(candidates, "SYNTHETIC_editor_MARKER");
        Assert(explicitSelection.selected && explicitSelection.reason == PlayerStartSelectionReason::ExplicitName);
    });
    test("multiple primary starts and named ambiguity never choose the first", [] {
        PlayerStartCandidate a = Start(), b = Start(); b.placement.name = "synthetic_other";
        Assert(SelectDiagnosticPlayerStart({a,b}).reason == PlayerStartSelectionReason::MultiplePrimaryCandidates);
        Assert(!SelectDiagnosticPlayerStart({a,b}).selected);
        Assert(SelectDiagnosticPlayerStart({a,b}, "synthetic_other").candidateIndex == 1);
        b.placement.name = a.placement.name;
        const auto duplicate = SelectDiagnosticPlayerStart({a,b}, a.placement.name);
        Assert(!duplicate.selected && duplicate.reason == PlayerStartSelectionReason::RequestedNameAmbiguous);
    });
    test("no implicit fallback from primary to respawn or teleport", [] {
        PlayerStartCandidate a = Start(); a.kind = PlayerStartKind::Respawn;
        const auto choice = SelectDiagnosticPlayerStart({a}); Assert(!choice.selected && choice.reason == PlayerStartSelectionReason::NoPrimary);
        Assert(SelectDiagnosticPlayerStart({a}, a.placement.name).selected);
        a.kind = PlayerStartKind::Teleport; Assert(!SelectDiagnosticPlayerStart({a}).selected);
        Assert(SelectDiagnosticPlayerStart({}).reason == PlayerStartSelectionReason::NoCandidates);
        Assert(SelectDiagnosticPlayerStart({Start()}, "synthetic_missing").reason == PlayerStartSelectionReason::RequestedNameNotFound);
    });
    test("floor support with either triangle winding", [] {
        for (unsigned reverse = 0; reverse < 2; ++reverse) {
            const auto result = AssessPlayerStart(Start(), Floor(0, reverse != 0));
            Assert(result.status == PlayerStartSupportStatus::NearbyRenderSupport && result.hasHit && result.hasGeometryBounds);
            Near(result.verticalGap, 0); Near(result.normal[2], 1); Near(result.point[2], 0);
            Near(result.rawNormal[2], reverse ? -1 : 1); Assert(result.checkedTriangles == 2 && result.material == "synthetic/floor");
        }
    });
    test("ground assessment does not move authored origin", [] {
        const auto candidate = Start(0,0,10); PlayerStartProbeOptions options; options.maxGroundGap = 16;
        const auto result = AssessPlayerStart(candidate, Floor(), options);
        Assert(result.status == PlayerStartSupportStatus::NearbyRenderSupport); Near(result.verticalGap, 10);
        Near(candidate.placement.origin[2], 10); Near(result.point[2], 0);
    });
    test("missing/nonfinite origins", [] {
        auto candidate = Start(); candidate.placement.hasOrigin = false;
        Assert(AssessPlayerStart(candidate, Floor()).status == PlayerStartSupportStatus::MissingOrigin);
        candidate.placement.hasOrigin = true; candidate.placement.origin[0] = (std::numeric_limits<float>::quiet_NaN)();
        Assert(AssessPlayerStart(candidate, Floor()).status == PlayerStartSupportStatus::NonFiniteOrigin);
    });
    test("no provided world area geometry", [] {
        Assert(AssessPlayerStart(Start(), GeometryScene()).status == PlayerStartSupportStatus::NoEligibleGeometry);
        auto scene = Floor(); scene.surfaces[0].model = "synthetic_placed_model";
        Assert(AssessPlayerStart(Start(), scene).status == PlayerStartSupportStatus::NoEligibleGeometry);
        PlayerStartProbeOptions options; options.worldAreasOnly = false;
        Assert(AssessPlayerStart(Start(), scene, options).status == PlayerStartSupportStatus::NearbyRenderSupport);
    });
    test("outside provided horizontal geometry bounds", [] {
        const auto result = AssessPlayerStart(Start(20,20,0), Floor());
        Assert(result.status == PlayerStartSupportStatus::OutsideProvidedGeometryBounds && !result.hasHit && result.hasGeometryBounds);
        Near(result.geometryBounds[0], -10); Near(result.geometryBounds[3], 10);
    });
    test("hole inside bounds and probe range are explicit", [] {
        auto scene = Floor(); scene.surfaces[0].indices.resize(3);
        Assert(AssessPlayerStart(Start(8,8,0), scene).status == PlayerStartSupportStatus::NoGroundWithinProbe);
        Assert(AssessPlayerStart(Start(0,0,300), Floor()).status == PlayerStartSupportStatus::NoGroundWithinProbe);
    });
    test("large gap is reported with the hit and no snap", [] {
        const auto candidate = Start(0,0,40); const auto result = AssessPlayerStart(candidate, Floor());
        Assert(result.status == PlayerStartSupportStatus::GroundGapTooLarge && result.hasHit);
        Near(result.verticalGap, 40); Near(candidate.placement.origin[2], 40);
    });
    test("steep nearest surface is rejected", [] {
        auto scene = Floor(); scene.vertices[0].z = -100; scene.vertices[1].z = 100; scene.vertices[2].z = -100; scene.vertices[3].z = 100;
        const auto result = AssessPlayerStart(Start(), scene);
        Assert(result.hasHit && result.status == PlayerStartSupportStatus::GroundTooSteep && result.normal[2] < .7f);
    });
    test("small above-origin decal tolerance is explicit", [] {
        const auto result = AssessPlayerStart(Start(), Floor(.25f));
        Assert(result.status == PlayerStartSupportStatus::NearbyRenderSupport); Near(result.verticalGap, -.25f);
        Assert(AssessPlayerStart(Start(), Floor(2)).status == PlayerStartSupportStatus::NoGroundWithinProbe);
    });
    test("caller filter controls which surfaces are eligible", [] {
        PlayerStartProbeOptions options; options.surfaceFilter = [](const GeometrySurface&) { return false; };
        Assert(AssessPlayerStart(Start(), Floor(), options).status == PlayerStartSupportStatus::NoEligibleGeometry);
    });
    test("malformed indices and nonfinite referenced vertices", [] {
        auto scene = Floor(); scene.surfaces[0].indices.pop_back();
        Assert(AssessPlayerStart(Start(), scene).status == PlayerStartSupportStatus::MalformedGeometry);
        scene = Floor(); scene.surfaces[0].indices[0] = 100;
        Assert(AssessPlayerStart(Start(), scene).status == PlayerStartSupportStatus::MalformedGeometry);
        scene = Floor(); scene.vertices[0].x = (std::numeric_limits<float>::infinity)();
        Assert(AssessPlayerStart(Start(), scene).status == PlayerStartSupportStatus::MalformedGeometry);
    });
    test("budget failure does not expose a partial accepted hit", [] {
        PlayerStartProbeOptions options; options.maxTriangles = 1;
        const auto result = AssessPlayerStart(Start(), Floor(), options);
        Assert(result.status == PlayerStartSupportStatus::TriangleBudgetExceeded && !result.hasHit && !result.hasGeometryBounds);
    });
    test("invalid probe options", [] {
        PlayerStartProbeOptions options; options.maxProbeDown = -1;
        Reject([&] { AssessPlayerStart(Start(), Floor(), options); });
        options = PlayerStartProbeOptions(); options.minimumUpNormal = 2;
        Reject([&] { AssessPlayerStart(Start(), Floor(), options); });
        options = PlayerStartProbeOptions(); options.maxGroundGap = 1000;
        Reject([&] { AssessPlayerStart(Start(), Floor(), options); });
    });
    std::cout << "wolf2009 player-start self-test: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
}
