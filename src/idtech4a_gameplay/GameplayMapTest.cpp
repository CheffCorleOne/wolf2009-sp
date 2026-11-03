// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameplayFixture.h"
#include "MapPhysics.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
void Check(bool b,const char *message) { if (!b) throw std::runtime_error(message); }
template<class F> void Reject(F run,const char *message) {
    bool rejected=false;
    try { run(); } catch (const std::exception &) { rejected=true; }
    Check(rejected,message);
}
void Synthetic() {
    using namespace wolf2009;
    auto map=MakeGameplayFixture();
    const auto files=EncodeGameplayMap(map);
    Check(files.worldBoxes==2 && files.entityBoxes==3 && files.entities==6,"Fixture object counts changed");
    Check(files.mapText.find("( 1 0 0 -176 )")!=std::string::npos &&
          files.mapText.find("( 0 0 -1 -16 )")!=std::string::npos,"Exact brush plane distances lost");
    Check(files.mapText.find("\"wolf2009_collisionmodel\" \"runtime_platform\"")!=std::string::npos,
          "Inline collision key lost");
    Check(files.mapText.find("\"classname\" \"wolf2009_trigger\"")!=std::string::npos &&
          files.mapText.find("\"target1\" \"runtime_remove_targets\"")!=std::string::npos,
          "Native trigger/target lifecycle wiring lost");
    Check(files.scriptText.find("mover.moveToPos('-128 -64 16')")!=std::string::npos,
          "Native mover command not emitted");
    map.entities[1].visualModel="wolf2009/fixture/MODL/models/platform.md5r";
    const auto visual=EncodeGameplayMap(map);
    Check(visual.mapText.find("\"clipmodel\" \"runtime_platform\"")!=std::string::npos &&
          visual.mapText.find("\"model\" \"wolf2009/fixture/MODL/models/platform.md5r\"")!=std::string::npos,
          "Collision and visual model resources not independent");
    auto invalid=map; invalid.worldBoxes[0].contentsKnown=false;
    Reject([&]{EncodeGameplayMap(invalid);},"Unknown world contents accepted");
    invalid=map; invalid.worldBoxes[0].contents=256;
    Reject([&]{EncodeGameplayMap(invalid);},"BODY silently converted into static SOLID");
    invalid=map; invalid.entities[4].targets[0]="missing";
    Reject([&]{EncodeGameplayMap(invalid);},"Missing target accepted");
    invalid=map; invalid.entities[2].name=invalid.entities[1].name;
    Reject([&]{EncodeGameplayMap(invalid);},"Duplicate entity name accepted");
    invalid=map; invalid.entities[1].bind=invalid.entities[2].name; invalid.entities[2].bind=invalid.entities[1].name;
    Reject([&]{EncodeGameplayMap(invalid);},"Bind cycle accepted");
    invalid=map; invalid.entities[1].axis[0]=2;
    Reject([&]{EncodeGameplayMap(invalid);},"Scaled entity collision accepted");
    invalid=map; invalid.entities[0].origin.x=std::numeric_limits<double>::quiet_NaN();
    Reject([&]{EncodeGameplayMap(invalid);},"Nonfinite start accepted");
    invalid=map; invalid.entities.back().localBoxes[0].contents=1;
    Reject([&]{EncodeGameplayMap(invalid);},"Trigger/solid role mismatch accepted");
    invalid=map; invalid.entities[1].visualModel="models/../escape.md5r";
    Reject([&]{EncodeGameplayMap(invalid);},"Visual resource traversal accepted");
    invalid=map; invalid.worldBoxes[0].maximum=invalid.worldBoxes[0].minimum;
    Reject([&]{EncodeGameplayMap(invalid);},"Degenerate collision box accepted");
    GameplayMapLimits limits; limits.boxes=4;
    Reject([&]{EncodeGameplayMap(map,limits);},"Combined box budget not enforced");
    limits=GameplayMapLimits(); limits.textBytes=64;
    Reject([&]{EncodeGameplayMap(map,limits);},"Map text budget not enforced");
    StaticCollisionScene world;
    world.boxes=map.worldBoxes; world.coverageComplete=true;
    world.contentsComplete=true; world.identityBodyPoseVerified=true;
    Check(ImportGameplayWorld(world).size()==2,"Complete exact HKXS subset refused");
    world.otherRigidBodies=1;
    Reject([&]{ImportGameplayWorld(world);},"Omitted dynamic/body collision accepted");
    world.otherRigidBodies=0; world.coverageComplete=false;
    Reject([&]{ImportGameplayWorld(world);},"Incomplete shape graph accepted");
    ModelCollision model;
    model.localBoxes=map.entities[1].localBoxes; model.geometryComplete=true;
    model.contentsComplete=true; model.localPoseVerified=true;
    Check(ImportGameplayModel(model).size()==1,"Complete model subset refused");
    model.unsupportedGeometry["Cylinder"]=1;
    Reject([&]{ImportGameplayModel(model);},"Unsupported exact model geometry accepted");
    GameplayMapSourcePaths paths;
    Check(ResolveGameplayMapPath("wolf2009/main/PROC/maps/game/empty/empty.map",paths) &&
          paths.entities=="wolf2009/main/ENTS/maps/game/empty/empty.ents" &&
          paths.collision=="wolf2009/main/HKXS/maps/game/empty/empty.hkxs","Typed map source resolution failed");
    const char *bad[]={"wolf2009/main/PROC/maps/game/../empty.map","wolf2009/main/HKXS/maps/a.map",
        "wolf2009/main/PROC/maps/a.map.map","wolf2009/main/PROC/maps/a.reg","maps/a.map",
        "wolf2009/main/PROC/maps/a.map\n","wolf2009/main/PROC/maps//a.map"};
    for(const auto *path:bad) Check(!ResolveGameplayMapPath(path,paths),"Invalid map resource path accepted");
    PlayerStartCandidate start;
    start.placement.name="rotated_start"; start.placement.hasOrigin=true;
    start.placement.hasAngle=true; start.placement.angle=90;
    const auto rotated=ImportGameplayPlayerStart(start);
    Check(std::abs(rotated.axis[0])<1e-8 && std::abs(rotated.axis[1]-1)<1e-8,
          "Yaw 90 does not face world +Y");
    std::cout << "WOLF2009_GAMEPLAY_MAP_SYNTHETIC_OK\n";
}
void Original(const char *path) {
    using namespace wolf2009;
    std::ifstream input(path,std::ios::binary);
    Check(bool(input),"Cannot open supplied map");
    const auto archive=Archive::Scan(input);
    std::string name;
    for(const auto &chunk:archive.Chunks()) if(chunk.type=="PROC") for(const auto &entry:chunk.entries) {
        Check(name.empty(),"More than one map in supplied archive"); name=entry.name;
    }
    Check(!name.empty(),"Map PROC absent");
    const auto scene=ReadMapStaticCollision(input,archive,name);
    std::map<std::uint32_t,std::size_t> contents;
    for(const auto &box:scene.boxes) ++contents[box.contents];
    for(const auto &entry:contents)
        std::cout << "Source collision contents=" << entry.first << " boxes=" << entry.second << '\n';
    const auto entities=ReadMapEntities(input,archive,name);
    const auto map=BuildStaticGameplayMap(scene,entities);
    const auto result=EncodeGameplayMap(map);
    Check(result.worldBoxes==scene.boxes.size() && result.entityBoxes==0 && result.entities==2,
          "Real static collision was modified/dropped by bridge");
    Check(map.entities.front().origin.x==0 && map.entities.front().origin.y==0 &&
          map.entities.front().origin.z==0,"Expected empty.mpk authored start");
    std::cout << "WOLF2009_GAMEPLAY_EMPTY_DATA_OK world_boxes=" << result.worldBoxes
              << " unimplemented_ents=" << map.unimplementedEntities << "\n";
}
}
int main(int argc,char **argv) {
    try { Synthetic(); if(argc==2) Original(argv[1]); return 0; }
    catch(const std::exception &error) { std::cerr << "Gameplay map test: " << error.what() << '\n'; return 1; }
}
