// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_GAMEPLAY_MAP_H
#define WOLF2009_GAMEPLAY_MAP_H
#include "Entities.h"
#include "ModelCollision.h"
#include "PlayerStart.h"
#include "StaticCollision.h"
#include <cstddef>
#include <string>
#include <vector>

namespace wolf2009 {
// Explicit native content-bit mapping shared by brush and triangle adapters.
// Unknown combinations fail; no implicit SOLID material is supplied.
const char *GameplayCollisionMaterial(std::uint32_t contents);
// These are explicitly implemented Doom3 semantics, not aliases for arbitrary
// Wolf classnames/BRAI actions. Callers opt into each behavior after decoding it.
enum class GameplayEntityKind { PlayerStart, Static, Mover, Trigger, RemoveTargets, Point };
struct GameplayEntity {
    GameplayEntityKind kind = GameplayEntityKind::Point;
    std::string name;
    CollisionVec3 origin;
    double axis[9] = {1,0,0, 0,1,0, 0,0,1}; // idTech basis rows
    // Model-local exact collision data, never render bounds. The generated
    // inline clipmodel and visual model are independent resources.
    std::vector<StaticCollisionBox> localBoxes;
    std::string visualModel, bind;
    std::vector<std::string> targets;
    bool hidden = false;
    double triggerWait = .5, triggerDelay = 0;
    // An explicit command for native idMover, delivered by the map script.
    // Generic trigger activation does not imply a MoveToPos command.
    bool moveOnSpawn = false;
    CollisionVec3 moveDestination;
    double moveSpeed = 100;
};
struct GameplayMapLimits {
    std::size_t entities = 4096, boxes = 20000, textBytes = 32u << 20;
    double coordinate = 10000000;
};
struct GameplayMap {
    std::vector<StaticCollisionBox> worldBoxes;
    std::vector<GameplayEntity> entities;
    double gravity = 1066; // GPL Doom3 default, not measured Wolf gravity
    // Diagnostic metadata only; never written as engine behavior flags.
    std::size_t sourceEntities = 0, unimplementedEntities = 0;
};
struct GameplayMapFiles {
    std::string mapText, scriptText;
    std::size_t worldBoxes = 0, entityBoxes = 0, entities = 0;
};

// Strict import boundary for the presently complete, axis-box HKXS subset.
// Rejects all omitted bodies/shapes, unknown contents and unverified placement.
// A complete static body does not establish Wolf gameplay/filter compatibility.
std::vector<StaticCollisionBox> ImportGameplayWorld(const StaticCollisionScene &scene);
std::vector<StaticCollisionBox> ImportGameplayModel(const ModelCollision &model);
GameplayEntity ImportGameplayPlayerStart(const PlayerStartCandidate &candidate);

// Selects the diagnostic primary spawn, imports a fully covered world and
// reports all other ENTS objects as unimplemented. It never silently maps
// proprietary classnames to Doom3 doors, triggers, actors or weapons.
GameplayMap BuildStaticGameplayMap(const StaticCollisionScene &scene,
                                  const EntityDocument &entities,
                                  const std::string &requestedStart = std::string());

// Generates the standard Doom3 Version 2 map consumed by idMapFile::Parse ->
// collisionModelManager::LoadMap -> gameLocal::SpawnMapEntities. The companion
// script only delivers explicitly requested native mover commands on spawn.
// Names/targets/binds and proper rigid rotations are validated before output.
GameplayMapFiles EncodeGameplayMap(const GameplayMap &map,
                                  const GameplayMapLimits &limits = GameplayMapLimits());

// Resolves exactly: wolf2009/<mount>/PROC/maps/<resource>.map. No paths from
// other types, .reg aliases, dot segments, absolute paths or extra suffixes.
// Returns the same-mount typed source paths for the virtual read-only adapter.
struct GameplayMapSourcePaths { std::string entities, collision; };
bool ResolveGameplayMapPath(const std::string &path, GameplayMapSourcePaths &out);
} // namespace wolf2009
#endif
