// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "Entities.h"
#include "idlib/precompiled.h"
#include "game/Game_local.h"

namespace {
const char* const TrainyardEntities = "wolf2009/main/ENTS/maps/game/trainyard/trainyard";

void Need(unsigned& checks, bool ok, const char* reason) {
    ++checks;
    if (!ok) throw std::runtime_error(reason);
}

struct CloseFile {
    void operator()(idFile* file) const { fileSystem->CloseFile(file); }
};

wolf2009::EntityDocument ReadTrainyardEntities(unsigned& checks) {
    std::unique_ptr<idFile, CloseFile> file(fileSystem->OpenFileRead(TrainyardEntities, false));
    Need(checks, bool(file), "Original trainyard ENTS member is unavailable");
    const int length = file->Length();
    Need(checks, length > 0 && std::size_t(length) <= wolf2009::EntityLimits().sourceBytes,
         "Trainyard ENTS member is outside the source byte budget");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    Need(checks, file->Read(bytes.data(), length) == length, "Short read of the original trainyard ENTS member");
    return wolf2009::DecodeEntities4(bytes);
}

idVec3 SourceOrigin(unsigned& checks, const wolf2009::EntityDocument& document,
                    const char* name, const char* classname) {
    const wolf2009::EntityRecord* match = NULL;
    for (const auto& entity : document.entities) {
        const auto* key = entity.Find("name");
        if (!key || key->kind != wolf2009::EntityProperty::Text || key->text != name) continue;
        Need(checks, match == NULL, "Trainyard ENTS placement has a duplicate name");
        match = &entity;
    }
    Need(checks, match != NULL, "Trainyard ENTS placement is absent");
    const auto placement = wolf2009::ReadEntityPlacement(*match);
    Need(checks, placement.classname == classname && placement.hasOrigin,
         "Trainyard ENTS placement has a different class or no origin");
    return idVec3(placement.origin[0], placement.origin[1], placement.origin[2]);
}
}

extern "C" __attribute__((visibility("default"))) int Wolf2009_CheckTrainyardAasQA(int expectedLoaded) {
    unsigned checks = 0, hops = 0;
    int area = 0, goal = 0;
    try {
        const idAAS* aas = gameLocal.GetAAS("aas28");
        Need(checks, (aas != NULL) == (expectedLoaded != 0), "Unexpected availability of native aas28");
        if (!expectedLoaded) {
            gameLocal.Printf("WOLF2009_TRAINYARD_AAS_GAME_QA_OK loaded=0 checks=%u hops=0\n", checks);
            return 1;
        }

        Need(checks, gameLocal.GameState() == GAMESTATE_ACTIVE, "Trainyard AAS check requires an active game");
        const auto document = ReadTrainyardEntities(checks);
        const idVec3 greeter = SourceOrigin(checks, document, "train_greeter", "trainyard_greeter");
        const idVec3 ladder = SourceOrigin(checks, document, "run_to_ladder_point", "target_null");
        const idAASSettings* settings = aas->GetSettings();
        Need(checks, settings && settings->numBoundingBoxes == 1 && settings->fileExtension == "aas28",
             "Native AAS does not have the measured aas28 hull settings");

        // The greeter origin is on a floor plane. Query one unit above it;
        // the ladder marker is already above the floor, so query it directly.
        const idVec3 origin = greeter + idVec3(0, 0, 1);
        const idBounds& hull = settings->boundingBoxes[0];
        area = aas->PointReachableAreaNum(origin, hull, AREA_REACHABLE_WALK);
        goal = aas->PointReachableAreaNum(ladder, hull, AREA_REACHABLE_WALK);
        Need(checks, area == 6199 && goal == 8040, "Native reachable-area lookup differs for trainyard placements");
        Need(checks, (aas->AreaFlags(area) & AREA_FLOOR) && (aas->AreaFlags(goal) & AREA_FLOOR),
             "Native trainyard endpoints do not both have floor flags");

        // A portal source can yield a default first reach from an empty native
        // cache. Verify the full chain reaches the goal without moving an entity.
        const int travelMask = TFL_WALK | TFL_AIR;
        const unsigned maxHops = 256;
        std::set<int> visited;
        idVec3 point = origin;
        int firstReach = 0, firstTime = 0;
        unsigned zeroCostEdges = 0;
        while (area != goal) {
            Need(checks, hops < maxHops && visited.insert(area).second,
                 "Native trainyard route cycled or exceeded its hop budget");
            int travelTime = 0;
            idReachability* reach = NULL;
            Need(checks, aas->RouteToGoalArea(area, point, goal, travelMask, travelTime, &reach) && reach,
                 "Native trainyard route returned no reachability");
            Need(checks, reach->fromAreaNum == area && reach->toAreaNum > 0 && reach->toAreaNum != area,
                 "Native trainyard reachability does not continue from the current area");
            Need(checks, (reach->travelType & TFL_WALK) && !(reach->travelType & ~travelMask),
                 "Native trainyard reachability uses a travel type outside the admitted mask");
            // The shipped 8351 -> 8348 WALK edge has zero raw cost.
            // The accumulated native route time must still be positive.
            Need(checks, travelTime > 0, "Native trainyard route has a nonpositive travel time");
            if (!reach->travelTime) ++zeroCostEdges;
            Need(checks, std::isfinite(reach->end.x) && std::isfinite(reach->end.y) && std::isfinite(reach->end.z),
                 "Native trainyard reachability has a nonfinite endpoint");
            if (!hops) {
                firstReach = reach->toAreaNum;
                firstTime = travelTime;
            }
            area = reach->toAreaNum;
            point = reach->end;
            ++hops;
        }

        gameLocal.Printf("WOLF2009_TRAINYARD_AAS_GAME_QA_OK loaded=1 checks=%u hops=%u start=6199 goal=%d "
                         "first_reach=%d first_time=%d zero_cost_edges=%u travel_mask=%d greeter_point_area=%d ladder_point_area=%d\n",
                         checks, hops, goal, firstReach, firstTime, zeroCostEdges, travelMask,
                         aas->PointAreaNum(greeter), aas->PointAreaNum(ladder));
        return 1;
    } catch (const std::exception& error) {
        gameLocal.Warning("WOLF2009_TRAINYARD_AAS_GAME_QA_FAIL loaded=%d checks=%u hops=%u area=%d goal=%d reason=%s",
                          expectedLoaded, checks, hops, area, goal, error.what());
        return 0;
    }
}
